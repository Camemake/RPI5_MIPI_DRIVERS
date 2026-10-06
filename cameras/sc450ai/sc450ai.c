// SPDX-License-Identifier: GPL-2.0
/*
 * SmartSens SC450AI CSI-2 sensor driver for Raspberry Pi.
 *
 * Register tables are the linear 2688x1520 modes from the Rockchip BSP driver
 * (XCLK 27 MHz, RAW10, BGGR). 2-lane runs at a 360 MHz link; 4-lane at 180 MHz.
 * Those are the link frequencies that driver selects for these exact tables.
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

#define SC450AI_REG_CHIP_ID		0x3107
#define SC450AI_CHIP_ID			0xbd2f
#define SC450AI_REG_CTRL_MODE		0x0100
#define SC450AI_MODE_STANDBY		0x00
#define SC450AI_MODE_STREAMING		0x01

#define SC450AI_WIDTH			2688
#define SC450AI_HEIGHT			1520
#define SC450AI_XCLK_HZ			27000000

struct sc450ai_reg {
	u16 addr;
	u8 val;
};

static const struct sc450ai_reg sc450ai_2688x1520_2lane[] = {
	{ 0x0103, 0x01 },
	{ 0x0100, 0x00 },
	{ 0x36e9, 0x80 },
	{ 0x36f9, 0x80 },
	{ 0x3018, 0x3a },
	{ 0x3019, 0x0c },
	{ 0x301c, 0x78 },
	{ 0x301f, 0x3c },
	{ 0x302d, 0xa0 },
	{ 0x302e, 0x00 },
	{ 0x3208, 0x0a },
	{ 0x3209, 0x80 },
	{ 0x320a, 0x05 },
	{ 0x320b, 0xf0 },
	{ 0x320c, 0x02 },
	{ 0x320d, 0xee },
	{ 0x320e, 0x06 },
	{ 0x320f, 0x38 },
	{ 0x3214, 0x11 },
	{ 0x3215, 0x11 },
	{ 0x3220, 0x00 },
	{ 0x3223, 0xc0 },
	{ 0x3253, 0x10 },
	{ 0x325f, 0x44 },
	{ 0x3274, 0x09 },
	{ 0x3280, 0x01 },
	{ 0x3301, 0x07 },
	{ 0x3306, 0x20 },
	{ 0x3308, 0x08 },
	{ 0x330b, 0x58 },
	{ 0x330e, 0x18 },
	{ 0x3315, 0x00 },
	{ 0x335d, 0x60 },
	{ 0x3364, 0x56 },
	{ 0x338f, 0x80 },
	{ 0x3390, 0x08 },
	{ 0x3391, 0x18 },
	{ 0x3392, 0x38 },
	{ 0x3393, 0x07 },
	{ 0x3394, 0x10 },
	{ 0x3395, 0x18 },
	{ 0x3396, 0x08 },
	{ 0x3397, 0x18 },
	{ 0x3398, 0x38 },
	{ 0x3399, 0x10 },
	{ 0x339a, 0x13 },
	{ 0x339b, 0x15 },
	{ 0x339c, 0x18 },
	{ 0x33af, 0x18 },
	{ 0x3400, 0x16 },
	{ 0x360f, 0x13 },
	{ 0x3621, 0xec },
	{ 0x3622, 0x00 },
	{ 0x3625, 0x0b },
	{ 0x3627, 0x20 },
	{ 0x3630, 0x90 },
	{ 0x3633, 0x56 },
	{ 0x3637, 0x1d },
	{ 0x3638, 0x12 },
	{ 0x363c, 0x0f },
	{ 0x363d, 0x0f },
	{ 0x363e, 0x08 },
	{ 0x3670, 0x4a },
	{ 0x3671, 0xe0 },
	{ 0x3672, 0xe0 },
	{ 0x3673, 0xe0 },
	{ 0x3674, 0xc0 },
	{ 0x3675, 0x87 },
	{ 0x3676, 0x8c },
	{ 0x367a, 0x48 },
	{ 0x367b, 0x58 },
	{ 0x367c, 0x48 },
	{ 0x367d, 0x58 },
	{ 0x3690, 0x22 },
	{ 0x3691, 0x33 },
	{ 0x3692, 0x44 },
	{ 0x3699, 0x03 },
	{ 0x369a, 0x0f },
	{ 0x369b, 0x1f },
	{ 0x369c, 0x40 },
	{ 0x369d, 0x78 },
	{ 0x36a2, 0x48 },
	{ 0x36a3, 0x78 },
	{ 0x36b0, 0x53 },
	{ 0x36b1, 0x74 },
	{ 0x36b2, 0x34 },
	{ 0x36b3, 0x40 },
	{ 0x36b4, 0x78 },
	{ 0x36b7, 0xa0 },
	{ 0x36b8, 0xa0 },
	{ 0x36b9, 0x20 },
	{ 0x36bd, 0x40 },
	{ 0x36be, 0x48 },
	{ 0x36d0, 0x20 },
	{ 0x36e0, 0x08 },
	{ 0x36e1, 0x08 },
	{ 0x36e2, 0x12 },
	{ 0x36e3, 0x48 },
	{ 0x36e4, 0x78 },
	{ 0x36ec, 0x43 },
	{ 0x36fc, 0x00 },
	{ 0x3907, 0x00 },
	{ 0x3908, 0x41 },
	{ 0x391e, 0xf1 },
	{ 0x391f, 0x11 },
	{ 0x3921, 0x10 },
	{ 0x3933, 0x82 },
	{ 0x3934, 0x30 },
	{ 0x3935, 0x02 },
	{ 0x3936, 0xc7 },
	{ 0x3937, 0x76 },
	{ 0x3938, 0x76 },
	{ 0x3939, 0x00 },
	{ 0x393a, 0x28 },
	{ 0x393b, 0x00 },
	{ 0x393c, 0x23 },
	{ 0x3e01, 0xc2 },
	{ 0x3e02, 0x60 },
	{ 0x3e03, 0x0b },
	{ 0x3e08, 0x03 },
	{ 0x3e1b, 0x2a },
	{ 0x440e, 0x02 },
	{ 0x4509, 0x20 },
	{ 0x4837, 0x16 },
	{ 0x5000, 0x0e },
	{ 0x5001, 0x44 },
	{ 0x5780, 0x76 },
	{ 0x5784, 0x08 },
	{ 0x5785, 0x04 },
	{ 0x5787, 0x0a },
	{ 0x5788, 0x0a },
	{ 0x5789, 0x0a },
	{ 0x578a, 0x0a },
	{ 0x578b, 0x0a },
	{ 0x578c, 0x0a },
	{ 0x578d, 0x40 },
	{ 0x5790, 0x08 },
	{ 0x5791, 0x04 },
	{ 0x5792, 0x04 },
	{ 0x5793, 0x08 },
	{ 0x5794, 0x04 },
	{ 0x5795, 0x04 },
	{ 0x5799, 0x46 },
	{ 0x579a, 0x77 },
	{ 0x57a1, 0x04 },
	{ 0x57a8, 0xd0 },
	{ 0x57aa, 0x28 },
	{ 0x57ab, 0x00 },
	{ 0x57ac, 0x00 },
	{ 0x57ad, 0x00 },
	{ 0x59e0, 0xfe },
	{ 0x59e1, 0x40 },
	{ 0x59e2, 0x3f },
	{ 0x59e3, 0x38 },
	{ 0x59e4, 0x30 },
	{ 0x59e5, 0x3f },
	{ 0x59e6, 0x38 },
	{ 0x59e7, 0x30 },
	{ 0x59e8, 0x3f },
	{ 0x59e9, 0x3c },
	{ 0x59ea, 0x38 },
	{ 0x59eb, 0x3f },
	{ 0x59ec, 0x3c },
	{ 0x59ed, 0x38 },
	{ 0x59ee, 0xfe },
	{ 0x59ef, 0x40 },
	{ 0x59f4, 0x3f },
	{ 0x59f5, 0x38 },
	{ 0x59f6, 0x30 },
	{ 0x59f7, 0x3f },
	{ 0x59f8, 0x38 },
	{ 0x59f9, 0x30 },
	{ 0x59fa, 0x3f },
	{ 0x59fb, 0x3c },
	{ 0x59fc, 0x38 },
	{ 0x59fd, 0x3f },
	{ 0x59fe, 0x3c },
	{ 0x59ff, 0x38 },
	{ 0x36e9, 0x44 },
	{ 0x36f9, 0x20 },
};

static const struct sc450ai_reg sc450ai_2688x1520_4lane[] = {
	{ 0x0103, 0x01 },
	{ 0x0100, 0x00 },
	{ 0x36e9, 0x80 },
	{ 0x36f9, 0x80 },
	{ 0x301c, 0x78 },
	{ 0x301f, 0x02 },
	{ 0x302d, 0xa0 },
	{ 0x302e, 0x00 },
	{ 0x3208, 0x0a },
	{ 0x3209, 0x80 },
	{ 0x320a, 0x05 },
	{ 0x320b, 0xf0 },
	{ 0x320c, 0x04 },
	{ 0x320d, 0x60 },
	{ 0x320e, 0x0c },
	{ 0x320f, 0x30 },
	{ 0x3214, 0x11 },
	{ 0x3215, 0x11 },
	{ 0x3220, 0x00 },
	{ 0x3223, 0xc0 },
	{ 0x3253, 0x10 },
	{ 0x325f, 0x44 },
	{ 0x3274, 0x09 },
	{ 0x3280, 0x01 },
	{ 0x3301, 0x08 },
	{ 0x3306, 0x24 },
	{ 0x3309, 0x60 },
	{ 0x330b, 0x64 },
	{ 0x330d, 0x30 },
	{ 0x3315, 0x00 },
	{ 0x331f, 0x59 },
	{ 0x335d, 0x60 },
	{ 0x3364, 0x56 },
	{ 0x338f, 0x80 },
	{ 0x3390, 0x08 },
	{ 0x3391, 0x18 },
	{ 0x3392, 0x38 },
	{ 0x3393, 0x0a },
	{ 0x3394, 0x10 },
	{ 0x3395, 0x18 },
	{ 0x3396, 0x08 },
	{ 0x3397, 0x18 },
	{ 0x3398, 0x38 },
	{ 0x3399, 0x0f },
	{ 0x339a, 0x12 },
	{ 0x339b, 0x14 },
	{ 0x339c, 0x18 },
	{ 0x33af, 0x18 },
	{ 0x3400, 0x16 },
	{ 0x360f, 0x13 },
	{ 0x3621, 0xec },
	{ 0x3627, 0xa0 },
	{ 0x3630, 0x90 },
	{ 0x3633, 0x56 },
	{ 0x3637, 0x1d },
	{ 0x3638, 0x0a },
	{ 0x363c, 0x0f },
	{ 0x363d, 0x0f },
	{ 0x363e, 0x08 },
	{ 0x3670, 0x4a },
	{ 0x3671, 0xe0 },
	{ 0x3672, 0xe0 },
	{ 0x3673, 0xe0 },
	{ 0x3674, 0xb0 },
	{ 0x3675, 0x88 },
	{ 0x3676, 0x8c },
	{ 0x367a, 0x48 },
	{ 0x367b, 0x58 },
	{ 0x367c, 0x48 },
	{ 0x367d, 0x58 },
	{ 0x3690, 0x34 },
	{ 0x3691, 0x43 },
	{ 0x3692, 0x44 },
	{ 0x3699, 0x03 },
	{ 0x369a, 0x0f },
	{ 0x369b, 0x1f },
	{ 0x369c, 0x40 },
	{ 0x369d, 0x48 },
	{ 0x36a2, 0x48 },
	{ 0x36a3, 0x78 },
	{ 0x36b0, 0x54 },
	{ 0x36b1, 0x75 },
	{ 0x36b2, 0x35 },
	{ 0x36b3, 0x48 },
	{ 0x36b4, 0x78 },
	{ 0x36b7, 0xa0 },
	{ 0x36b8, 0xa0 },
	{ 0x36b9, 0x20 },
	{ 0x36bd, 0x40 },
	{ 0x36be, 0x48 },
	{ 0x36d0, 0x20 },
	{ 0x36e0, 0x08 },
	{ 0x36e1, 0x08 },
	{ 0x36e2, 0x12 },
	{ 0x36e3, 0x48 },
	{ 0x36e4, 0x78 },
	{ 0x36fa, 0x0d },
	{ 0x36fb, 0xa4 },
	{ 0x36fc, 0x00 },
	{ 0x36fd, 0x24 },
	{ 0x3907, 0x00 },
	{ 0x3908, 0x41 },
	{ 0x391e, 0x01 },
	{ 0x391f, 0x11 },
	{ 0x3921, 0x10 },
	{ 0x3933, 0x82 },
	{ 0x3934, 0x0b },
	{ 0x3935, 0x02 },
	{ 0x3936, 0x5e },
	{ 0x3937, 0x76 },
	{ 0x3938, 0x78 },
	{ 0x3939, 0x00 },
	{ 0x393a, 0x28 },
	{ 0x393b, 0x00 },
	{ 0x393c, 0x1d },
	{ 0x3e01, 0xc2 },
	{ 0x3e02, 0x60 },
	{ 0x3e03, 0x0b },
	{ 0x3e08, 0x03 },
	{ 0x3e1b, 0x2a },
	{ 0x440e, 0x02 },
	{ 0x4509, 0x20 },
	{ 0x4837, 0x16 },
	{ 0x5000, 0x0e },
	{ 0x5001, 0x44 },
	{ 0x5780, 0x76 },
	{ 0x5784, 0x08 },
	{ 0x5785, 0x04 },
	{ 0x5787, 0x0a },
	{ 0x5788, 0x0a },
	{ 0x5789, 0x0a },
	{ 0x578a, 0x0a },
	{ 0x578b, 0x0a },
	{ 0x578c, 0x0a },
	{ 0x578d, 0x40 },
	{ 0x5790, 0x08 },
	{ 0x5791, 0x04 },
	{ 0x5792, 0x04 },
	{ 0x5793, 0x08 },
	{ 0x5794, 0x04 },
	{ 0x5795, 0x04 },
	{ 0x5799, 0x46 },
	{ 0x579a, 0x77 },
	{ 0x57a1, 0x04 },
	{ 0x57a8, 0xd0 },
	{ 0x57aa, 0x2a },
	{ 0x57ab, 0x7f },
	{ 0x57ac, 0x00 },
	{ 0x57ad, 0x00 },
	{ 0x59e0, 0xfe },
	{ 0x59e1, 0x40 },
	{ 0x59e2, 0x3f },
	{ 0x59e3, 0x38 },
	{ 0x59e4, 0x30 },
	{ 0x59e5, 0x3f },
	{ 0x59e6, 0x38 },
	{ 0x59e7, 0x30 },
	{ 0x59e8, 0x3f },
	{ 0x59e9, 0x3c },
	{ 0x59ea, 0x38 },
	{ 0x59eb, 0x3f },
	{ 0x59ec, 0x3c },
	{ 0x59ed, 0x38 },
	{ 0x59ee, 0xfe },
	{ 0x59ef, 0x40 },
	{ 0x59f4, 0x3f },
	{ 0x59f5, 0x38 },
	{ 0x59f6, 0x30 },
	{ 0x59f7, 0x3f },
	{ 0x59f8, 0x38 },
	{ 0x59f9, 0x30 },
	{ 0x59fa, 0x3f },
	{ 0x59fb, 0x3c },
	{ 0x59fc, 0x38 },
	{ 0x59fd, 0x3f },
	{ 0x59fe, 0x3c },
	{ 0x59ff, 0x38 },
	{ 0x36e9, 0x44 },
	{ 0x36f9, 0x20 },
};

struct sc450ai_mode {
	u32 lanes;
	u64 link_freq;
	u64 pixel_rate;
	u32 hblank;
	u32 vblank;
	const struct sc450ai_reg *regs;
	unsigned int num_regs;
};

/*
 * HTS in the table is in units of 4 pixels, matching the Rockchip driver's
 * hts_def = reg(0x320c) << 2. VTS is the register value itself.
 * 2-lane: HTS 0x02ee -> 3000, VTS 0x0638 -> 1592
 * 4-lane: HTS 0x0460 -> 4480, VTS 0x0c30 -> 3120
 */
