#!/usr/bin/env python3
"""Generate drivers/gpu/drm/panel/panel-samsung-amb670yf01.c from the
downstream OnePlus 9 Pro panel node (one timing), replaying its on-command
sequence with the mainline mipi_dsi_*_multi helpers.

usage: gen-amb670yf01.py <panel-node.dts> <timing-name> > panel.c
"""
import importlib.util
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("ext", os.path.join(here, "dsi-panel-extract.py"))
ext = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ext)


def hexs(b):
    return ", ".join("0x%02x" % x for x in b)


def wait(ms):
    if not ms:
        return []
    if ms < 20:
        return ["\tmipi_dsi_usleep_range(&dsi_ctx, %d, %d);" % (ms * 1000, ms * 1000 + 1000)]
    return ["\tmipi_dsi_msleep(&dsi_ctx, %d);" % ms]


def main():
    dts = open(sys.argv[1]).read()
    timing = ext.node_text(dts, sys.argv[2])
    g = lambda k: ext.u32s(ext.prop(timing, "qcom,mdss-dsi-" + k))[0]
    d = lambda k: ext.u32s(ext.prop(timing, "qcom,mdss-dsc-" + k))[0]
    w, h = g("panel-width"), g("panel-height")
    hfp, hbp, hpw = g("h-front-porch"), g("h-back-porch"), g("h-pulse-width")
    vfp, vbp, vpw = g("v-front-porch"), g("v-back-porch"), g("v-pulse-width")
    fps = g("panel-framerate")
    sw, sh, bpc, bpp = d("slice-width"), d("slice-height"), d("bit-per-component"), d("bit-per-pixel")
    cmds = ext.decode(ext.prop(timing, "qcom,mdss-dsi-on-command"))

    on = []
    for dtype, _last, ms, p in cmds:
        if p and p[0] == 0x9e:
            # Samsung takes the PPS via DCS 0x9e: send the one matching our DSC config
            on.append("\tamb670yf01_send_pps(ctx, &dsi_ctx);")
        elif p == [0x11]:
            on.append("\tmipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);")
        elif p[:1] == [0x35]:
            on.append("\tmipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);")
        elif dtype in (0x05, 0x15, 0x39):
            on.append("\tmipi_dsi_dcs_write_seq_multi(&dsi_ctx, %s);" % hexs(p))
        else:
            raise SystemExit("unhandled dtype %#x" % dtype)
        on += wait(ms)

    print(TEMPLATE % dict(
        timing=sys.argv[2], w=w, h=h, hfp=hfp, hbp=hbp, hpw=hpw, vfp=vfp, vbp=vbp, vpw=vpw,
        fps=fps, sw=sw, sh=sh, bpc=bpc, bpp=bpp, on="\n".join(on)))


