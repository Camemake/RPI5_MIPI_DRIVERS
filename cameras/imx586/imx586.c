// SPDX-License-Identifier: GPL-2.0
/*
 * Sony IMX586 CSI-2 sensor driver for Raspberry Pi 5 CAM0.
 *
 * Same 27 MHz carrier as the other CAM0 modules. I2C 0x1a.
 * The table is the 4000x3000 RAW10 4-lane list (0x0114=0x03, 0x0901=0x22).
 * It was written for a 24 MHz INCK (0x0136=0x18). This board's crystal is
 * 27 MHz, so the table's EXCK bytes are rewritten to 0x1B00 and the PLL
 * ratios are left alone.
 *
 * OP bit rate = xclk / 0x030D * 0x030E:0x030F / 0x030B
 *             = 27000000 / 6 * 400 / 2 = 900 Mbps.
 * That same OP PLL (div 6, multiplier 400) is the one the Rockchip driver
 * tags as 400 MHz link / 800 Mbps at 24 MHz. Link frequency here is half
 * the bit rate: 450 MHz. Chip id is 0x0016/0x0017 = 0x0586. Stream on is
 * 0x0100=1 after the table has settled.
 *
 * The table's analog gain 0x0204/0x0205 = 0 is 1x, and at the table's
 * exposure that picture is a few codes above black. 0x03e0 is 32x
 * (1024 / (1024 - 992)) and is what makes the room visible. 0x0601 is
 * forced off so a leftover color-bar write cannot stay on.
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

#define IMX586_REG_MODE			0x0100
#define IMX586_REG_CHIP_ID_H		0x0016
#define IMX586_REG_CHIP_ID_L		0x0017
#define IMX586_CHIP_ID			0x0586

#define IMX586_WIDTH			4000
#define IMX586_HEIGHT			3000
#define IMX586_XCLK_HZ			27000000
#define IMX586_LINK_HZ			450000000ULL

struct imx586_reg {
	u16 addr;
	u8 val;
};

static const struct imx586_reg imx586_4000x3000[] = {
#include "regs.inc"
};

struct imx586_mode {
	u32 lanes;
	u64 link_freq;
	u64 pixel_rate;
	u32 hblank;
	u32 vblank;
	const struct imx586_reg *regs;
	unsigned int num_regs;
};

static const s64 imx586_link_freq[] = {
	400000000, 450000000, 500000000, 625000000, 850000000,
};

static const struct imx586_mode imx586_mode = {
	.lanes = 4,
	.link_freq = IMX586_LINK_HZ,
	.pixel_rate = IMX586_LINK_HZ * 2 * 4 / 10,
	.hblank = 3872,
	.vblank = 60,
	.regs = imx586_4000x3000,
	.num_regs = ARRAY_SIZE(imx586_4000x3000),
};

struct imx586 {
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct clk *xclk;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	const struct imx586_mode *mode;
	struct mutex lock;
	bool streaming;
};

static inline struct imx586 *to_imx586(struct v4l2_subdev *sd)
{
	return container_of(sd, struct imx586, sd);
}

static int imx586_write(struct imx586 *sensor, u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;

	ret = i2c_master_send(sensor->client, buf, sizeof(buf));
	if (ret == sizeof(buf))
		return 0;
	dev_err(&sensor->client->dev, "write 0x%04x failed: %d\n", reg, ret);
	return ret < 0 ? ret : -EIO;
}

static int imx586_read(struct imx586 *sensor, u16 reg, u8 *val)
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

static int imx586_write_table(struct imx586 *sensor)
{
	unsigned int i;
	int ret;

	for (i = 0; i < sensor->mode->num_regs; i++) {
		ret = imx586_write(sensor, sensor->mode->regs[i].addr,
				   sensor->mode->regs[i].val);
		if (ret)
			return ret;
	}
	return 0;
}

static int imx586_identify(struct imx586 *sensor)
{
	u8 hi, lo;
	u16 id;
	int ret;

	ret = imx586_read(sensor, IMX586_REG_CHIP_ID_H, &hi);
	if (ret)
		return ret;
	ret = imx586_read(sensor, IMX586_REG_CHIP_ID_L, &lo);
	if (ret)
		return ret;
	id = ((u16)hi << 8) | lo;
	if (id != IMX586_CHIP_ID) {
		dev_err(&sensor->client->dev,
			"chip id 0x%04x is not IMX586 (0x%04x)\n",
			id, IMX586_CHIP_ID);
		return -ENODEV;
	}
	dev_info(&sensor->client->dev,
		 "IMX586 id 0x%04x, %u lanes, link %llu Hz, xclk %lu\n",
		 id, sensor->mode->lanes, sensor->mode->link_freq,
		 clk_get_rate(sensor->xclk));
	return 0;
}

static int imx586_start(struct imx586 *sensor)
{
	int ret;

	ret = imx586_write(sensor, IMX586_REG_MODE, 0x00);
	if (ret)
		return ret;
	ret = imx586_write_table(sensor);
	if (ret)
		return ret;
	ret = imx586_write(sensor, 0x0204, 0x03);
	if (ret)
		return ret;
	ret = imx586_write(sensor, 0x0205, 0xe0);
	if (ret)
		return ret;
	ret = imx586_write(sensor, 0x0216, 0x03);
	if (ret)
		return ret;
	ret = imx586_write(sensor, 0x0217, 0xe0);
	if (ret)
		return ret;
	ret = imx586_write(sensor, 0x0601, 0x00);
	if (ret)
		return ret;
	fsleep(10 * 1000);
	return imx586_write(sensor, IMX586_REG_MODE, 0x01);
}

static int imx586_stop(struct imx586 *sensor)
{
	return imx586_write(sensor, IMX586_REG_MODE, 0x00);
}

static int imx586_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx586 *sensor = to_imx586(sd);
	int ret;

	mutex_lock(&sensor->lock);
	if (enable == sensor->streaming) {
		ret = 0;
		goto out;
	}
	if (enable)
		ret = imx586_start(sensor);
	else
		ret = imx586_stop(sensor);
	if (!ret)
		sensor->streaming = enable;
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static const struct v4l2_mbus_framefmt imx586_fmt = {
	.width = IMX586_WIDTH,
	.height = IMX586_HEIGHT,
	.code = MEDIA_BUS_FMT_SRGGB10_1X10,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_NONE,
};

static int imx586_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, 0) = imx586_fmt;
	return 0;
}

static int imx586_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	return 0;
}

static int imx586_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;
	fse->min_width = fse->max_width = IMX586_WIDTH;
	fse->min_height = fse->max_height = IMX586_HEIGHT;
	return 0;
}

static int imx586_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_state_get_format(state, fmt->pad);
	return 0;
}

static int imx586_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	*v4l2_subdev_state_get_format(state, fmt->pad) = imx586_fmt;
	fmt->format = imx586_fmt;
	return 0;
}

static int imx586_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP_DEFAULT)
		return -EINVAL;
	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = IMX586_WIDTH;
	sel->r.height = IMX586_HEIGHT;
	return 0;
}

static const struct v4l2_subdev_video_ops imx586_video_ops = {
	.s_stream = imx586_s_stream,
};

static const struct v4l2_subdev_pad_ops imx586_pad_ops = {
	.enum_mbus_code = imx586_enum_mbus_code,
	.enum_frame_size = imx586_enum_frame_size,
	.get_fmt = imx586_get_fmt,
	.set_fmt = imx586_set_fmt,
	.get_selection = imx586_get_selection,
};

static const struct v4l2_subdev_internal_ops imx586_internal_ops = {
	.init_state = imx586_init_state,
};

static const struct v4l2_subdev_ops imx586_subdev_ops = {
	.video = &imx586_video_ops,
	.pad = &imx586_pad_ops,
};

static int imx586_init_controls(struct imx586 *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	const struct imx586_mode *mode = sensor->mode;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 4);
	if (ret)
		return ret;

	v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ,
			       ARRAY_SIZE(imx586_link_freq) - 1, 1,
			       imx586_link_freq);
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

static int imx586_parse_endpoint(struct imx586 *sensor)
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
				     "IMX586 overlay has %u lanes, table is %u\n",
				     lanes, sensor->mode->lanes);
	if (link && link != sensor->mode->link_freq)
		dev_info(dev, "overlay link %llu Hz, control menu starts at %llu Hz\n",
			 link, sensor->mode->link_freq);
	return 0;
}

static int imx586_power(struct imx586 *sensor, bool on)
{
	int ret;

	if (!on) {
		gpiod_set_value_cansleep(sensor->pwdn_gpio, 0);
		gpiod_set_value_cansleep(sensor->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
		clk_disable_unprepare(sensor->xclk);
		return 0;
	}

	ret = clk_set_rate(sensor->xclk, IMX586_XCLK_HZ);
	if (ret)
		dev_warn(&sensor->client->dev,
			 "xclk set_rate(%u) failed: %d, rate is %lu\n",
			 IMX586_XCLK_HZ, ret, clk_get_rate(sensor->xclk));

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

static int imx586_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx586 *sensor;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;
	sensor->client = client;
	sensor->mode = &imx586_mode;
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

	ret = imx586_parse_endpoint(sensor);
	if (ret)
		return ret;

	v4l2_i2c_subdev_init(&sensor->sd, client, &imx586_subdev_ops);
	sensor->sd.internal_ops = &imx586_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = imx586_init_controls(sensor);
	if (ret)
		goto err_entity;

	sensor->sd.state_lock = &sensor->lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_ctrls;

	ret = imx586_power(sensor, true);
	if (ret)
		goto err_subdev;
	ret = imx586_identify(sensor);
	if (ret)
		goto err_power;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_power;

	return 0;

err_power:
	imx586_power(sensor, false);
err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
	return ret;
}

static void imx586_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx586 *sensor = to_imx586(sd);

	v4l2_async_unregister_subdev(sd);
	imx586_power(sensor, false);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id imx586_of_match[] = {
	{ .compatible = "sony,imx586" },
	{ }
};
MODULE_DEVICE_TABLE(of, imx586_of_match);

static const struct i2c_device_id imx586_id[] = {
	{ "imx586" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, imx586_id);

static struct i2c_driver imx586_i2c_driver = {
	.driver = {
		.name = "imx586",
		.of_match_table = imx586_of_match,
	},
	.probe = imx586_probe,
	.remove = imx586_remove,
	.id_table = imx586_id,
};
module_i2c_driver(imx586_i2c_driver);

MODULE_DESCRIPTION("Sony IMX586 CSI-2 sensor driver");
MODULE_LICENSE("GPL");
