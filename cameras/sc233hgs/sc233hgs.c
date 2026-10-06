// SPDX-License-Identifier: GPL-2.0
/*
 * SmartSens SC233HGS CSI-2 sensor driver for Raspberry Pi 5 CAM0.
 *
 * Same 27 MHz carrier as the other CAM0 modules. Chip id 0xcb61.
 * The 1920x1200 table is the mode CameVision Ego streamed (0x301f=0x48,
 * 4 lanes, 270 MHz, VTS 0x0c80). This module has no EFSYNC wire, and
 * with trigger mode left off the sensor emits one frame each time
 * 0x2100 goes 0 to 1. A work item repeats that edge.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define SC233HGS_REG_CHIP_ID		0x3107
#define SC233HGS_CHIP_ID		0xcb61
#define SC233HGS_REG_STANDBY		0x2100
#define SC233HGS_FRAME_MS		50

#define SC233HGS_WIDTH			1920
#define SC233HGS_HEIGHT			1200
#define SC233HGS_XCLK_HZ		27000000

struct sc233hgs_reg {
	u16 addr;
	u8 val;
};

static const struct sc233hgs_reg sc233hgs_1920x1200[] = {
#include "regs.inc"
};

struct sc233hgs_mode {
	u32 lanes;
	u64 link_freq;
	u64 pixel_rate;
	u32 hblank;
	u32 vblank;
	const struct sc233hgs_reg *regs;
	unsigned int num_regs;
};

static const s64 sc233hgs_link_freq[] = {
	120000000, 180000000, 216000000, 270000000,
	324000000, 360000000, 396000000, 405000000,
	432000000, 450000000, 480000000, 540000000,
};

static const struct sc233hgs_mode sc233hgs_mode = {
	.lanes = 4,
	.link_freq = 270000000,
	.pixel_rate = 270000000ULL * 2 * 4 / 10,
	.hblank = 330,
	.vblank = 2000,
	.regs = sc233hgs_1920x1200,
	.num_regs = ARRAY_SIZE(sc233hgs_1920x1200),
};

struct sc233hgs {
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct clk *xclk;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	const struct sc233hgs_mode *mode;
	struct mutex lock;
	struct delayed_work kick;
	bool streaming;
};

static inline struct sc233hgs *to_sc233hgs(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sc233hgs, sd);
}

static int sc233hgs_write(struct sc233hgs *sensor, u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;

	ret = i2c_master_send(sensor->client, buf, sizeof(buf));
	if (ret == sizeof(buf))
		return 0;
	dev_err(&sensor->client->dev, "write 0x%04x failed: %d\n", reg, ret);
	return ret < 0 ? ret : -EIO;
}

static int sc233hgs_read(struct sc233hgs *sensor, u16 reg, u8 *val)
{
	struct i2c_msg msgs[2];
	u8 addr[2] = { reg >> 8, reg & 0xff };
	int ret;

	msgs[0].addr = sensor->client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = addr;
	msgs[1].addr = sensor->client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = val;

	ret = i2c_transfer(sensor->client->adapter, msgs, 2);
	if (ret == 2)
		return 0;
	return ret < 0 ? ret : -EIO;
}

static int sc233hgs_write_table(struct sc233hgs *sensor)
{
	unsigned int i;
	int ret;

	for (i = 0; i < sensor->mode->num_regs; i++) {
		ret = sc233hgs_write(sensor, sensor->mode->regs[i].addr,
				     sensor->mode->regs[i].val);
		if (ret)
			return ret;
	}
	return 0;
}

static int sc233hgs_set_exposure(struct sc233hgs *sensor, u32 exp)
{
	int ret;

	ret = sc233hgs_write(sensor, 0x3e00, (exp >> 12) & 0x0f);
	if (ret)
		return ret;
	ret = sc233hgs_write(sensor, 0x3e01, (exp >> 4) & 0xff);
	if (ret)
		return ret;
	return sc233hgs_write(sensor, 0x3e02, (exp & 0x0f) << 4);
}

static int sc233hgs_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc233hgs *sensor = container_of(ctrl->handler, struct sc233hgs,
					       ctrl_handler);

	if (!sensor->streaming && ctrl->id == V4L2_CID_EXPOSURE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		return sc233hgs_set_exposure(sensor, ctrl->val);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops sc233hgs_ctrl_ops = {
	.s_ctrl = sc233hgs_s_ctrl,
};

static int sc233hgs_power(struct sc233hgs *sensor, bool on)
{
	int ret;

	if (!on) {
		gpiod_set_value_cansleep(sensor->pwdn_gpio, 0);
		gpiod_set_value_cansleep(sensor->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
		clk_disable_unprepare(sensor->xclk);
		return 0;
	}

	ret = clk_set_rate(sensor->xclk, SC233HGS_XCLK_HZ);
	if (ret)
		dev_warn(&sensor->client->dev,
			 "xclk set_rate(%u) failed: %d, rate is %lu\n",
			 SC233HGS_XCLK_HZ, ret, clk_get_rate(sensor->xclk));

	ret = clk_prepare_enable(sensor->xclk);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(sensor->reset_gpio, 0);
	ret = regulator_bulk_enable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
	if (ret) {
		clk_disable_unprepare(sensor->xclk);
		return ret;
	}
	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
	fsleep(1000);
	gpiod_set_value_cansleep(sensor->pwdn_gpio, 1);
	fsleep(16000);
	return 0;
}

static int sc233hgs_identify(struct sc233hgs *sensor)
{
	u8 hi, lo;
	u16 id;
	int ret;

	ret = sc233hgs_read(sensor, SC233HGS_REG_CHIP_ID, &hi);
	if (ret)
		return ret;
	ret = sc233hgs_read(sensor, SC233HGS_REG_CHIP_ID + 1, &lo);
	if (ret)
		return ret;
	id = ((u16)hi << 8) | lo;
	if (id != SC233HGS_CHIP_ID) {
		dev_err(&sensor->client->dev,
			"chip id 0x%04x is not SC233HGS (0x%04x) at i2c 0x%02x\n",
			id, SC233HGS_CHIP_ID, sensor->client->addr);
		return -ENODEV;
	}
	dev_info(&sensor->client->dev,
		 "SC233HGS id 0x%04x, %u lanes, link %llu Hz, xclk %lu\n",
		 id, sensor->mode->lanes, sensor->mode->link_freq,
		 clk_get_rate(sensor->xclk));
	return 0;
}

static void sc233hgs_kick(struct work_struct *work)
{
	struct sc233hgs *sensor = container_of(to_delayed_work(work),
					       struct sc233hgs, kick);

	mutex_lock(&sensor->lock);
	if (!sensor->streaming) {
		mutex_unlock(&sensor->lock);
		return;
	}
	sc233hgs_write(sensor, SC233HGS_REG_STANDBY, 0x00);
	mutex_unlock(&sensor->lock);
	/* A back-to-back write is too short. The sensor only emits a frame
	 * when standby has been low for about a millisecond.
	 */
	fsleep(2000);
	mutex_lock(&sensor->lock);
	if (sensor->streaming)
		sc233hgs_write(sensor, SC233HGS_REG_STANDBY, 0x01);
	mutex_unlock(&sensor->lock);
	if (READ_ONCE(sensor->streaming))
		schedule_delayed_work(&sensor->kick,
				       msecs_to_jiffies(SC233HGS_FRAME_MS));
}