static const struct sc450ai_mode sc450ai_modes[] = {
	{
		.lanes = 2,
		.link_freq = 360000000,
		.pixel_rate = 360000000ULL * 2 * 2 / 10,
		.hblank = 3000 - SC450AI_WIDTH,
		.vblank = 1592 - SC450AI_HEIGHT,
		.regs = sc450ai_2688x1520_2lane,
		.num_regs = 180,
	},
	{
		.lanes = 4,
		.link_freq = 180000000,
		.pixel_rate = 180000000ULL * 2 * 4 / 10,
		.hblank = 4480 - SC450AI_WIDTH,
		.vblank = 3120 - SC450AI_HEIGHT,
		.regs = sc450ai_2688x1520_4lane,
		.num_regs = 179,
	},
};

struct sc450ai {
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct clk *xclk;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *pwdn_gpio;
	const struct sc450ai_mode *mode;
	struct mutex lock;
	bool streaming;
};

static const s64 sc450ai_link_freq_2lane[] = { 360000000 };
static const s64 sc450ai_link_freq_4lane[] = { 180000000 };

static inline struct sc450ai *to_sc450ai(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sc450ai, sd);
}

static int sc450ai_write(struct sc450ai *sensor, u16 reg, u8 val)
{
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;

	ret = i2c_master_send(sensor->client, buf, sizeof(buf));
	if (ret == sizeof(buf))
		return 0;
	dev_err(&sensor->client->dev, "write 0x%04x failed: %d\n", reg, ret);
	return ret < 0 ? ret : -EIO;
}

