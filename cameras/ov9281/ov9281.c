// SPDX-License-Identifier: GPL-2.0
/*
 * OmniVision OV9281 CSI-2 sensor driver for Raspberry Pi 5.
 *
 * The register list is the 1280x800 linear table from the Rockchip BSP
 * driver. That file comments it as a 24 MHz clock, 120 fps, 800 Mbps per
 * lane, so the link frequency of the table is 400 MHz. 0x3662=0x05 is
 * RAW10. This module's crystal is 27 MHz, so the same PLL ratios run at
 * 400 * 27/24 = 450 MHz. The Pi kernel's ov9282 driver wants 24 MHz and
 * is not the one that should bind.
 */

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

#define OV9281_REG_CHIP_ID		0x300a
#define OV9281_CHIP_ID			0x9281
#define OV9281_REG_MODE			0x0100
#define OV9281_MODE_STANDBY		0x00
#define OV9281_MODE_STREAMING		0x01

#define OV9281_WIDTH			1280
#define OV9281_HEIGHT			800
#define OV9281_XCLK_HZ			27000000
#define OV9281_LANES			2
#define OV9281_LINK_HZ			450000000ULL
#define OV9281_HTS			0x0b60
#define OV9281_VTS			0x038e

struct ov9281_reg {
	u16 addr;
	u8 val;
};

/* 0x0103 is written by start(), with a delay, and is not in this list. */
static const struct ov9281_reg ov9281_1280x800[] = {
	{ 0x0302, 0x32 },
	{ 0x030d, 0x50 },
	{ 0x030e, 0x02 },
	{ 0x3001, 0x00 },
	{ 0x3004, 0x00 },
	{ 0x3005, 0x00 },
	{ 0x3006, 0x04 },
	{ 0x3011, 0x0a },
	{ 0x3013, 0x18 },
	{ 0x3022, 0x01 },
	{ 0x3023, 0x00 },
	{ 0x302c, 0x00 },
	{ 0x302f, 0x00 },
	{ 0x3030, 0x04 },
	{ 0x3039, 0x32 },
	{ 0x303a, 0x00 },
	{ 0x303f, 0x01 },
	{ 0x3500, 0x00 },
	{ 0x3501, 0x2a },
	{ 0x3502, 0x90 },
	{ 0x3503, 0x08 },
	{ 0x3505, 0x8c },
	{ 0x3507, 0x03 },
	{ 0x3508, 0x00 },
	{ 0x3509, 0x10 },
	{ 0x3610, 0x80 },
	{ 0x3611, 0xa0 },
	{ 0x3620, 0x6f },
	{ 0x3632, 0x56 },
	{ 0x3633, 0x78 },
	{ 0x3662, 0x05 },
	{ 0x3666, 0x00 },
	{ 0x366f, 0x5a },
	{ 0x3680, 0x84 },
	{ 0x3712, 0x80 },
	{ 0x372d, 0x22 },
	{ 0x3731, 0x80 },
	{ 0x3732, 0x30 },
	{ 0x3778, 0x00 },
	{ 0x377d, 0x22 },
	{ 0x3788, 0x02 },
	{ 0x3789, 0xa4 },
	{ 0x378a, 0x00 },
	{ 0x378b, 0x4a },
	{ 0x3799, 0x20 },
	{ 0x3800, 0x00 },
	{ 0x3801, 0x00 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x00 },
	{ 0x3804, 0x05 },
	{ 0x3805, 0x0f },
	{ 0x3806, 0x03 },
	{ 0x3807, 0x2f },
	{ 0x3808, 0x05 },
	{ 0x3809, 0x00 },
	{ 0x380a, 0x03 },
	{ 0x380b, 0x20 },
	{ 0x380c, 0x02 },
	{ 0x380d, 0xd8 },
	{ 0x380e, 0x03 },
	{ 0x380f, 0x8e },
	{ 0x3810, 0x00 },
	{ 0x3811, 0x08 },
	{ 0x3812, 0x00 },
	{ 0x3813, 0x08 },
	{ 0x3814, 0x11 },
	{ 0x3815, 0x11 },
	{ 0x3820, 0x40 },
	{ 0x3821, 0x00 },
	{ 0x3881, 0x42 },
	{ 0x38b1, 0x00 },
	{ 0x3920, 0xff },
	{ 0x4003, 0x40 },
	{ 0x4008, 0x04 },
	{ 0x4009, 0x0b },
	{ 0x400c, 0x00 },
	{ 0x400d, 0x07 },
	{ 0x4010, 0x40 },
	{ 0x4043, 0x40 },
	{ 0x4307, 0x30 },
	{ 0x4317, 0x00 },
	{ 0x4501, 0x00 },
	{ 0x4507, 0x00 },
	{ 0x4509, 0x00 },
	{ 0x450a, 0x08 },
	{ 0x4601, 0x04 },
	{ 0x470f, 0x00 },
	{ 0x4f07, 0x00 },
	{ 0x4800, 0x00 },
	{ 0x5000, 0x9f },
	{ 0x5001, 0x00 },
	{ 0x5e00, 0x00 },
	{ 0x5d00, 0x07 },
	{ 0x5d01, 0x00 },
};

