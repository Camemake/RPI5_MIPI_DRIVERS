// SPDX-License-Identifier: GPL-2.0
/*
 * Sony IMX908 CSI-2 sensor driver for Raspberry Pi 5.
 *
 * The register list is the one that streamed on the Luckfox Aura.
 * It comes from Sony's kernel driver (Lachlan Michael, 28 Aug 2026),
 * not a captured mode dump. The clock is the module crystal.
 * 0x3014=0x03 is that 27 MHz input. 0x3040=0x01 is 2 lanes: 4-lane
 * mode on this connector fails CRC and the packets arrive as data
 * type 0x37. 0x3015=0x03 is the 720 MHz link. Chip id 0x038c is
 * little-endian at 0x4c0c, and that register does not answer until
 * standby is cancelled.
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

#define IMX908_REG_STANDBY		0x3000
#define IMX908_STANDBY_EN		0x01
#define IMX908_STANDBY_CANCEL		0x00
#define IMX908_REG_XMSTA		0x3002
#define IMX908_XMSTA_START		0x00
#define IMX908_XMSTA_STOP		0x01
#define IMX908_REG_CHIP_ID		0x4c0c
#define IMX908_CHIP_ID			0x038c

#define IMX908_WIDTH			3856
#define IMX908_HEIGHT			2176
#define IMX908_XCLK_HZ			27000000
#define IMX908_LANES			2
#define IMX908_LINK_HZ			445500000ULL
#define IMX908_VMAX			2250
#define IMX908_HMAX			1100
#define IMX908_EXPOSURE			1121

struct imx908_reg {
	u16 addr;
	u8 val;
};

static const struct imx908_reg imx908_common[] = {
	{ 0x30a6, 0x00 },
	{ 0x039c, 0x03 },
	{ 0x309c, 0x03 },
	{ 0x3416, 0x20 },
	{ 0x3417, 0x00 },
	{ 0x3456, 0xf4 },
	{ 0x345d, 0x01 },
	{ 0x3460, 0x00 },
	{ 0x3461, 0x0b },
	{ 0x3471, 0x00 },
	{ 0x3472, 0x23 },
	{ 0x347b, 0x02 },
	{ 0x3481, 0x01 },
	{ 0x380c, 0x00 },
	{ 0x380f, 0x0c },
	{ 0x381c, 0x11 },
	{ 0x3820, 0x22 },
	{ 0x3824, 0x33 },
	{ 0x3828, 0x22 },
	{ 0x382c, 0x33 },
	{ 0x3830, 0x33 },
	{ 0x3838, 0x05 },
	{ 0x383c, 0x07 },
	{ 0x3840, 0x06 },
	{ 0x3848, 0x17 },
	{ 0x384c, 0x0b },
	{ 0x3850, 0x10 },
	{ 0x3854, 0x12 },
	{ 0x385c, 0x20 },
	{ 0x38c4, 0x64 },
	{ 0x38c5, 0x64 },
	{ 0x38c6, 0x64 },
	{ 0x3c4a, 0x15 },
	{ 0x3c4c, 0x13 },
	{ 0x3c4d, 0x13 },
	{ 0x3c4e, 0x13 },
	{ 0x3c50, 0x77 },
	{ 0x3c51, 0x07 },
	{ 0x3db4, 0x00 },
	{ 0x4419, 0x06 },
	{ 0x441c, 0x00 },
	{ 0x4426, 0x00 },
	{ 0x4538, 0x20 },
	{ 0x4539, 0x19 },
	{ 0x453a, 0x19 },
	{ 0x453b, 0x19 },
	{ 0x453c, 0x19 },
	{ 0x453d, 0x19 },
	{ 0x453e, 0x19 },
	{ 0x453f, 0x19 },
	{ 0x4540, 0x19 },
	{ 0x4544, 0x11 },
	{ 0x4545, 0x11 },
	{ 0x4546, 0x11 },
	{ 0x463c, 0x20 },
	{ 0x465e, 0xcf },
	{ 0x4684, 0x20 },
	{ 0x46a6, 0xcf },
	{ 0x46e2, 0xf3 },
};

static const u16 imx908_bit_depth_regs[] = {
	0x3d78, 0x3d79, 0x3d80, 0x3d81, 0x3d88, 0x3d89, 0x3d90, 0x3d91,
};

static const struct v4l2_mbus_framefmt imx908_fmt = {
	.width = IMX908_WIDTH,
	.height = IMX908_HEIGHT,
	.code = MEDIA_BUS_FMT_SRGGB10_1X10,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_NONE,
};

static const s64 imx908_link_freq[] = {
	297000000, 360000000, 445500000, 594000000, 720000000,
};

static const u8 imx908_rate_sel[] = {
	0x07, 0x06, 0x05, 0x04, 0x03,
};

struct imx908 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct i2c_client *client;
	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[3];
	struct v4l2_ctrl *link_freq;
	struct mutex lock;
	bool streaming;
};

static inline struct imx908 *to_imx908(struct v4l2_subdev *sd)
{
	return container_of(sd, struct imx908, sd);
}

static int imx908_write(struct imx908 *sensor, u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;
	int try;

	for (try = 0; try < 8; try++) {
		ret = i2c_master_send(sensor->client, buf, sizeof(buf));
		if (ret == sizeof(buf))
			return 0;
		fsleep(10000);
	}
	dev_err(&sensor->client->dev, "write 0x%04x failed: %d\n", reg, ret);
	return ret < 0 ? ret : -EIO;
}

static int imx908_write16(struct imx908 *sensor, u16 reg, u16 val)
{
	int ret;

	ret = imx908_write(sensor, reg, val & 0xff);
	if (ret)
		return ret;
	return imx908_write(sensor, reg + 1, val >> 8);
}

static int imx908_write24(struct imx908 *sensor, u16 reg, u32 val)
{
	int ret;

	ret = imx908_write(sensor, reg, val & 0xff);
	if (ret)
		return ret;
	ret = imx908_write(sensor, reg + 1, (val >> 8) & 0xff);
	if (ret)
		return ret;
	return imx908_write(sensor, reg + 2, (val >> 16) & 0xff);
}

static int imx908_read(struct imx908 *sensor, u16 reg, u8 *buf, int len)
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
	msgs[1].len = len;
	msgs[1].buf = buf;
	ret = i2c_transfer(sensor->client->adapter, msgs, 2);
	if (ret == 2)
		return 0;
	return ret < 0 ? ret : -EIO;
}

static int imx908_write_mode(struct imx908 *sensor)
{
	unsigned int i;
	int ret;
	u32 shr0 = IMX908_VMAX - IMX908_EXPOSURE;

	for (i = 0; i < ARRAY_SIZE(imx908_common); i++) {
		ret = imx908_write(sensor, imx908_common[i].addr,
				   imx908_common[i].val);
		if (ret)
			return ret;
	}
	ret = imx908_write(sensor, 0x3003, 0x00);
	if (ret)
		return ret;
	ret = imx908_write(sensor, 0x3022, 0x00);
	if (ret)
		return ret;
	ret = imx908_write(sensor, 0x3023, 0x00);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(imx908_bit_depth_regs); i++) {
		ret = imx908_write(sensor, imx908_bit_depth_regs[i], 0x0c);
		if (ret)
			return ret;
	}
	ret = imx908_write(sensor, 0x3014, 0x03);
	if (ret)
		return ret;
	ret = imx908_write(sensor, 0x3015, 0x05);
	if (ret)
		return ret;
	/* Sony's RAW10 packet-type value. */
	ret = imx908_write(sensor, 0x359c, 0x2c);
	if (ret)
		return ret;
	ret = imx908_write(sensor, 0x3040, 0x01);
	if (ret)
		return ret;
	ret = imx908_write16(sensor, 0x30dc, 50);
	if (ret)
		return ret;
	ret = imx908_write(sensor, 0x3018, 0x00);
	if (ret)
		return ret;
	ret = imx908_write24(sensor, 0x3028, IMX908_VMAX);
	if (ret)
		return ret;
	ret = imx908_write16(sensor, 0x302c, IMX908_HMAX);
	if (ret)
		return ret;
	ret = imx908_write24(sensor, 0x3050, shr0);
	if (ret)
		return ret;
	return imx908_write16(sensor, 0x3070, 80);
}

