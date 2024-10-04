// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 bigsaltyfishes <bigsaltyfishes@gmail.com>
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

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

struct hx83121a_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
};

static inline
struct hx83121a_panel *to_hx83121a_panel(struct drm_panel *panel)
{
	return container_of(panel, struct hx83121a_panel, panel);
}

static int dsi_populate_dsc_params(struct hx83121a_panel *ctx)
{
	int ret;
	struct drm_dsc_config *dsc = &ctx->dsc;

	dsc->simple_422 = 0;
	dsc->convert_rgb = 1;
	dsc->vbr_enable = 0;

	drm_dsc_set_const_params(dsc);
	drm_dsc_set_rc_buf_thresh(dsc);

	/* handle only bpp = bpc = 8, pre-SCR panels */
	ret = drm_dsc_setup_rc_params(dsc, DRM_DSC_1_1_PRE_SCR);
	if (ret < 0)
		dev_err(&ctx->dsi->dev, "failed to setup dsc params");

	dsc->initial_scale_value = drm_dsc_initial_scale_value(dsc);
	dsc->line_buf_depth = dsc->bits_per_component + 1;

	return drm_dsc_compute_rc_parameters(dsc);
}

static int hx83121a_panel_init_dsc_config(struct hx83121a_panel *ctx)
{
	ctx->dsc = (struct drm_dsc_config) {
		.dsc_version_major = 1,
		.dsc_version_minor = 1,
		.slice_height = 40,
		.slice_width = 800,
		.slice_count = 2,
		.bits_per_component = 8,
		.bits_per_pixel = 8 << 4,
		.block_pred_enable = true,

		.pic_width = 1600,
		.pic_height = 2560,
	};
	return dsi_populate_dsc_params(ctx);
}

static void hx83121a_panel_reset(struct hx83121a_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(100);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
}

static int hx83121a_panel_on(struct hx83121a_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x83, 0x12, 0x1a, 0x55, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xcb,
			       0x1f, 0x55, 0x03, 0x28, 0x0d, 0x08, 0x0a);
	msleep(120);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xcd,
			       0x81, 0x00, 0x3d, 0x77, 0x18, 0x7a, 0x00);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	mipi_dsi_dcs_write_seq(dsi, 0xb2,
			       0x00, 0x6a, 0x40, 0x00, 0x00, 0x14, 0x6e, 0x40,
			       0x73, 0x02, 0x80, 0x21, 0x21, 0x00, 0x00, 0xf0);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int hx83121a_panel_off(struct hx83121a_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(128);

	return 0;
}

static int hx83121a_panel_prepare(struct drm_panel *panel)
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

	hx83121a_panel_reset(ctx);

	ret = hx83121a_panel_on(ctx);
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

	msleep(120); /* TODO: Is this panel-dependent? */

	return 0;
}

static int hx83121a_panel_unprepare(struct drm_panel *panel)
{
	struct hx83121a_panel *ctx = to_hx83121a_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = hx83121a_panel_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode hx83121a_panel_mode = {
	.clock = (1600 + 60 + 20 + 40) * (2560 + 112 + 4 + 18) * 60 / 1000,
	.hdisplay = 1600,
	.hsync_start = 1600 + 60,
	.hsync_end = 1600 + 60 + 20,
	.htotal = 1600 + 60 + 20 + 40,
	.vdisplay = 2560,
	.vsync_start = 2560 + 112,
	.vsync_end = 2560 + 112 + 4,
	.vtotal = 2560 + 112 + 4 + 18,
	.width_mm = 166,
	.height_mm = 265,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int hx83121a_panel_get_modes(struct drm_panel *panel,
				       struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &hx83121a_panel_mode);
}

static const struct drm_panel_funcs hx83121a_panel_funcs = {
	.prepare = hx83121a_panel_prepare,
	.unprepare = hx83121a_panel_unprepare,
	.get_modes = hx83121a_panel_get_modes,
};

static int hx83121a_panel_probe(struct mipi_dsi_device *dsi)
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &hx83121a_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	hx83121a_panel_init_dsc_config(ctx);
	dsi->dsc = &ctx->dsc;
	dsi->dsc_slice_per_pkt = 2;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void hx83121a_panel_remove(struct mipi_dsi_device *dsi)
{
	struct hx83121a_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id hx83121a_panel_of_match[] = {
	{ .compatible = "csot,pnc357db14" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hx83121a_panel_of_match);

static struct mipi_dsi_driver hx83121a_panel_driver = {
	.probe = hx83121a_panel_probe,
	.remove = hx83121a_panel_remove,
	.driver = {
		.name = "panel-himax-hx83121a",
		.of_match_table = hx83121a_panel_of_match,
	},
};
module_mipi_dsi_driver(hx83121a_panel_driver);

MODULE_AUTHOR("bigsaltyfishes <bigsaltyfishes@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Himax HX83121A Equipped Panel");
MODULE_LICENSE("GPL");
