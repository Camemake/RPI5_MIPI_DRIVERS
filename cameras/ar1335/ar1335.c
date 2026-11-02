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

#define AR1335_REG_CHIP_ID		0x0000
#define AR1335_CHIP_ID			0x0153
#define AR1335_REG_RESET		0x301a
#define AR1335_MODE_STANDBY		0x0218
#define AR1335_MODE_STREAMING		0x021c

#define AR1335_WIDTH			4208
#define AR1335_HEIGHT			3120
#define AR1335_XCLK_HZ			24000000
#define AR1335_LANES			4
#define AR1335_LINK_HZ			276000000ULL
#define AR1335_LINE_LENGTH		0x1240
#define AR1335_FRAME_LENGTH		0x0c4e

struct ar1335_reg {
	u16 addr;
	u16 val;
};

static const struct ar1335_reg ar1335_4208x3120[] = {
#include "regs.inc"
};

static const struct v4l2_mbus_framefmt ar1335_fmt = {
	.width = AR1335_WIDTH,
	.height = AR1335_HEIGHT,
	.code = MEDIA_BUS_FMT_SGRBG10_1X10,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_NONE,
};

static const s64 ar1335_link_freq[] = {
	AR1335_LINK_HZ,
};

struct ar1335 {
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

static inline struct ar1335 *to_ar1335(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ar1335, sd);
}

static int ar1335_write16(struct ar1335 *sensor, u16 reg, u16 val)
{
	u8 buf[4] = { reg >> 8, reg & 0xff, val >> 8, val & 0xff };
	int ret;

	ret = i2c_master_send(sensor->client, buf, sizeof(buf));
	if (ret == sizeof(buf))
		return 0;
	dev_err(&sensor->client->dev, "write 0x%04x failed: %d\n", reg, ret);
	return ret < 0 ? ret : -EIO;
}

static int ar1335_read16(struct ar1335 *sensor, u16 reg, u16 *val)
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

static int ar1335_power(struct ar1335 *sensor, bool on)
{
	int ret;

	if (!on) {
		gpiod_set_value_cansleep(sensor->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(sensor->supplies),
				       sensor->supplies);
		clk_disable_unprepare(sensor->xclk);
		return 0;
	}

	ret = clk_set_rate(sensor->xclk, AR1335_XCLK_HZ);
	if (ret)
		dev_warn(&sensor->client->dev,
			 "xclk set_rate(%u) failed: %d, rate is %lu\n",
			 AR1335_XCLK_HZ, ret, clk_get_rate(sensor->xclk));

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

static int ar1335_identify(struct ar1335 *sensor)
{
	u16 id;
	int ret;

	ret = ar1335_read16(sensor, AR1335_REG_CHIP_ID, &id);
	if (ret)
		return ret;
	if (id != AR1335_CHIP_ID) {
		dev_err(&sensor->client->dev,
			"chip id 0x%04x is not AR1335 (0x%04x) at i2c 0x%02x\n",
			id, AR1335_CHIP_ID, sensor->client->addr);
		return -ENODEV;
	}
	dev_info(&sensor->client->dev,
		 "AR1335 id 0x%04x, %u lanes, link %llu Hz, xclk %u\n",
		 id, AR1335_LANES, AR1335_LINK_HZ, AR1335_XCLK_HZ);
	return 0;
}

static int ar1335_write_table(struct ar1335 *sensor)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ar1335_4208x3120); i++) {
		if (!ar1335_4208x3120[i].addr) {
			msleep(ar1335_4208x3120[i].val);
			continue;
		}
		ret = ar1335_write16(sensor, ar1335_4208x3120[i].addr,
				     ar1335_4208x3120[i].val);
		if (ret)
			return ret;
	}
	return 0;
}

static int ar1335_start(struct ar1335 *sensor)
{
	return ar1335_write_table(sensor);
}

static int ar1335_stop(struct ar1335 *sensor)
{
	return ar1335_write16(sensor, AR1335_REG_RESET, AR1335_MODE_STANDBY);
}

static int ar1335_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ar1335 *sensor = to_ar1335(sd);
	int ret;

	mutex_lock(&sensor->lock);
	if (enable == sensor->streaming) {
		ret = 0;
		goto out;
	}
	if (enable)
		ret = ar1335_start(sensor);
	else
		ret = ar1335_stop(sensor);
	if (!ret)
		sensor->streaming = enable;
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int ar1335_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, 0) = ar1335_fmt;
	return 0;
}