static int imx908_power(struct imx908 *sensor, bool on)
{
	int ret;

	if (!on) {
		gpiod_set_value_cansleep(sensor->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(sensor->supplies),
				       sensor->supplies);
		clk_disable_unprepare(sensor->xclk);
		return 0;
	}

	ret = clk_set_rate(sensor->xclk, IMX908_XCLK_HZ);
	if (ret)
		dev_warn(&sensor->client->dev,
			 "xclk set_rate(%u) failed: %d, rate is %lu\n",
			 IMX908_XCLK_HZ, ret, clk_get_rate(sensor->xclk));
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
	fsleep(20000);
	return 0;
}

static int imx908_identify(struct imx908 *sensor)
{
	u8 idb[2];
	u16 id;
	int ret;
	int try;

	ret = imx908_write(sensor, IMX908_REG_STANDBY, IMX908_STANDBY_CANCEL);
	if (ret)
		return ret;
	fsleep(24000);
	ret = -EIO;
	for (try = 0; try < 5; try++) {
		ret = imx908_read(sensor, IMX908_REG_CHIP_ID, idb, 2);
		if (!ret)
			break;
		fsleep(20000);
	}
	if (ret)
		return ret;
	id = idb[0] | ((u16)idb[1] << 8);
	if (id != IMX908_CHIP_ID) {
		dev_err(&sensor->client->dev,
			"chip id 0x%04x is not IMX908 (0x%04x) at i2c 0x%02x\n",
			id, IMX908_CHIP_ID, sensor->client->addr);
		return -ENODEV;
	}
	dev_info(&sensor->client->dev,
		 "IMX908 id 0x%04x, %u lanes, link %llu Hz, xclk %u\n",
		 id, IMX908_LANES, IMX908_LINK_HZ, IMX908_XCLK_HZ);
	return imx908_write(sensor, IMX908_REG_STANDBY, IMX908_STANDBY_EN);
}