static int sc450ai_read(struct sc450ai *sensor, u16 reg, u8 *val)
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

static int sc450ai_write_table(struct sc450ai *sensor)
{
	unsigned int i;
	int ret;

	for (i = 0; i < sensor->mode->num_regs; i++) {
		ret = sc450ai_write(sensor, sensor->mode->regs[i].addr,
				    sensor->mode->regs[i].val);
		if (ret)
			return ret;
		/* Software reset at the head of every SmartSens table. */
		if (sensor->mode->regs[i].addr == 0x0103)
			fsleep(10000);
	}
	return 0;
}

static int sc450ai_set_exposure(struct sc450ai *sensor, u32 exp)
{
	int ret;

	ret = sc450ai_write(sensor, 0x3e00, (exp >> 12) & 0x0f);
	if (ret)
		return ret;
	ret = sc450ai_write(sensor, 0x3e01, (exp >> 4) & 0xff);
	if (ret)
		return ret;
	return sc450ai_write(sensor, 0x3e02, (exp & 0x0f) << 4);
}

static int sc450ai_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc450ai *sensor = container_of(ctrl->handler, struct sc450ai,
					      ctrl_handler);

	if (!sensor->streaming && ctrl->id == V4L2_CID_EXPOSURE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		return sc450ai_set_exposure(sensor, ctrl->val);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops sc450ai_ctrl_ops = {
	.s_ctrl = sc450ai_s_ctrl,
};

static int sc450ai_power(struct sc450ai *sensor, bool on)
{
	int ret;

	if (!on) {
		gpiod_set_value_cansleep(sensor->pwdn_gpio, 0);
		gpiod_set_value_cansleep(sensor->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
		clk_disable_unprepare(sensor->xclk);
		return 0;
	}

	ret = clk_set_rate(sensor->xclk, SC450AI_XCLK_HZ);
	if (ret)
		dev_warn(&sensor->client->dev,
			 "xclk set_rate(%u) failed: %d, rate is %lu\n",
			 SC450AI_XCLK_HZ, ret, clk_get_rate(sensor->xclk));

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
	/* 8192 XCLK cycles, plus the Rockchip driver's extra margin. */
	fsleep(16000);
	return 0;
}

static int sc450ai_identify(struct sc450ai *sensor)
{
	u8 hi, lo;
	u16 id;
	int ret;

	ret = sc450ai_read(sensor, SC450AI_REG_CHIP_ID, &hi);
	if (ret)
		return ret;
	ret = sc450ai_read(sensor, SC450AI_REG_CHIP_ID + 1, &lo);
	if (ret)
		return ret;
	id = ((u16)hi << 8) | lo;
	if (id != SC450AI_CHIP_ID) {
		dev_err(&sensor->client->dev,
			"chip id 0x%04x is not SC450AI (0x%04x) at i2c 0x%02x\n",
			id, SC450AI_CHIP_ID, sensor->client->addr);
		return -ENODEV;
	}
	dev_info(&sensor->client->dev,
		 "SC450AI id 0x%04x, %u lanes, link %llu Hz\n",
		 id, sensor->mode->lanes, sensor->mode->link_freq);
	return 0;
}

static int sc450ai_start(struct sc450ai *sensor)
{
	int ret;

	ret = sc450ai_write_table(sensor);
	if (ret)
		return ret;
	ret = __v4l2_ctrl_handler_setup(&sensor->ctrl_handler);
	if (ret)
		return ret;
	return sc450ai_write(sensor, SC450AI_REG_CTRL_MODE, SC450AI_MODE_STREAMING);
}

static int sc450ai_stop(struct sc450ai *sensor)
{
	return sc450ai_write(sensor, SC450AI_REG_CTRL_MODE, SC450AI_MODE_STANDBY);
}

static int sc450ai_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sc450ai *sensor = to_sc450ai(sd);
	int ret;

	mutex_lock(&sensor->lock);
	if (enable == sensor->streaming) {
		ret = 0;
		goto out;
	}
	if (enable)
		ret = sc450ai_start(sensor);
	else
		ret = sc450ai_stop(sensor);
	if (!ret)
		sensor->streaming = enable;
out:
	mutex_unlock(&sensor->lock);
	return ret;
}

static const struct v4l2_mbus_framefmt sc450ai_fmt = {
	.width = SC450AI_WIDTH,
	.height = SC450AI_HEIGHT,
	.code = MEDIA_BUS_FMT_SBGGR10_1X10,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_RAW,
	.ycbcr_enc = V4L2_YCBCR_ENC_601,
	.quantization = V4L2_QUANTIZATION_FULL_RANGE,
	.xfer_func = V4L2_XFER_FUNC_NONE,
};

static int sc450ai_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	*v4l2_subdev_state_get_format(state, 0) = sc450ai_fmt;
	return 0;
}

