// SPDX-License-Identifier: GPL-2.0
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define SC835HAI_REG_CHIP_ID		0x3107
#define SC835HAI_CHIP_ID		0xc170
#define SC835HAI_REG_RESET		0x0103
#define SC835HAI_REG_MODE		0x0100
#define SC835HAI_MODE_STANDBY		0x00
#define SC835HAI_MODE_STREAMING		0x01

#define SC835HAI_WIDTH			3840
#define SC835HAI_HEIGHT			2160
#define SC835HAI_XCLK_HZ		24000000
#define SC835HAI_LANES			2
#define SC835HAI_LINK_HZ		396000000ULL
#define SC835HAI_LINE_LENGTH		0x1068
#define SC835HAI_FRAME_LENGTH		0x08ca

struct sc835hai_reg {
	u16 addr;
	u8 val;
};

static const struct sc835hai_reg sc835hai_3840x2160[] = {
#include "regs.inc"
};

static const struct v4l2_mbus_framefmt sc835hai_fmt = {
	.width = SC835HAI_WIDTH,
	.height = SC835HAI_HEIGHT,
	.code = MEDIA_BUS_FMT_SBGGR10_1X10,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_NONE,
};

static const s64 sc835hai_link_freq[] = {
	SC835HAI_LINK_HZ,
};

struct sc835hai {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct i2c_client *client;
	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[3];
	struct mutex lock;
	bool streaming;
};

static inline struct sc835hai *to_sc835hai(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sc835hai, sd);
}

static int sc835hai_write(struct sc835hai *sensor, u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;

	ret = i2c_master_send(sensor->client, buf, sizeof(buf));
	if (ret == sizeof(buf))
		return 0;
	dev_err(&sensor->client->dev, "write 0x%04x failed: %d\n", reg, ret);
	return ret < 0 ? ret : -EIO;
}

static int sc835hai_read16(struct sc835hai *sensor, u16 reg, u16 *val)
{
	struct i2c_msg msgs[2];
	u8 addr[2] = { reg >> 8, reg & 0xff };
	u8 buf[2];
	int ret;

	msgs[0].addr = sensor->client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = addr;
	msgs[1].addr = sensor->client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 2;
	msgs[1].buf = buf;

	ret = i2c_transfer(sensor->client->adapter, msgs, 2);
	if (ret != 2)
		return ret < 0 ? ret : -EIO;
	*val = ((u16)buf[0] << 8) | buf[1];
	return 0;
}

static int sc835hai_power(struct sc835hai *sensor, bool on)
{
	int ret;

	if (!on) {
		gpiod_set_value_cansleep(sensor->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(sensor->supplies),
				       sensor->supplies);
		clk_disable_unprepare(sensor->xclk);
		return 0;
	}

	ret = clk_set_rate(sensor->xclk, SC835HAI_XCLK_HZ);
	if (ret)
		dev_warn(&sensor->client->dev,
			 "xclk set_rate(%u) failed: %d, rate is %lu\n",
			 SC835HAI_XCLK_HZ, ret, clk_get_rate(sensor->xclk));

	ret = clk_prepare_enable(sensor->xclk);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
	ret = regulator_bulk_enable(ARRAY_SIZE(sensor->supplies),
				    sensor->supplies);
	if (ret) {
		clk_disable_unprepare(sensor->xclk);
		return ret;
	}
	msleep(2);
	gpiod_set_value_cansleep(sensor->reset_gpio, 0);
	msleep(20);
	return 0;
}

static int sc835hai_identify(struct sc835hai *sensor)
{
	u16 id;
	int ret;

	ret = sc835hai_read16(sensor, SC835HAI_REG_CHIP_ID, &id);
	if (ret)
		return ret;
	if (id != SC835HAI_CHIP_ID) {
		dev_err(&sensor->client->dev,
			"chip id 0x%04x is not 0x%04x at i2c 0x%02x\n",
			id, SC835HAI_CHIP_ID, sensor->client->addr);
		return -ENODEV;
	}
	dev_info(&sensor->client->dev,
		 "SC835HAI id 0x%04x, %u lanes, link %llu Hz, xclk %u\n",
		 id, SC835HAI_LANES, SC835HAI_LINK_HZ, SC835HAI_XCLK_HZ);
	return 0;
}

static int sc835hai_start(struct sc835hai *sensor)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(sc835hai_3840x2160); i++) {
		ret = sc835hai_write(sensor, sc835hai_3840x2160[i].addr,
				     sc835hai_3840x2160[i].val);
		if (ret)
			return ret;
		if (sc835hai_3840x2160[i].addr == SC835HAI_REG_RESET)
			msleep(10);
	}
	msleep(10);
	return sc835hai_write(sensor, SC835HAI_REG_MODE, SC835HAI_MODE_STREAMING);
}

static int sc835hai_stop(struct sc835hai *sensor)
{
	return sc835hai_write(sensor, SC835HAI_REG_MODE, SC835HAI_MODE_STANDBY);
}

