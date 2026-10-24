// SPDX-License-Identifier: GPL-2.0
/*
 * Color onsemi AR0234CS CSI-2 sensor driver for Raspberry Pi 5.
 *
 * 1920x1200 GRBG RAW10, 2 lanes. Registers are 16-bit address and
 * 16-bit value. Chip id 0x0a56 at 0x3000 is the color part. 0x1a56
 * is the mono part and is rejected here.
 *
 * The 10-bit PLL list is the one published for a 24 MHz clock:
 * pre-divider 8, multiplier 150, VCO 450 MHz. This module's crystal
 * is 27 MHz, so the same ratios run at 450 * 27/24 = 506.25 MHz.
 * The MIPI timing bytes stay with that table. 0x31ae=0x0202 is 2 lanes.
 * Stream on is 0x301a=0x205c. Standby is 0x301a=0x2058.
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

#define AR0234CS_REG_CHIP_ID		0x3000
#define AR0234CS_CHIP_ID		0x0a56
#define AR0234CS_CHIP_ID_MONO		0x1a56
#define AR0234CS_REG_RESET		0x301a
#define AR0234CS_RESET_ASSERT		0x0001
#define AR0234CS_MODE_STANDBY		0x2058
#define AR0234CS_MODE_STREAMING		0x205c

#define AR0234CS_WIDTH			1920
#define AR0234CS_HEIGHT			1200
#define AR0234CS_XCLK_HZ		27000000
#define AR0234CS_LANES			2
#define AR0234CS_LINK_HZ		506250000ULL
#define AR0234CS_LINE_LENGTH		0x0264
#define AR0234CS_FRAME_LENGTH		0x04c4

struct ar0234cs_reg {
	u16 addr;
	u16 val;
};

/*
 * Applied after reset, in this order. Stream on is written by start().
 * Exposure 0x3012=0x02dc is 732 lines of the 1220-line frame.
 */
static const struct ar0234cs_reg ar0234cs_1920x1200[] = {
	{ 0x302a, 0x0005 },
	{ 0x302c, 0x0001 },
	{ 0x302e, 0x0008 },
	{ 0x3030, 0x0096 },
	{ 0x3036, 0x000a },
	{ 0x3038, 0x0001 },
	{ 0x31b0, 0x0082 },
	{ 0x31b2, 0x005c },
	{ 0x31b4, 0x4248 },
	{ 0x31b6, 0x4258 },
	{ 0x31b8, 0x904b },
	{ 0x31ba, 0x030b },
	{ 0x31bc, 0x0d89 },
	{ 0x3354, 0x002b },
	{ 0x31ac, 0x0a0a },
	{ 0x31ae, 0x0202 },
	{ 0x30b0, 0x0028 },
	{ 0x306e, 0x9010 },
	{ 0x3082, 0x0003 },
	{ 0x31d0, 0x0000 },
	{ 0x3088, 0x8050 },
	{ 0x3086, 0x9237 },
	{ 0x3096, 0x0280 },
	{ 0x31e0, 0x0003 },
	{ 0x3f4c, 0x121f },
	{ 0x3f4e, 0x121f },
	{ 0x3f50, 0x0b81 },
	{ 0x3ed2, 0xfa96 },
	{ 0x3180, 0x824f },
	{ 0x3ecc, 0x0c42 },
	{ 0x3ecc, 0x0c42 },
	{ 0x30f0, 0x2283 },
	{ 0x3102, 0x5000 },
	{ 0x30b4, 0x0011 },
	{ 0x3064, 0x1982 },
	{ 0x3088, 0x81ba },
	{ 0x3086, 0x3d02 },
	{ 0x30ba, 0x7626 },
	{ 0x3002, 0x0008 },
	{ 0x3004, 0x0008 },
	{ 0x3006, 0x04b7 },
	{ 0x3008, 0x0787 },
	{ 0x30a2, 0x0001 },
	{ 0x30a6, 0x0001 },
	{ 0x3040, 0x0000 },
	{ 0x300a, AR0234CS_FRAME_LENGTH },
	{ 0x300c, AR0234CS_LINE_LENGTH },
	{ 0x3012, 0x02dc },
	{ 0x305e, 0x0080 },
	{ 0x3060, 0x000e },
	{ 0x3070, 0x0000 },
};

static const struct v4l2_mbus_framefmt ar0234cs_fmt = {
	.width = AR0234CS_WIDTH,
	.height = AR0234CS_HEIGHT,
	.code = MEDIA_BUS_FMT_SGRBG10_1X10,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_NONE,
};