static int sc450ai_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	return 0;
}

static int sc450ai_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;
	fse->min_width = fse->max_width = SC450AI_WIDTH;
	fse->min_height = fse->max_height = SC450AI_HEIGHT;
	return 0;
}

static int sc450ai_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_state_get_format(state, fmt->pad);
	return 0;
}

static int sc450ai_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *fmt)
{
	*v4l2_subdev_state_get_format(state, fmt->pad) = sc450ai_fmt;
	fmt->format = sc450ai_fmt;
	return 0;
}

static int sc450ai_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_selection *sel)
{
	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_CROP_BOUNDS &&
	    sel->target != V4L2_SEL_TGT_CROP_DEFAULT)
		return -EINVAL;
	sel->r.left = 0;
	sel->r.top = 0;
	sel->r.width = SC450AI_WIDTH;
	sel->r.height = SC450AI_HEIGHT;
	return 0;
}

static const struct v4l2_subdev_video_ops sc450ai_video_ops = {
	.s_stream = sc450ai_s_stream,
};

static const struct v4l2_subdev_pad_ops sc450ai_pad_ops = {
	.enum_mbus_code = sc450ai_enum_mbus_code,
	.enum_frame_size = sc450ai_enum_frame_size,
	.get_fmt = sc450ai_get_fmt,
	.set_fmt = sc450ai_set_fmt,
	.get_selection = sc450ai_get_selection,
};