static int sc835hai_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sc835hai *sensor = to_sc835hai(sd);
	int ret;

	mutex_lock(&sensor->lock);
	if (enable == sensor->streaming) {
		ret = 0;
		goto out;
	}
	if (enable)
		ret = sc835hai_start(sensor);
	else
		ret = sc835hai_stop(sensor);
	if (!ret)
		sensor->streaming = enable;
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int sc835hai_init_state(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, 0) = sc835hai_fmt;
	return 0;
}

static int sc835hai_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int sc835hai_enum_frame_size(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;
	fse->min_width = fse->max_width = SC835HAI_WIDTH;
	fse->min_height = fse->max_height = SC835HAI_HEIGHT;
	return 0;
}

static int sc835hai_get_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_state_get_format(state, fmt->pad);
	return 0;
}

static int sc835hai_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *fmt)
{
	*v4l2_subdev_state_get_format(state, fmt->pad) = sc835hai_fmt;
	fmt->format = sc835hai_fmt;
	return 0;
}

static int sc835hai_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP_DEFAULT)
		return -EINVAL;
	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = SC835HAI_WIDTH;
	sel->r.height = SC835HAI_HEIGHT;
	return 0;
}

static const struct v4l2_subdev_video_ops sc835hai_video_ops = {
	.s_stream = sc835hai_s_stream,
};

static const struct v4l2_subdev_pad_ops sc835hai_pad_ops = {
	.enum_mbus_code = sc835hai_enum_mbus_code,
	.enum_frame_size = sc835hai_enum_frame_size,
	.get_fmt = sc835hai_get_fmt,
	.set_fmt = sc835hai_set_fmt,
	.get_selection = sc835hai_get_selection,
};

static const struct v4l2_subdev_internal_ops sc835hai_internal_ops = {
	.init_state = sc835hai_init_state,
};

static const struct v4l2_subdev_ops sc835hai_subdev_ops = {
	.video = &sc835hai_video_ops,
	.pad = &sc835hai_pad_ops,
};

static int sc835hai_init_controls(struct sc835hai *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	const u64 pixel_rate = SC835HAI_LINK_HZ * 2 * SC835HAI_LANES / 10;
	const s32 hblank = SC835HAI_LINE_LENGTH - SC835HAI_WIDTH;
	const s32 vblank = SC835HAI_FRAME_LENGTH - SC835HAI_HEIGHT;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 4);
	if (ret)
		return ret;

	v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ,
			       0, 0, sc835hai_link_freq);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE,
			  pixel_rate, pixel_rate, 1, pixel_rate);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_HBLANK, hblank, hblank, 1, hblank);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_VBLANK, vblank, vblank, 1, vblank);

	if (hdl->error)
		return hdl->error;

	sensor->sd.ctrl_handler = hdl;
	return 0;
}

static int sc835hai_parse_endpoint(struct sc835hai *sensor)
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

	if (lanes != SC835HAI_LANES)
		return dev_err_probe(dev, -EINVAL,
				     "table is %u lanes, overlay has %u\n",
				     SC835HAI_LANES, lanes);
	if (link && link != SC835HAI_LINK_HZ)
		return dev_err_probe(dev, -EINVAL,
				     "link frequency %llu does not match %llu\n",
				     link, SC835HAI_LINK_HZ);
	return 0;
}

static int sc835hai_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sc835hai *sensor;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;
	sensor->client = client;
	mutex_init(&sensor->lock);

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

	sensor->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(sensor->reset_gpio),
				     "reset gpio\n");

	ret = sc835hai_parse_endpoint(sensor);
	if (ret)
		return ret;

	v4l2_i2c_subdev_init(&sensor->sd, client, &sc835hai_subdev_ops);
	sensor->sd.internal_ops = &sc835hai_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = sc835hai_init_controls(sensor);
	if (ret)
		goto err_entity;

	sensor->sd.state_lock = &sensor->lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_ctrls;

	ret = sc835hai_power(sensor, true);
	if (ret)
		goto err_subdev;
	ret = sc835hai_identify(sensor);
	if (ret)
		goto err_power;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_power;

	return 0;

err_power:
	sc835hai_power(sensor, false);
err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
	mutex_destroy(&sensor->lock);
	return ret;
}

static void sc835hai_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc835hai *sensor = to_sc835hai(sd);

	v4l2_async_unregister_subdev(sd);
	sc835hai_power(sensor, false);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id sc835hai_of_match[] = {
	{ .compatible = "smartsens,sc835hai" },
	{ }
};
MODULE_DEVICE_TABLE(of, sc835hai_of_match);

static const struct i2c_device_id sc835hai_id[] = {
	{ "sc835hai" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc835hai_id);

static struct i2c_driver sc835hai_i2c_driver = {
	.driver = {
		.name = "sc835hai",
		.of_match_table = sc835hai_of_match,
	},
	.probe = sc835hai_probe,
	.remove = sc835hai_remove,
	.id_table = sc835hai_id,
};
module_i2c_driver(sc835hai_i2c_driver);

MODULE_DESCRIPTION("SmartSens SC835HAI CSI-2 sensor driver");
MODULE_LICENSE("GPL");
