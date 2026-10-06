// SPDX-License-Identifier: GPL-2.0
/*
 * Sony IMX675 CSI-2 sensor driver for Raspberry Pi 5 CAM0.
 *
 * Same 27 MHz carrier as the other CAM0 modules. I2C 0x1a.
 * The table is the 2608x1960 RAW10 list. 0x3014=0x03 selects a 27 MHz
 * INCK, 0x3040=0x01 is 2-lane, 0x3023=0x00 is RAW10 GRBG. Stream on is
 * 0x3000=0 then 0x3002=0 after the table has settled.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define IMX675_REG_STANDBY		0x3000
#define IMX675_REG_XMSTA		0x3002
#define IMX675_REG_MODULE		0x3a00
#define IMX675_MODULE_ID		0x96

#define IMX675_WIDTH			2608
#define IMX675_HEIGHT			1960
#define IMX675_XCLK_HZ			27000000

struct imx675_reg {
	u16 addr;
	u8 val;
};

static const struct imx675_reg imx675_2608x1960[] = {
#include "regs.inc"
};

struct imx675_mode {
	u32 lanes;
	u64 link_freq;
	u64 pixel_rate;
	u32 hblank;
	u32 vblank;
	const struct imx675_reg *regs;
	unsigned int num_regs;
};

static const s64 imx675_link_freq[] = {
	445000000, 594000000, 720000000, 800000000,
	891000000, 1188000000,
};

static const struct imx675_mode imx675_mode = {
	.lanes = 2,
	.link_freq = 800000000,
	.pixel_rate = 800000000ULL * 2 * 2 / 10,
	.hblank = 1100,
	.vblank = 290,
	.regs = imx675_2608x1960,
	.num_regs = ARRAY_SIZE(imx675_2608x1960),
};

struct imx675 {
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct clk *xclk;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	const struct imx675_mode *mode;
	struct mutex lock;
	bool streaming;
};

static inline struct imx675 *to_imx675(struct v4l2_subdev *sd)
{
	return container_of(sd, struct imx675, sd);
}

static int imx675_write(struct imx675 *sensor, u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;

	ret = i2c_master_send(sensor->client, buf, sizeof(buf));
	if (ret == sizeof(buf))
		return 0;
	dev_err(&sensor->client->dev, "write 0x%04x failed: %d\n", reg, ret);
	return ret < 0 ? ret : -EIO;
}

static int imx675_read(struct imx675 *sensor, u16 reg, u8 *val)
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

static int imx675_write_table(struct imx675 *sensor)
{
	unsigned int i;
	int ret;

	for (i = 0; i < sensor->mode->num_regs; i++) {
		ret = imx675_write(sensor, sensor->mode->regs[i].addr,
				   sensor->mode->regs[i].val);
		if (ret)
			return ret;
	}
	return 0;
}

static int imx675_identify(struct imx675 *sensor)
{
	u8 id;
	int ret;

	ret = imx675_read(sensor, IMX675_REG_MODULE, &id);
	if (ret)
		return ret;
	if (id != IMX675_MODULE_ID) {
		dev_err(&sensor->client->dev,
			"module id 0x%02x at 0x%04x is not IMX675 (0x%02x)\n",
			id, IMX675_REG_MODULE, IMX675_MODULE_ID);
		return -ENODEV;
	}
	dev_info(&sensor->client->dev,
		 "IMX675 id 0x%02x, %u lanes, link %llu Hz, xclk %lu\n",
		 id, sensor->mode->lanes, sensor->mode->link_freq,
		 clk_get_rate(sensor->xclk));
	return 0;
}

static int imx675_start(struct imx675 *sensor)
{
	int ret;

	ret = imx675_write_table(sensor);
	if (ret)
		return ret;
	fsleep(10 * 1000);
	ret = imx675_write(sensor, IMX675_REG_STANDBY, 0x00);
	if (ret)
		return ret;
	fsleep(2 * 1000);
	return imx675_write(sensor, IMX675_REG_XMSTA, 0x00);
}

static int imx675_stop(struct imx675 *sensor)
{
	int ret;

	ret = imx675_write(sensor, IMX675_REG_STANDBY, 0x01);
	if (ret)
		return ret;
	return imx675_write(sensor, IMX675_REG_XMSTA, 0x01);
}

static int imx675_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx675 *sensor = to_imx675(sd);
	int ret;

	mutex_lock(&sensor->lock);
	if (enable == sensor->streaming) {
		ret = 0;
		goto out;
	}
	if (enable)
		ret = imx675_start(sensor);
	else
		ret = imx675_stop(sensor);
	if (!ret)
		sensor->streaming = enable;
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static const struct v4l2_mbus_framefmt imx675_fmt = {
	.width = IMX675_WIDTH,
	.height = IMX675_HEIGHT,
	.code = MEDIA_BUS_FMT_SGRBG10_1X10,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_NONE,
};

static int imx675_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, 0) = imx675_fmt;
	return 0;
}

static int imx675_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	return 0;
}

static int imx675_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;
	fse->min_width = fse->max_width = IMX675_WIDTH;
	fse->min_height = fse->max_height = IMX675_HEIGHT;
	return 0;
}

static int imx675_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_state_get_format(state, fmt->pad);
	return 0;
}

static int imx675_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	*v4l2_subdev_state_get_format(state, fmt->pad) = imx675_fmt;
	fmt->format = imx675_fmt;
	return 0;
}

static int imx675_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP_DEFAULT)
		return -EINVAL;
	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = IMX675_WIDTH;
	sel->r.height = IMX675_HEIGHT;
	return 0;
}

static const struct v4l2_subdev_video_ops imx675_video_ops = {
	.s_stream = imx675_s_stream,
};

static const struct v4l2_subdev_pad_ops imx675_pad_ops = {
	.enum_mbus_code = imx675_enum_mbus_code,
	.enum_frame_size = imx675_enum_frame_size,
	.get_fmt = imx675_get_fmt,
	.set_fmt = imx675_set_fmt,
	.get_selection = imx675_get_selection,
};

static const struct v4l2_subdev_internal_ops imx675_internal_ops = {
	.init_state = imx675_init_state,
};

static const struct v4l2_subdev_ops imx675_subdev_ops = {
	.video = &imx675_video_ops,
	.pad = &imx675_pad_ops,
};

static int imx675_init_controls(struct imx675 *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	const struct imx675_mode *mode = sensor->mode;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 4);
	if (ret)
		return ret;

	v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ,
			       ARRAY_SIZE(imx675_link_freq) - 1, 3,
			       imx675_link_freq);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE,
			  mode->pixel_rate, mode->pixel_rate, 1, mode->pixel_rate);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_HBLANK,
			  mode->hblank, mode->hblank, 1, mode->hblank);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_VBLANK,
			  mode->vblank, mode->vblank, 1, mode->vblank);

	if (hdl->error)
		return hdl->error;

	sensor->sd.ctrl_handler = hdl;
	return 0;
}

static int imx675_parse_endpoint(struct imx675 *sensor)
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
				     "IMX675 overlay has %u lanes, table is %u\n",
				     lanes, sensor->mode->lanes);
	if (link && link != sensor->mode->link_freq)
		dev_info(dev, "overlay link %llu Hz, control menu starts at %llu Hz\n",
			 link, sensor->mode->link_freq);
	return 0;
}

static int imx675_power(struct imx675 *sensor, bool on)
{
	int ret;

	if (!on) {
		gpiod_set_value_cansleep(sensor->pwdn_gpio, 0);
		gpiod_set_value_cansleep(sensor->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
		clk_disable_unprepare(sensor->xclk);
		return 0;
	}

	ret = clk_set_rate(sensor->xclk, IMX675_XCLK_HZ);
	if (ret)
		dev_warn(&sensor->client->dev,
			 "xclk set_rate(%u) failed: %d, rate is %lu\n",
			 IMX675_XCLK_HZ, ret, clk_get_rate(sensor->xclk));

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
	fsleep(20000);
	return 0;
}

static int imx675_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx675 *sensor;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;
	sensor->client = client;
	sensor->mode = &imx675_mode;
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

	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(sensor->reset_gpio), "reset gpio\n");
	sensor->pwdn_gpio = devm_gpiod_get_optional(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->pwdn_gpio))
		return dev_err_probe(dev, PTR_ERR(sensor->pwdn_gpio), "pwdn gpio\n");

	ret = imx675_parse_endpoint(sensor);
	if (ret)
		return ret;

	v4l2_i2c_subdev_init(&sensor->sd, client, &imx675_subdev_ops);
	sensor->sd.internal_ops = &imx675_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = imx675_init_controls(sensor);
	if (ret)
		goto err_entity;

	sensor->sd.state_lock = &sensor->lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_ctrls;

	ret = imx675_power(sensor, true);
	if (ret)
		goto err_subdev;
	ret = imx675_identify(sensor);
	if (ret)
		goto err_power;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_power;

	return 0;

err_power:
	imx675_power(sensor, false);
err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
	return ret;
}

static void imx675_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx675 *sensor = to_imx675(sd);

	v4l2_async_unregister_subdev(sd);
	imx675_power(sensor, false);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id imx675_of_match[] = {
	{ .compatible = "sony,imx675" },
	{ }
};
MODULE_DEVICE_TABLE(of, imx675_of_match);

static const struct i2c_device_id imx675_id[] = {
	{ "imx675" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, imx675_id);

static struct i2c_driver imx675_i2c_driver = {
	.driver = {
		.name = "imx675",
		.of_match_table = imx675_of_match,
	},
	.probe = imx675_probe,
	.remove = imx675_remove,
	.id_table = imx675_id,
};
module_i2c_driver(imx675_i2c_driver);

MODULE_DESCRIPTION("Sony IMX675 CSI-2 sensor driver");
MODULE_LICENSE("GPL");