static const struct v4l2_subdev_internal_ops sc450ai_internal_ops = {
	.init_state = sc450ai_init_state,
};

static const struct v4l2_subdev_ops sc450ai_subdev_ops = {
	.video = &sc450ai_video_ops,
	.pad = &sc450ai_pad_ops,
};

static int sc450ai_init_controls(struct sc450ai *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;
	const struct sc450ai_mode *mode = sensor->mode;
	const s64 *link_menu = mode->lanes == 4 ? sc450ai_link_freq_4lane
						: sc450ai_link_freq_2lane;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 6);
	if (ret)
		return ret;

	v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ, 0, 0, link_menu);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE,
			  mode->pixel_rate, mode->pixel_rate, 1, mode->pixel_rate);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_HBLANK,
			  mode->hblank, mode->hblank, 1, mode->hblank);
	v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_VBLANK,
			  mode->vblank, mode->vblank, 1, mode->vblank);
	v4l2_ctrl_new_std(hdl, &sc450ai_ctrl_ops, V4L2_CID_EXPOSURE,
			  1, 3120 - 8, 1, 0x80);

	if (hdl->error)
		return hdl->error;

	sensor->sd.ctrl_handler = hdl;
	return 0;
}

static const struct sc450ai_mode *sc450ai_mode_for_lanes(unsigned int lanes)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sc450ai_modes); i++) {
		if (sc450ai_modes[i].lanes == lanes)
			return &sc450ai_modes[i];
	}
	return NULL;
}