static int sc233hgs_start(struct sc233hgs *sensor)
{
	int ret;

	ret = sc233hgs_write_table(sensor);
	if (ret)
		return ret;
	fsleep(8 * 1000);
	/* Table exposure is 64 lines. Open it up and add a stop of gain. */
	ret = sc233hgs_write(sensor, 0x3e00, 0x00);
	if (ret)
		return ret;
	ret = sc233hgs_write(sensor, 0x3e01, 0xc0);
	if (ret)
		return ret;
	ret = sc233hgs_write(sensor, 0x3e02, 0x00);
	if (ret)
		return ret;
	ret = sc233hgs_write(sensor, 0x3e09, 0x80);
	if (ret)
		return ret;
	return sc233hgs_write(sensor, SC233HGS_REG_STANDBY, 0x01);
}

static int sc233hgs_stop(struct sc233hgs *sensor)
{
	return sc233hgs_write(sensor, SC233HGS_REG_STANDBY, 0x00);
}

static int sc233hgs_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sc233hgs *sensor = to_sc233hgs(sd);
	int ret;

	mutex_lock(&sensor->lock);
	if (enable == sensor->streaming) {
		ret = 0;
		goto out;
	}
	if (enable) {
		ret = sc233hgs_start(sensor);
		if (!ret) {
			sensor->streaming = true;
			schedule_delayed_work(&sensor->kick,
					       msecs_to_jiffies(SC233HGS_FRAME_MS));
		}
	} else {
		sensor->streaming = false;
		mutex_unlock(&sensor->lock);
		cancel_delayed_work_sync(&sensor->kick);
		cancel_delayed_work_sync(&sensor->kick);
		mutex_lock(&sensor->lock);
		ret = sc233hgs_stop(sensor);
	}
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static const struct v4l2_mbus_framefmt sc233hgs_fmt = {
	.width = SC233HGS_WIDTH,
	.height = SC233HGS_HEIGHT,
	.code = MEDIA_BUS_FMT_SBGGR10_1X10,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_NONE,
};