struct ov9281 {
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct clk *xclk;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
	struct mutex lock;
	bool streaming;
};

static const s64 ov9281_link_freq[] = { OV9281_LINK_HZ };

static const struct v4l2_mbus_framefmt ov9281_fmt = {
	.width = OV9281_WIDTH,
	.height = OV9281_HEIGHT,
	.code = MEDIA_BUS_FMT_Y10_1X10,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_NONE,
};

static inline struct ov9281 *to_ov9281(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov9281, sd);
}

static int ov9281_write(struct ov9281 *sensor, u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;

	ret = i2c_master_send(sensor->client, buf, sizeof(buf));
	if (ret == sizeof(buf))
		return 0;
	dev_err(&sensor->client->dev, "write 0x%04x failed: %d\n", reg, ret);
	return ret < 0 ? ret : -EIO;
}

static int ov9281_read(struct ov9281 *sensor, u16 reg, u8 *val)
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

static int ov9281_power(struct ov9281 *sensor, bool on)
{
	int ret;

	if (!on) {
		gpiod_set_value_cansleep(sensor->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(sensor->supplies),
				       sensor->supplies);
		clk_disable_unprepare(sensor->xclk);
		return 0;
	}

	ret = clk_set_rate(sensor->xclk, OV9281_XCLK_HZ);
	if (ret)
		dev_warn(&sensor->client->dev,
			 "xclk set_rate(%u) failed: %d, rate is %lu\n",
			 OV9281_XCLK_HZ, ret, clk_get_rate(sensor->xclk));

	ret = clk_prepare_enable(sensor->xclk);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(sensor->reset_gpio, 0);
	ret = regulator_bulk_enable(ARRAY_SIZE(sensor->supplies),
				    sensor->supplies);
	if (ret) {
		clk_disable_unprepare(sensor->xclk);
		return ret;
	}
	gpiod_set_value_cansleep(sensor->reset_gpio, 1);
	fsleep(10000);
	return 0;
}

static int ov9281_identify(struct ov9281 *sensor)
{
	u8 hi, lo;
	u16 id;
	int ret;

	ret = ov9281_read(sensor, OV9281_REG_CHIP_ID, &hi);
	if (ret)
		return ret;
	ret = ov9281_read(sensor, OV9281_REG_CHIP_ID + 1, &lo);
	if (ret)
		return ret;
	id = ((u16)hi << 8) | lo;
	if (id != OV9281_CHIP_ID) {
		dev_err(&sensor->client->dev,
			"chip id 0x%04x is not OV9281 (0x%04x) at i2c 0x%02x\n",
			id, OV9281_CHIP_ID, sensor->client->addr);
		return -ENODEV;
	}
	dev_info(&sensor->client->dev,
		 "OV9281 id 0x%04x, %u lanes, link %llu Hz, xclk %u\n",
		 id, OV9281_LANES, OV9281_LINK_HZ, OV9281_XCLK_HZ);
	return 0;
}

static int ov9281_start(struct ov9281 *sensor)
{
	unsigned int i;
	int ret;

	ret = ov9281_write(sensor, OV9281_REG_MODE, OV9281_MODE_STANDBY);
	if (ret)
		return ret;
	ret = ov9281_write(sensor, 0x0103, 0x01);
	if (ret)
		return ret;
	fsleep(10000);
	for (i = 0; i < ARRAY_SIZE(ov9281_1280x800); i++) {
		ret = ov9281_write(sensor, ov9281_1280x800[i].addr,
				   ov9281_1280x800[i].val);
		if (ret)
			return ret;
	}
	fsleep(10000);
	return ov9281_write(sensor, OV9281_REG_MODE, OV9281_MODE_STREAMING);
}

static int ov9281_stop(struct ov9281 *sensor)
{
	return ov9281_write(sensor, OV9281_REG_MODE, OV9281_MODE_STANDBY);
}

static int ov9281_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov9281 *sensor = to_ov9281(sd);
	int ret;

	mutex_lock(&sensor->lock);
	if (enable == sensor->streaming) {
		ret = 0;
		goto out;
	}
	if (enable)
		ret = ov9281_start(sensor);
	else
		ret = ov9281_stop(sensor);
	if (!ret)
		sensor->streaming = enable;
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int ov9281_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, 0) = ov9281_fmt;
	return 0;
}