static int imx908_start(struct imx908 *sensor)
{
	unsigned int idx;
	int ret;

	ret = imx908_write_mode(sensor);
	if (ret)
		return ret;
	idx = sensor->link_freq ? sensor->link_freq->val : 2;
	if (idx >= ARRAY_SIZE(imx908_rate_sel))
		idx = 2;
	ret = imx908_write(sensor, 0x3015, imx908_rate_sel[idx]);
	if (ret)
		return ret;
	dev_info(&sensor->client->dev, "datarate sel 0x%02x, link %lld Hz\n",
		 imx908_rate_sel[idx], imx908_link_freq[idx]);
	ret = imx908_write(sensor, IMX908_REG_STANDBY, IMX908_STANDBY_CANCEL);
	if (ret)
		return ret;
	fsleep(24000);
	return imx908_write(sensor, IMX908_REG_XMSTA, IMX908_XMSTA_START);
}

static int imx908_stop(struct imx908 *sensor)
{
	int ret;

	ret = imx908_write(sensor, IMX908_REG_STANDBY, IMX908_STANDBY_EN);
	if (ret)
		return ret;
	return imx908_write(sensor, IMX908_REG_XMSTA, IMX908_XMSTA_STOP);
}

static int imx908_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx908 *sensor = to_imx908(sd);
	int ret;

	mutex_lock(&sensor->lock);
	if (enable == sensor->streaming) {
		ret = 0;
		goto out;
	}
	if (enable)
		ret = imx908_start(sensor);
	else
		ret = imx908_stop(sensor);
	if (!ret)
		sensor->streaming = enable;
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int imx908_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, 0) = imx908_fmt;
	return 0;
}

