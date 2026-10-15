// SPDX-License-Identifier: GPL-2.0-only
/* Samsung EA8061S AMS498QV01 540x960 panel used by Samsung J2. */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct ea8061s_ams498qv01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;
};

static inline struct ea8061s_ams498qv01 *to_ea8061s(struct drm_panel *panel)
{
	return container_of(panel, struct ea8061s_ams498qv01, panel);
}

static void ea8061s_reset(struct ea8061s_ams498qv01 *ctx)
{
	/* Downstream panelrst sequence is physical high/low/high, 20ms each. */
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static int ea8061s_init(struct ea8061s_ams498qv01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	/* Commands and waits copied from dsi_panel_EA8061S_AMS498QV01. */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba, 0xc4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x00, 0x00, 0x00, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x19, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int ea8061s_enable(struct drm_panel *panel)
{
	struct ea8061s_ams498qv01 *ctx = to_ea8061s(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	return dsi_ctx.accum_err;
}

static int ea8061s_disable(struct drm_panel *panel)
{
	struct ea8061s_ams498qv01 *ctx = to_ea8061s(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	return dsi_ctx.accum_err;
}

static int ea8061s_prepare(struct drm_panel *panel)
{
	struct ea8061s_ams498qv01 *ctx = to_ea8061s(panel);
	int ret;

	/* GPIO64 is the downstream fixed_reg_gpio64 LCD enable. */
	gpiod_set_value_cansleep(ctx->enable_gpio, 1);
	usleep_range(100, 200);
	ea8061s_reset(ctx);
	/* Downstream panelrst supply specifies 150ms post-on settling. */
	msleep(150);

	ret = ea8061s_init(ctx);
	if (ret)
		dev_err(&ctx->dsi->dev, "Panel init commands failed: %d\n", ret);

	return ret;
}

static int ea8061s_unprepare(struct drm_panel *panel)
{
	struct ea8061s_ams498qv01 *ctx = to_ea8061s(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	int ret;

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);
	ret = dsi_ctx.accum_err;

	/* Downstream panelrst_disable drives physical reset low. */
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	gpiod_set_value_cansleep(ctx->enable_gpio, 0);
	return ret;
}

static const struct drm_display_mode ea8061s_mode = {
	.clock = 41577,
	.hdisplay = 540,
	.hsync_start = 540 + 126,
	.hsync_end = 540 + 126 + 1,
	.htotal = 540 + 126 + 1 + 43,
	.vdisplay = 960,
	.vsync_start = 960 + 4,
	.vsync_end = 960 + 4 + 1,
	.vtotal = 960 + 4 + 1 + 11,
	.width_mm = 62,
	.height_mm = 110,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int ea8061s_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &ea8061s_mode);
}

static const struct drm_panel_funcs ea8061s_panel_funcs = {
	.prepare = ea8061s_prepare,
	.enable = ea8061s_enable,
	.disable = ea8061s_disable,
	.unprepare = ea8061s_unprepare,
	.get_modes = ea8061s_get_modes,
};

static int ea8061s_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ea8061s_ams498qv01 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct ea8061s_ams498qv01, panel,
				   &ea8061s_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	/* GPIO64 is already asserted by the bootloader; avoid a probe-time pulse. */
	ctx->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpio),
				     "Failed to get LCD enable GPIO\n");

	/* Active-low descriptor: logical low is the downstream physical high. */
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get panel reset GPIO\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);
	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET;

	drm_panel_add(&ctx->panel);
	ret = mipi_dsi_attach(dsi);
	if (ret) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void ea8061s_remove(struct mipi_dsi_device *dsi)
{
	struct ea8061s_ams498qv01 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ea8061s_of_match[] = {
	{ .compatible = "samsung,ea8061s-ams498qv01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ea8061s_of_match);

static struct mipi_dsi_driver ea8061s_driver = {
	.probe = ea8061s_probe,
	.remove = ea8061s_remove,
	.driver = {
		.name = "panel-samsung-ea8061s-ams498qv01",
		.of_match_table = ea8061s_of_match,
	},
};
module_mipi_dsi_driver(ea8061s_driver);

MODULE_DESCRIPTION("DRM driver for Samsung J2 EA8061S AMS498QV01 panel");
MODULE_LICENSE("GPL");