static int sc450ai_parse_endpoint(struct sc450ai *sensor)
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

	sensor->mode = sc450ai_mode_for_lanes(lanes);
	if (!sensor->mode)
		return dev_err_probe(dev, -EINVAL,
				     "SC450AI has no %u-lane mode\n", lanes);
	if (link && link != sensor->mode->link_freq)
		return dev_err_probe(dev, -EINVAL,
				     "link frequency %llu does not match the %u-lane table (%llu)\n",
				     link, lanes, sensor->mode->link_freq);
	return 0;
}

static int sc450ai_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sc450ai *sensor;
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
		return dev_err_probe(dev, PTR_ERR(sensor->reset_gpio), "reset gpio\n");
	sensor->pwdn_gpio = devm_gpiod_get_optional(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->pwdn_gpio))
		return dev_err_probe(dev, PTR_ERR(sensor->pwdn_gpio), "pwdn gpio\n");

	ret = sc450ai_parse_endpoint(sensor);
	if (ret)
		return ret;

	v4l2_i2c_subdev_init(&sensor->sd, client, &sc450ai_subdev_ops);
	sensor->sd.internal_ops = &sc450ai_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		return ret;

	ret = sc450ai_init_controls(sensor);
	if (ret)
		goto err_entity;

	sensor->sd.state_lock = &sensor->lock;
	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_ctrls;

	ret = sc450ai_power(sensor, true);
	if (ret)
		goto err_subdev;
	ret = sc450ai_identify(sensor);
	if (ret)
		goto err_power;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_power;

	return 0;

err_power:
	sc450ai_power(sensor, false);
err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
	return ret;
}

static void sc450ai_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc450ai *sensor = to_sc450ai(sd);

	v4l2_async_unregister_subdev(sd);
	sc450ai_power(sensor, false);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id sc450ai_of_match[] = {
	{ .compatible = "smartsens,sc450ai" },
	{ }
};
MODULE_DEVICE_TABLE(of, sc450ai_of_match);

static const struct i2c_device_id sc450ai_id[] = {
	{ "sc450ai" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sc450ai_id);

static struct i2c_driver sc450ai_i2c_driver = {
	.driver = {
		.name = "sc450ai",
		.of_match_table = sc450ai_of_match,
	},
	.probe = sc450ai_probe,
	.remove = sc450ai_remove,
	.id_table = sc450ai_id,
};
module_i2c_driver(sc450ai_i2c_driver);

MODULE_DESCRIPTION("SmartSens SC450AI CSI-2 sensor driver");
MODULE_LICENSE("GPL");