TEMPLATE = r'''// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung AMB670YF01 6.7" DSC command mode AMOLED panel (OnePlus 9 Pro).
 *
 * Init sequence generated from the vendor device tree (timing %(timing)s)
 * with tools/gen-amb670yf01.py.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct amb670yf01 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data amb670yf01_supplies[] = {
	{ .supply = "vddio" },	/* 1.8V */
	{ .supply = "vdd" },	/* 3.0V */
	{ .supply = "avdd" },	/* 5.5V */
};

static inline struct amb670yf01 *to_amb670yf01(struct drm_panel *panel)
{
	return container_of(panel, struct amb670yf01, panel);
}

static void amb670yf01_reset(struct amb670yf01 *ctx)
{
	/* vendor qcom,mdss-dsi-reset-sequence: high 10ms, low 1ms, high 10ms */
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static void amb670yf01_send_pps(struct amb670yf01 *ctx,
				struct mipi_dsi_multi_context *dsi_ctx)
{
	u8 buf[1 + sizeof(struct drm_dsc_picture_parameter_set)];

	buf[0] = 0x9e;
	drm_dsc_pps_payload_pack((struct drm_dsc_picture_parameter_set *)&buf[1],
				 &ctx->dsc);
	mipi_dsi_dcs_write_buffer_multi(dsi_ctx, buf, sizeof(buf));
}

static int amb670yf01_on(struct amb670yf01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

%(on)s

	return dsi_ctx.accum_err;
}

static int amb670yf01_enable(struct drm_panel *panel)
{
	struct amb670yf01 *ctx = to_amb670yf01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	/* vendor qcom,mdss-dsi-post-on-backlight */
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int amb670yf01_disable(struct drm_panel *panel)
{
	struct amb670yf01 *ctx = to_amb670yf01(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 21);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 101);

	return dsi_ctx.accum_err;
}

static int amb670yf01_prepare(struct drm_panel *panel)
{
	struct amb670yf01 *ctx = to_amb670yf01(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(amb670yf01_supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	amb670yf01_reset(ctx);

	ret = amb670yf01_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(amb670yf01_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int amb670yf01_unprepare(struct drm_panel *panel)
{
	struct amb670yf01 *ctx = to_amb670yf01(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(amb670yf01_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode amb670yf01_mode = {
	.clock = (%(w)d + %(hfp)d + %(hpw)d + %(hbp)d) * (%(h)d + %(vfp)d + %(vpw)d + %(vbp)d) * %(fps)d / 1000,
	.hdisplay = %(w)d,
	.hsync_start = %(w)d + %(hfp)d,
	.hsync_end = %(w)d + %(hfp)d + %(hpw)d,
	.htotal = %(w)d + %(hfp)d + %(hpw)d + %(hbp)d,
	.vdisplay = %(h)d,
	.vsync_start = %(h)d + %(vfp)d,
	.vsync_end = %(h)d + %(vfp)d + %(vpw)d,
	.vtotal = %(h)d + %(vfp)d + %(vpw)d + %(vbp)d,
	.width_mm = 71,
	.height_mm = 159,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int amb670yf01_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &amb670yf01_mode);
}

static const struct drm_panel_funcs amb670yf01_panel_funcs = {
	.prepare = amb670yf01_prepare,
	.unprepare = amb670yf01_unprepare,
	.enable = amb670yf01_enable,
	.disable = amb670yf01_disable,
	.get_modes = amb670yf01_get_modes,
};

static int amb670yf01_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return ret < 0 ? ret : 0;
}

static const struct backlight_ops amb670yf01_bl_ops = {
	.update_status = amb670yf01_bl_update_status,
};

static struct backlight_device *amb670yf01_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 400,	/* vendor qcom,mdss-brightness-default-level */
		.max_brightness = 2047,	/* normal max; above is high-brightness mode */
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &amb670yf01_bl_ops, &props);
}

static int amb670yf01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct amb670yf01 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct amb670yf01, panel,
				   &amb670yf01_panel_funcs, DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(amb670yf01_supplies),
					    amb670yf01_supplies, &ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB101010;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS;

	/* vendor qcom,mdss-dsi-lp11-init */
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = amb670yf01_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	dsi->dsc = &ctx->dsc;
	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 1;
	ctx->dsc.slice_height = %(sh)d;
	ctx->dsc.slice_width = %(sw)d;
	ctx->dsc.slice_count = %(w)d / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = %(bpc)d;
	ctx->dsc.bits_per_pixel = %(bpp)d << 4;
	ctx->dsc.block_pred_enable = true;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void amb670yf01_remove(struct mipi_dsi_device *dsi)
{
	struct amb670yf01 *ctx = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id amb670yf01_of_match[] = {
	{ .compatible = "samsung,amb670yf01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, amb670yf01_of_match);

static struct mipi_dsi_driver amb670yf01_driver = {
	.probe = amb670yf01_probe,
	.remove = amb670yf01_remove,
	.driver = {
		.name = "panel-samsung-amb670yf01",
		.of_match_table = amb670yf01_of_match,
	},
};
module_mipi_dsi_driver(amb670yf01_driver);

MODULE_DESCRIPTION("DRM driver for Samsung AMB670YF01 DSC cmd mode panel (OnePlus 9 Pro)");
MODULE_LICENSE("GPL");'''


if __name__ == "__main__":
    main()