static int sc233hgs_init_state(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, 0) = sc233hgs_fmt;
	return 0;
}

static int sc233hgs_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int sc233hgs_enum_frame_size(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;
	fse->min_width = fse->max_width = SC233HGS_WIDTH;
	fse->min_height = fse->max_height = SC233HGS_HEIGHT;
	return 0;
}

static int sc233hgs_get_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_state_get_format(state, fmt->pad);
	return 0;
}

static int sc233hgs_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *fmt)
{
	*v4l2_subdev_state_get_format(state, fmt->pad) = sc233hgs_fmt;
	fmt->format = sc233hgs_fmt;
	return 0;
}

static int sc233hgs_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP_DEFAULT)
		return -EINVAL;
	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = SC233HGS_WIDTH;
	sel->r.height = SC233HGS_HEIGHT;
	return 0;
}

static const struct v4l2_subdev_video_ops sc233hgs_video_ops = {
	.s_stream = sc233hgs_s_stream,
};

static const struct v4l2_subdev_pad_ops sc233hgs_pad_ops = {
	.enum_mbus_code = sc233hgs_enum_mbus_code,
	.enum_frame_size = sc233hgs_enum_frame_size,
	.get_fmt = sc233hgs_get_fmt,
	.set_fmt = sc233hgs_set_fmt,
	.get_selection = sc233hgs_get_selection,
};

static const struct v4l2_subdev_internal_ops sc233hgs_internal_ops = {
	.init_state = sc233hgs_init_state,
};

static const struct v4l2_subdev_ops sc233hgs_subdev_ops = {
	.video = &sc233hgs_video_ops,
	.pad = &sc233hgs_pad_ops,
};

static int sc233hgs_init_controls(struct sc233hgs *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	const struct sc233hgs_mode *mode = sensor->mode;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 6);
	if (ret)
		return ret;

	v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ,
			       ARRAY_SIZE(sc233hgs_link_freq) - 1, 3,
			       sc233hgs_link_freq);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE,
			  mode->pixel_rate, mode->pixel_rate, 1, mode->pixel_rate);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_HBLANK,
			  mode->hblank, mode->hblank, 1, mode->hblank);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_VBLANK,
			  mode->vblank, mode->vblank, 1, mode->vblank);
	v4l2_ctrl_new_std(hdl, &sc233hgs_ctrl_ops, V4L2_CID_EXPOSURE,
			  3, 3192, 1, 1024);

	if (hdl->error)
		return hdl->error;

	sensor->sd.ctrl_handler = hdl;
	return 0;
}