static const s64 ar0234cs_link_freq[] = {
	AR0234CS_LINK_HZ,
};

struct ar0234cs {
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

static inline struct ar0234cs *to_ar0234cs(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ar0234cs, sd);
}

static int ar0234cs_write16(struct ar0234cs *sensor, u16 reg, u16 val)
{
	u8 buf[4] = { reg >> 8, reg & 0xff, val >> 8, val & 0xff };
	int ret;

	ret = i2c_master_send(sensor->client, buf, sizeof(buf));
	if (ret == sizeof(buf))
		return 0;
	dev_err(&sensor->client->dev, "write 0x%04x failed: %d\n", reg, ret);
	return ret < 0 ? ret : -EIO;
}

static int ar0234cs_read16(struct ar0234cs *sensor, u16 reg, u16 *val)
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

static int ar0234cs_power(struct ar0234cs *sensor, bool on)
{
	int ret;

	if (!on) {
		gpiod_set_value_cansleep(sensor->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(sensor->supplies),
				       sensor->supplies);
		clk_disable_unprepare(sensor->xclk);
		return 0;
	}

	ret = clk_set_rate(sensor->xclk, AR0234CS_XCLK_HZ);
	if (ret)
		dev_warn(&sensor->client->dev,
			 "xclk set_rate(%u) failed: %d, rate is %lu\n",
			 AR0234CS_XCLK_HZ, ret, clk_get_rate(sensor->xclk));

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

static int ar0234cs_identify(struct ar0234cs *sensor)
{
	u16 id;
	int ret;

	ret = ar0234cs_read16(sensor, AR0234CS_REG_CHIP_ID, &id);
	if (ret)
		return ret;
	if (id == AR0234CS_CHIP_ID_MONO) {
		dev_err(&sensor->client->dev,
			"chip id 0x%04x is the mono AR0234, this driver is the color part\n",
			id);
		return -ENODEV;
	}
	if (id != AR0234CS_CHIP_ID) {
		dev_err(&sensor->client->dev,
			"chip id 0x%04x is not AR0234CS (0x%04x) at i2c 0x%02x\n",
			id, AR0234CS_CHIP_ID, sensor->client->addr);
		return -ENODEV;
	}
	dev_info(&sensor->client->dev,
		 "AR0234CS id 0x%04x, %u lanes, link %llu Hz, xclk %u\n",
		 id, AR0234CS_LANES, AR0234CS_LINK_HZ, AR0234CS_XCLK_HZ);
	return 0;
}

static int ar0234cs_start(struct ar0234cs *sensor)
{
	unsigned int i;
	int ret;

	fsleep(20000);
	ret = ar0234cs_write16(sensor, AR0234CS_REG_RESET, AR0234CS_RESET_ASSERT);
	if (ret)
		return ret;
	fsleep(200000);
	ret = ar0234cs_write16(sensor, AR0234CS_REG_RESET, AR0234CS_MODE_STANDBY);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(ar0234cs_1920x1200); i++) {
		ret = ar0234cs_write16(sensor, ar0234cs_1920x1200[i].addr,
				       ar0234cs_1920x1200[i].val);
		if (ret)
			return ret;
	}
	fsleep(50000);
	return ar0234cs_write16(sensor, AR0234CS_REG_RESET, AR0234CS_MODE_STREAMING);
}

static int ar0234cs_stop(struct ar0234cs *sensor)
{
	return ar0234cs_write16(sensor, AR0234CS_REG_RESET, AR0234CS_MODE_STANDBY);
}

static int ar0234cs_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ar0234cs *sensor = to_ar0234cs(sd);
	int ret;

	mutex_lock(&sensor->lock);
	if (enable == sensor->streaming) {
		ret = 0;
		goto out;
	}
	if (enable)
		ret = ar0234cs_start(sensor);
	else
		ret = ar0234cs_stop(sensor);
	if (!ret)
		sensor->streaming = enable;
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int ar0234cs_init_state(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, 0) = ar0234cs_fmt;
	return 0;
}

static int ar0234cs_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	return 0;
}

static int ar0234cs_enum_frame_size(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;
	fse->min_width = fse->max_width = AR0234CS_WIDTH;
	fse->min_height = fse->max_height = AR0234CS_HEIGHT;
	return 0;
}

static int ar0234cs_get_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_state_get_format(state, fmt->pad);
	return 0;
}

