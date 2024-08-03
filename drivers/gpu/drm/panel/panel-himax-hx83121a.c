// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree.
 * Copyright (c) 2024 Luca Weiss <luca.weiss@fairphone.com>
 * Copyright (c) 2024 bigsaltyfishes <bigsaltyfishes@gmail.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

/* Manufacturer specific DSI commands */
#define HX83121A_SETDISP 0xb2 
#define HX83121A_SETEXTC 0xb9
#define HX83121A_SETBANK 0xbd
#define HX83121A_SETCLOCK 0xcb
#define HX83121A_UNKNOWN1 0xcd

struct hx83121a_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[3];
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
};

static inline struct hx83121a_panel *to_hx83121a_panel(struct drm_panel *panel)
{
	return container_of(panel, struct hx83121a_panel, panel);
}

static void hx83121a_reset(struct hx83121a_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(50);
}

static int hx83121a_on(struct hx83121a_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	mipi_dsi_dcs_write_seq(dsi, HX83121A_SETEXTC, 0xb9, 0x83, 0x12, 0x1a, 0x55, 0x00);
	mipi_dsi_dcs_write_seq(dsi, HX83121A_SETBANK, 0x81);
	mipi_dsi_dcs_write_seq(dsi, HX83121A_SETCLOCK, 0x1f, 0x55, 0x03, 0x28, 0x0d, 0x08, 0x0a);
	msleep(120);
	mipi_dsi_dcs_write_seq(dsi, HX83121A_SETBANK, 0x00);
	mipi_dsi_dcs_write_seq(dsi, HX83121A_UNKNOWN1, 0x81, 0x00, 0x3d, 0x77, 0x18, 0x7a, 0x00);
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	mipi_dsi_dcs_write_seq(dsi, HX83121A_SETDISP, 0x00, 0x6a, 0x40, 0x00, 0x00, 0x14, 0x6e, 0x40, 0x73, 0x02, 0x80, 0x21, 0x21, 0x00, 0x00, 0xf0);
	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int hx83121a_disable(struct drm_panel *panel)
{
	struct hx83121a_panel *ctx = to_hx83121a_panel(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(20);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(128);

	return 0;
}

static int hx83121a_prepare(struct drm_panel *panel)
{
	struct hx83121a_panel *ctx = to_hx83121a_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	hx83121a_reset(ctx);

	ret = hx83121a_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);

	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_compression_mode(ctx->dsi, true);
	if (ret < 0) {
		dev_err(dev, "failed to enable compression mode: %d\n", ret);
		return ret;
	}

	msleep(50); /* TODO: Is this panel-dependent? */

	return 0;
}

static int hx83121a_unprepare(struct drm_panel *panel)
{
	struct hx83121a_panel *ctx = to_hx83121a_panel(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode hx83121a_mode = {
    .clock = (1600 + 60 + 20 + 40) * (2560 + 112 + 4 + 18) * 60 / 1000, // Adjusted based on Horizontal and Vertical timings
    .hdisplay = 1600,
    .hsync_start = 1600 + 60, // HorizontalActive + HorizontalFrontPorch
    .hsync_end = 1600 + 60 + 20, // HorizontalActive + HorizontalFrontPorch + HorizontalSyncPulse
    .htotal = 1600 + 60 + 20 + 40, // HorizontalActive + HorizontalFrontPorch + HorizontalSyncPulse + HorizontalBackPorch
    .vdisplay = 2560,
    .vsync_start = 2560 + 112, // VerticalActive + VerticalFrontPorch
    .vsync_end = 2560 + 112 + 4, // VerticalActive + VerticalFrontPorch + VerticalSyncPulse
    .vtotal = 2560 + 112 + 4 + 18, // VerticalActive + VerticalFrontPorch + VerticalSyncPulse + VerticalBackPorch
    .width_mm = 265, // Converted from HorizontalScreenSizeMM (0x109) which is 265 in decimal
    .height_mm = 166, // Converted from VerticalScreenSizeMM (0xA6) which is 166 in decimal
    .type = DRM_MODE_TYPE_DRIVER,
};

static int hx83121a_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &hx83121a_mode);
}

static const struct drm_panel_funcs hx83121a_panel_funcs = {
	.prepare = hx83121a_prepare,
	.unprepare = hx83121a_unprepare,
	.disable = hx83121a_disable,
	.get_modes = hx83121a_get_modes,
};

static int hx83121a_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct hx83121a_panel *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vdd1";
	ctx->supplies[1].supply = "vsn";
	ctx->supplies[2].supply = "vsp";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;

	drm_panel_init(&ctx->panel, dev, &hx83121a_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;

	/* TODO: Pass slice_per_pkt = 1 */
	ctx->dsc.slice_height = 40;
	ctx->dsc.slice_width = 1600;
	/*
	 * TODO: hdisplay should be read from the selected mode once
	 * it is passed back to drm_panel (in prepare?)
	 */
	WARN_ON(1600 % ctx->dsc.slice_width);
	ctx->dsc.slice_count = 1600 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 8;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	ctx->dsc.block_pred_enable = true;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void hx83121a_remove(struct mipi_dsi_device *dsi)
{
	struct hx83121a_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id hx83121a_of_match[] = {
	{ .compatible = "csot,pnc357db14" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hx83121a_of_match);

static struct mipi_dsi_driver hx83121a_driver = {
	.probe = hx83121a_probe,
	.remove = hx83121a_remove,
	.driver = {
		.name = "panel-himax-hx83121a",
		.of_match_table = hx83121a_of_match,
	},
};
module_mipi_dsi_driver(hx83121a_driver);

MODULE_DESCRIPTION("DRM driver for hx83121a-equipped DSI panels");
MODULE_LICENSE("GPL");