static int sc233hgs_parse_endpoint(struct sc233hgs *sensor)
{
	struct device *dev = &sensor->client->dev;
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	unsigned int lanes;
	u64 link = 0;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint)
		return dev_err_probe(dev, -EINVAL, "missing CSI-2 endpoint\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep);
	fwnode_handle_put(endpoint);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse endpoint\n");

	lanes = ep.bus.mipi_csi2.num_data_lanes;
	if (ep.nr_of_link_frequencies > 0)
		link = ep.link_frequencies[0];
	v4l2_fwnode_endpoint_free(&ep);

	if (lanes != sensor->mode->lanes)
		return dev_err_probe(dev, -EINVAL,
				     "SC233HGS overlay has %u lanes, table is %u\n",
				     lanes, sensor->mode->lanes);
	if (link && link != sensor->mode->link_freq)
		dev_info(dev, "overlay link %llu Hz, control menu starts at %llu Hz\n",
			 link, sensor->mode->link_freq);
	return 0;
}

static int sc233hgs_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sc233hgs *sensor;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;
	sensor->client = client;
	sensor->mode = &sc233hgs_mode;
	mutex_init(&sensor->lock);
	INIT_DELAYED_WORK(&sensor->kick, sc233hgs_kick);

	sensor->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(sensor->xclk))
		return dev_err_probe(dev, PTR_ERR(sensor->xclk), "xclk\n");

	sensor->supplies[0].supply = "avdd";
	sensor->supplies[1].supply = "dovdd";
	sensor->supplies[2].supply = "dvdd";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(sensor->supplies),
				      sensor->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "regulators\n");

	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(sensor->reset_gpio), "reset gpio\n");
	sensor->pwdn_gpio = devm_gpiod_get_optional(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->pwdn_gpio))
		return dev_err_probe(dev, PTR_ERR(sensor->pwdn_gpio), "pwdn gpio\n");

	ret = sc233hgs_parse_endpoint(sensor);
	if (ret)
		return ret;

	v4l2_i2c_subdev_init(&sensor->sd, client, &sc233hgs_subdev_ops);
	sensor->sd.internal_ops = &sc233hgs_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = sc233hgs_init_controls(sensor);
	if (ret)
		goto err_entity;

	sensor->sd.state_lock = &sensor->lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_ctrls;

	ret = sc233hgs_power(sensor, true);
	if (ret)
		goto err_subdev;
	ret = sc233hgs_identify(sensor);
	if (ret)
		goto err_power;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_power;

	return 0;

err_power:
	sc233hgs_power(sensor, false);
err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
	return ret;
}

static void sc233hgs_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc233hgs *sensor = to_sc233hgs(sd);

	sensor->streaming = false;
	cancel_delayed_work_sync(&sensor->kick);
	v4l2_async_unregister_subdev(sd);
	sc233hgs_power(sensor, false);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id sc233hgs_of_match[] = {
	{ .compatible = "smartsens,sc233hgs" },
	{ }
};
MODULE_DEVICE_TABLE(of, sc233hgs_of_match);

static const struct i2c_device_id sc233hgs_id[] = {
	{ "sc233hgs" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc233hgs_id);

static struct i2c_driver sc233hgs_i2c_driver = {
	.driver = {
		.name = "sc233hgs",
		.of_match_table = sc233hgs_of_match,
	},
	.probe = sc233hgs_probe,
	.remove = sc233hgs_remove,
	.id_table = sc233hgs_id,
};
module_i2c_driver(sc233hgs_i2c_driver);

MODULE_DESCRIPTION("SmartSens SC233HGS CSI-2 sensor driver");
MODULE_LICENSE("GPL");