static int ar0234cs_set_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *state,
			    struct v4l2_subdev_format *fmt)
{
	*v4l2_subdev_state_get_format(state, fmt->pad) = ar0234cs_fmt;
	fmt->format = ar0234cs_fmt;
	return 0;
}

static int ar0234cs_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP_DEFAULT)
		return -EINVAL;
	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = AR0234CS_WIDTH;
	sel->r.height = AR0234CS_HEIGHT;
	return 0;
}

static const struct v4l2_subdev_video_ops ar0234cs_video_ops = {
	.s_stream = ar0234cs_s_stream,
};

static const struct v4l2_subdev_pad_ops ar0234cs_pad_ops = {
	.enum_mbus_code = ar0234cs_enum_mbus_code,
	.enum_frame_size = ar0234cs_enum_frame_size,
	.get_fmt = ar0234cs_get_fmt,
	.set_fmt = ar0234cs_set_fmt,
	.get_selection = ar0234cs_get_selection,
};

static const struct v4l2_subdev_internal_ops ar0234cs_internal_ops = {
	.init_state = ar0234cs_init_state,
};

static const struct v4l2_subdev_ops ar0234cs_subdev_ops = {
	.video = &ar0234cs_video_ops,
	.pad = &ar0234cs_pad_ops,
};

static int ar0234cs_init_controls(struct ar0234cs *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	const u64 pixel_rate = AR0234CS_LINK_HZ * 2 * AR0234CS_LANES / 10;
	const s32 hblank = AR0234CS_LINE_LENGTH - AR0234CS_WIDTH;
	const s32 vblank = AR0234CS_FRAME_LENGTH - AR0234CS_HEIGHT;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 4);
	if (ret)
		return ret;

	v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ,
			       0, 0, ar0234cs_link_freq);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE,
			  pixel_rate, pixel_rate, 1, pixel_rate);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_HBLANK, hblank, hblank, 1, hblank);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_VBLANK, vblank, vblank, 1, vblank);

	if (hdl->error)
		return hdl->error;

	sensor->sd.ctrl_handler = hdl;
	return 0;
}

static int ar0234cs_parse_endpoint(struct ar0234cs *sensor)
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

	if (lanes != AR0234CS_LANES)
		return dev_err_probe(dev, -EINVAL,
				     "AR0234CS table is %u lanes, overlay has %u\n",
				     AR0234CS_LANES, lanes);
	if (link && link != AR0234CS_LINK_HZ)
		return dev_err_probe(dev, -EINVAL,
				     "link frequency %llu does not match %llu\n",
				     link, AR0234CS_LINK_HZ);
	return 0;
}

static int ar0234cs_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ar0234cs *sensor;
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

	ret = ar0234cs_parse_endpoint(sensor);
	if (ret)
		return ret;

	v4l2_i2c_subdev_init(&sensor->sd, client, &ar0234cs_subdev_ops);
	sensor->sd.internal_ops = &ar0234cs_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = ar0234cs_init_controls(sensor);
	if (ret)
		goto err_entity;

	sensor->sd.state_lock = &sensor->lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_ctrls;

	ret = ar0234cs_power(sensor, true);
	if (ret)
		goto err_subdev;
	ret = ar0234cs_identify(sensor);
	if (ret)
		goto err_power;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_power;

	return 0;

err_power:
	ar0234cs_power(sensor, false);
err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
	mutex_destroy(&sensor->lock);
	return ret;
}

static void ar0234cs_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0234cs *sensor = to_ar0234cs(sd);

	v4l2_async_unregister_subdev(sd);
	ar0234cs_power(sensor, false);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id ar0234cs_of_match[] = {
	{ .compatible = "onnn,ar0234cs" },
	{ }
};
MODULE_DEVICE_TABLE(of, ar0234cs_of_match);

static const struct i2c_device_id ar0234cs_id[] = {
	{ "ar0234cs" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ar0234cs_id);

static struct i2c_driver ar0234cs_i2c_driver = {
	.driver = {
		.name = "ar0234cs",
		.of_match_table = ar0234cs_of_match,
	},
	.probe = ar0234cs_probe,
	.remove = ar0234cs_remove,
	.id_table = ar0234cs_id,
};
module_i2c_driver(ar0234cs_i2c_driver);

MODULE_DESCRIPTION("onsemi AR0234CS color CSI-2 sensor driver");
MODULE_LICENSE("GPL");