static int ov9281_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_Y10_1X10;
	return 0;
}

static int ov9281_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MEDIA_BUS_FMT_Y10_1X10)
		return -EINVAL;
	fse->min_width = fse->max_width = OV9281_WIDTH;
	fse->min_height = fse->max_height = OV9281_HEIGHT;
	return 0;
}

static int ov9281_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_state_get_format(state, fmt->pad);
	return 0;
}

static int ov9281_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	*v4l2_subdev_state_get_format(state, fmt->pad) = ov9281_fmt;
	fmt->format = ov9281_fmt;
	return 0;
}

static int ov9281_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP_DEFAULT)
		return -EINVAL;
	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = OV9281_WIDTH;
	sel->r.height = OV9281_HEIGHT;
	return 0;
}

static const struct v4l2_subdev_video_ops ov9281_video_ops = {
	.s_stream = ov9281_s_stream,
};

static const struct v4l2_subdev_pad_ops ov9281_pad_ops = {
	.enum_mbus_code = ov9281_enum_mbus_code,
	.enum_frame_size = ov9281_enum_frame_size,
	.get_fmt = ov9281_get_fmt,
	.set_fmt = ov9281_set_fmt,
	.get_selection = ov9281_get_selection,
};

static const struct v4l2_subdev_internal_ops ov9281_internal_ops = {
	.init_state = ov9281_init_state,
};

static const struct v4l2_subdev_ops ov9281_subdev_ops = {
	.video = &ov9281_video_ops,
	.pad = &ov9281_pad_ops,
};

static int ov9281_init_controls(struct ov9281 *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	const u64 pixel_rate = OV9281_LINK_HZ * 2 * OV9281_LANES / 10;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 4);
	if (ret)
		return ret;

	v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ,
			       0, 0, ov9281_link_freq);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE,
			  pixel_rate, pixel_rate, 1, pixel_rate);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_HBLANK,
			  OV9281_HTS - OV9281_WIDTH, OV9281_HTS - OV9281_WIDTH,
			  1, OV9281_HTS - OV9281_WIDTH);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_VBLANK,
			  OV9281_VTS - OV9281_HEIGHT, OV9281_VTS - OV9281_HEIGHT,
			  1, OV9281_VTS - OV9281_HEIGHT);

	if (hdl->error)
		return hdl->error;

	sensor->sd.ctrl_handler = hdl;
	return 0;
}

static int ov9281_parse_endpoint(struct ov9281 *sensor)
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

	if (lanes != OV9281_LANES)
		return dev_err_probe(dev, -EINVAL,
				     "OV9281 table is %u lanes, overlay has %u\n",
				     OV9281_LANES, lanes);
	if (link && link != OV9281_LINK_HZ)
		return dev_err_probe(dev, -EINVAL,
				     "link frequency %llu does not match %llu\n",
				     link, OV9281_LINK_HZ);
	return 0;
}

static int ov9281_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov9281 *sensor;
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

	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(sensor->reset_gpio),
				     "reset gpio\n");

	ret = ov9281_parse_endpoint(sensor);
	if (ret)
		return ret;

	v4l2_i2c_subdev_init(&sensor->sd, client, &ov9281_subdev_ops);
	sensor->sd.internal_ops = &ov9281_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = ov9281_init_controls(sensor);
	if (ret)
		goto err_entity;

	sensor->sd.state_lock = &sensor->lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_ctrls;

	ret = ov9281_power(sensor, true);
	if (ret)
		goto err_subdev;
	ret = ov9281_identify(sensor);
	if (ret)
		goto err_power;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_power;

	return 0;

err_power:
	ov9281_power(sensor, false);
err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
	mutex_destroy(&sensor->lock);
	return ret;
}

static void ov9281_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9281 *sensor = to_ov9281(sd);

	v4l2_async_unregister_subdev(sd);
	ov9281_power(sensor, false);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id ov9281_of_match[] = {
	{ .compatible = "ovti,ov9281" },
	{ }
};
MODULE_DEVICE_TABLE(of, ov9281_of_match);

static const struct i2c_device_id ov9281_id[] = {
	{ "ov9281" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov9281_id);

static struct i2c_driver ov9281_i2c_driver = {
	.driver = {
		.name = "ov9281",
		.of_match_table = ov9281_of_match,
	},
	.probe = ov9281_probe,
	.remove = ov9281_remove,
	.id_table = ov9281_id,
};
module_i2c_driver(ov9281_i2c_driver);

MODULE_DESCRIPTION("OmniVision OV9281 CSI-2 sensor driver");
MODULE_LICENSE("GPL");