static int ar1335_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	return 0;
}

static int ar1335_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;
	fse->min_width = fse->max_width = AR1335_WIDTH;
	fse->min_height = fse->max_height = AR1335_HEIGHT;
	return 0;
}

static int ar1335_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_state_get_format(state, fmt->pad);
	return 0;
}

static int ar1335_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	*v4l2_subdev_state_get_format(state, fmt->pad) = ar1335_fmt;
	fmt->format = ar1335_fmt;
	return 0;
}

static int ar1335_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{
	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP_DEFAULT)
		return -EINVAL;
	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = AR1335_WIDTH;
	sel->r.height = AR1335_HEIGHT;
	return 0;
}

static const struct v4l2_subdev_video_ops ar1335_video_ops = {
	.s_stream = ar1335_s_stream,
};

static const struct v4l2_subdev_pad_ops ar1335_pad_ops = {
	.enum_mbus_code = ar1335_enum_mbus_code,
	.enum_frame_size = ar1335_enum_frame_size,
	.get_fmt = ar1335_get_fmt,
	.set_fmt = ar1335_set_fmt,
	.get_selection = ar1335_get_selection,
};

static const struct v4l2_subdev_internal_ops ar1335_internal_ops = {
	.init_state = ar1335_init_state,
};

static const struct v4l2_subdev_ops ar1335_subdev_ops = {
	.video = &ar1335_video_ops,
	.pad = &ar1335_pad_ops,
};

static int ar1335_init_controls(struct ar1335 *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	const u64 pixel_rate = AR1335_LINK_HZ * 2 * AR1335_LANES / 10;
	const s32 hblank = AR1335_LINE_LENGTH - AR1335_WIDTH;
	const s32 vblank = AR1335_FRAME_LENGTH - AR1335_HEIGHT;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 4);
	if (ret)
		return ret;

	v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ,
			       0, 0, ar1335_link_freq);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE,
			  pixel_rate, pixel_rate, 1, pixel_rate);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_HBLANK, hblank, hblank, 1, hblank);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_VBLANK, vblank, vblank, 1, vblank);

	if (hdl->error)
		return hdl->error;

	sensor->sd.ctrl_handler = hdl;
	return 0;
}

static int ar1335_parse_endpoint(struct ar1335 *sensor)
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

	if (lanes != AR1335_LANES)
		return dev_err_probe(dev, -EINVAL,
				     "AR1335 table is %u lanes, overlay has %u\n",
				     AR1335_LANES, lanes);
	if (link && link != AR1335_LINK_HZ)
		return dev_err_probe(dev, -EINVAL,
				     "link frequency %llu does not match %llu\n",
				     link, AR1335_LINK_HZ);
	return 0;
}

static int ar1335_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ar1335 *sensor;
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

	ret = ar1335_parse_endpoint(sensor);
	if (ret)
		return ret;

	v4l2_i2c_subdev_init(&sensor->sd, client, &ar1335_subdev_ops);
	sensor->sd.internal_ops = &ar1335_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = ar1335_init_controls(sensor);
	if (ret)
		goto err_entity;

	sensor->sd.state_lock = &sensor->lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_ctrls;

	ret = ar1335_power(sensor, true);
	if (ret)
		goto err_subdev;
	ret = ar1335_identify(sensor);
	if (ret)
		goto err_power;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_power;

	return 0;

err_power:
	ar1335_power(sensor, false);
err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
	mutex_destroy(&sensor->lock);
	return ret;
}

static void ar1335_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar1335 *sensor = to_ar1335(sd);

	v4l2_async_unregister_subdev(sd);
	ar1335_power(sensor, false);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id ar1335_of_match[] = {
	{ .compatible = "onnn,ar1335" },
	{ }
};
MODULE_DEVICE_TABLE(of, ar1335_of_match);

static const struct i2c_device_id ar1335_id[] = {
	{ "ar1335" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ar1335_id);

static struct i2c_driver ar1335_i2c_driver = {
	.driver = {
		.name = "ar1335",
		.of_match_table = ar1335_of_match,
	},
	.probe = ar1335_probe,
	.remove = ar1335_remove,
	.id_table = ar1335_id,
};
module_i2c_driver(ar1335_i2c_driver);

MODULE_DESCRIPTION("onsemi AR1335 CSI-2 sensor driver");
MODULE_LICENSE("GPL");