static int imx908_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	return 0;
}

static int imx908_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;
	fse->min_width = fse->max_width = IMX908_WIDTH;
	fse->min_height = fse->max_height = IMX908_HEIGHT;
	return 0;
}

static int imx908_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_state_get_format(state, fmt->pad);
	return 0;
}

static int imx908_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	*v4l2_subdev_state_get_format(state, fmt->pad) = imx908_fmt;
	fmt->format = imx908_fmt;
	return 0;
}

static int imx908_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP_DEFAULT)
		return -EINVAL;
	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = IMX908_WIDTH;
	sel->r.height = IMX908_HEIGHT;
	return 0;
}

static const struct v4l2_subdev_video_ops imx908_video_ops = {
	.s_stream = imx908_s_stream,
};

static const struct v4l2_subdev_pad_ops imx908_pad_ops = {
	.enum_mbus_code = imx908_enum_mbus_code,
	.enum_frame_size = imx908_enum_frame_size,
	.get_fmt = imx908_get_fmt,
	.set_fmt = imx908_set_fmt,
	.get_selection = imx908_get_selection,
};

static const struct v4l2_subdev_internal_ops imx908_internal_ops = {
	.init_state = imx908_init_state,
};

static const struct v4l2_subdev_ops imx908_subdev_ops = {
	.video = &imx908_video_ops,
	.pad = &imx908_pad_ops,
};

static int imx908_init_controls(struct imx908 *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	const u64 pixel_rate = IMX908_LINK_HZ * 2 * IMX908_LANES / 10;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 2);
	if (ret)
		return ret;
	sensor->link_freq = v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ,
						     ARRAY_SIZE(imx908_link_freq) - 1,
						     4, imx908_link_freq);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE,
			  pixel_rate, pixel_rate, 1, pixel_rate);
	if (hdl->error)
		return hdl->error;
	sensor->sd.ctrl_handler = hdl;
	return 0;
}

static int imx908_parse_endpoint(struct imx908 *sensor)
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
	if (lanes != IMX908_LANES)
		return dev_err_probe(dev, -EINVAL,
				     "IMX908 table is %u lanes, overlay has %u\n",
				     IMX908_LANES, lanes);
	if (!link)
		return dev_err_probe(dev, -EINVAL, "missing link frequency\n");
	return 0;
}

static int imx908_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx908 *sensor;
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
	ret = imx908_parse_endpoint(sensor);
	if (ret)
		return ret;

	v4l2_i2c_subdev_init(&sensor->sd, client, &imx908_subdev_ops);
	sensor->sd.internal_ops = &imx908_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;
	ret = imx908_init_controls(sensor);
	if (ret)
		goto err_entity;
	sensor->sd.state_lock = &sensor->lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_ctrls;
	ret = imx908_power(sensor, true);
	if (ret)
		goto err_subdev;
	ret = imx908_identify(sensor);
	if (ret)
		goto err_power;
	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_power;
	return 0;

err_power:
	imx908_power(sensor, false);
err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
	mutex_destroy(&sensor->lock);
	return ret;
}

static void imx908_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx908 *sensor = to_imx908(sd);

	v4l2_async_unregister_subdev(sd);
	imx908_power(sensor, false);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id imx908_of_match[] = {
	{ .compatible = "sony,imx908" },
	{ }
};
MODULE_DEVICE_TABLE(of, imx908_of_match);

static const struct i2c_device_id imx908_id[] = {
	{ "imx908" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, imx908_id);

static struct i2c_driver imx908_i2c_driver = {
	.driver = {
		.name = "imx908",
		.of_match_table = imx908_of_match,
	},
	.probe = imx908_probe,
	.remove = imx908_remove,
	.id_table = imx908_id,
};
module_i2c_driver(imx908_i2c_driver);

MODULE_DESCRIPTION("Sony IMX908 CSI-2 sensor driver");
MODULE_LICENSE("GPL");
