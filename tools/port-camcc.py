#!/usr/bin/env python3
"""Convert the downstream camcc-lahaina.c into a mainline camcc-sm8350.c.

usage: port-camcc.py <camcc-lahaina.c> <out.c>

Drops the downstream vdd voting, maps the 5LPE PLL types onto mainline's
lucid/zonda ops, adds the GDSCs (downstream declares them in DT) and a
mainline probe.
"""
import re
import sys


def drop_block(s, start_re):
    """Remove every '<start_re> { ... },' block with balanced braces."""
    out, i = [], 0
    for m in re.finditer(start_re, s):
        if m.start() < i:
            continue
        j = s.index("{", m.end() - 1)
        depth = 0
        k = j
        while True:
            if s[k] == "{":
                depth += 1
            elif s[k] == "}":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        end = k + 1
        if s[end:end + 1] == ",":
            end += 1
        # take the whole line(s)
        ls = s.rfind("\n", 0, m.start()) + 1
        le = s.find("\n", end)
        out.append(s[i:ls])
        i = le + 1
    out.append(s[i:])
    return "".join(out)


GDSCS = """
static struct gdsc titan_top_gdsc = {
	.gdscr = 0xc120,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "titan_top_gdsc",
	},
	.flags = RETAIN_FF_ENABLE,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc bps_gdsc = {
	.gdscr = 0x7004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "bps_gdsc",
	},
	.flags = HW_CTRL | RETAIN_FF_ENABLE,
	.parent = &titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc ipe_0_gdsc = {
	.gdscr = 0x8004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ipe_0_gdsc",
	},
	.flags = HW_CTRL | RETAIN_FF_ENABLE,
	.parent = &titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc sbi_gdsc = {
	.gdscr = 0x9004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "sbi_gdsc",
	},
	.flags = RETAIN_FF_ENABLE,
	.parent = &titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc ife_0_gdsc = {
	.gdscr = 0xa004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ife_0_gdsc",
	},
	.flags = 0,
	.parent = &titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc ife_1_gdsc = {
	.gdscr = 0xb004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ife_1_gdsc",
	},
	.flags = 0,
	.parent = &titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc ife_2_gdsc = {
	.gdscr = 0xb070,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "ife_2_gdsc",
	},
	.flags = 0,
	.parent = &titan_top_gdsc.pd,
	.pwrsts = PWRSTS_OFF_ON,
};

static struct gdsc *cam_cc_sm8350_gdscs[] = {
	[BPS_GDSC] = &bps_gdsc,
	[IPE_0_GDSC] = &ipe_0_gdsc,
	[SBI_GDSC] = &sbi_gdsc,
	[IFE_0_GDSC] = &ife_0_gdsc,
	[IFE_1_GDSC] = &ife_1_gdsc,
	[IFE_2_GDSC] = &ife_2_gdsc,
	[TITAN_TOP_GDSC] = &titan_top_gdsc,
};
"""

PROBE = """
static void cam_cc_sm8350_clk_regs_configure(struct device *dev, struct regmap *regmap)
{
	clk_lucid_5lpe_pll_configure(&cam_cc_pll0, regmap, &cam_cc_pll0_config);
	clk_lucid_5lpe_pll_configure(&cam_cc_pll1, regmap, &cam_cc_pll1_config);
	clk_zonda_pll_configure(&cam_cc_pll2, regmap, &cam_cc_pll2_config_sm8350_v2);
	clk_lucid_5lpe_pll_configure(&cam_cc_pll3, regmap, &cam_cc_pll3_config);
	clk_lucid_5lpe_pll_configure(&cam_cc_pll4, regmap, &cam_cc_pll4_config);
	clk_lucid_5lpe_pll_configure(&cam_cc_pll5, regmap, &cam_cc_pll5_config);
	clk_lucid_5lpe_pll_configure(&cam_cc_pll6, regmap, &cam_cc_pll6_config);
}

static const u32 cam_cc_sm8350_critical_cbcrs[] = {
	0xc148, /* CAM_CC_GDSC_CLK */
};

static const struct qcom_cc_driver_data cam_cc_sm8350_driver_data = {
	.clk_cbcrs = cam_cc_sm8350_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(cam_cc_sm8350_critical_cbcrs),
	.clk_regs_configure = cam_cc_sm8350_clk_regs_configure,
};

static const struct of_device_id cam_cc_sm8350_match_table[] = {
	{ .compatible = "qcom,sm8350-camcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, cam_cc_sm8350_match_table);

static int cam_cc_sm8350_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &cam_cc_sm8350_desc);
}

static struct platform_driver cam_cc_sm8350_driver = {
	.probe = cam_cc_sm8350_probe,
	.driver = {
		.name = "camcc-sm8350",
		.of_match_table = cam_cc_sm8350_match_table,
	},
};

module_platform_driver(cam_cc_sm8350_driver);

MODULE_DESCRIPTION("QTI CAMCC SM8350 Driver");
MODULE_LICENSE("GPL");
"""


def main():
    s = open(sys.argv[1]).read()

    s = drop_block(s, r"\.clkr\.vdd_data = \{")
    s = drop_block(s, r"\.vdd_data = \{")
    s = re.sub(r'#include "vdd-level.h"\n', "", s)
    s = re.sub(r"static DEFINE_VDD_REGULATORS\(.*\);\n", "", s)
    s = re.sub(r"static struct clk_vdd_class \*cam_cc_lahaina_regulators\[\] = \{.*?\};\n\n", "", s, flags=re.S)
    # downstream parks these RCGs on XO when off; mainline does that with
    # the shared ops (the GDSCs need a running clock to power back up)
    def safe(m):
        blk = m.group(0)
        if ".enable_safe_config = true," in blk:
            blk = blk.replace(".ops = &clk_rcg2_ops,", ".ops = &clk_rcg2_shared_ops,")
        return blk
    s = re.sub(r"static struct clk_rcg2 \w+ = \{.*?\n\};", safe, s, flags=re.S)
    s = re.sub(r"\t*\.enable_safe_config = true,\n", "", s)
    s = re.sub(r"\t\.flags = HW_CLK_CTRL_MODE,\n", "", s)
    s = re.sub(r"\t*\.clk_regulators = .*\n\t*\.num_clk_regulators = .*\n", "", s)

    s = s.replace("CLK_ALPHA_PLL_TYPE_LUCID_5LPE", "CLK_ALPHA_PLL_TYPE_LUCID")
    s = s.replace("CLK_ALPHA_PLL_TYPE_ZONDA_5LPE", "CLK_ALPHA_PLL_TYPE_ZONDA")
    s = s.replace("clk_alpha_pll_zonda_5lpe_ops", "clk_alpha_pll_zonda_ops")
    s = s.replace("qcom,camcc-lahaina.h", "qcom,sm8350-camcc.h")

    # everything from the match table on is replaced by a mainline probe
    s = s[:s.index("static const struct of_device_id cam_cc_lahaina_match_table")]
    s = s.replace("lahaina", "sm8350")
    s = s.replace("\t.clks = cam_cc_sm8350_clocks,",
                  "\t.clks = cam_cc_sm8350_clocks,")
    s = s.replace("\t.num_resets = ARRAY_SIZE(cam_cc_sm8350_resets),\n",
                  "\t.num_resets = ARRAY_SIZE(cam_cc_sm8350_resets),\n"
                  "\t.gdscs = cam_cc_sm8350_gdscs,\n"
                  "\t.num_gdscs = ARRAY_SIZE(cam_cc_sm8350_gdscs),\n"
                  "\t.use_rpm = true,\n"
                  "\t.driver_data = &cam_cc_sm8350_driver_data,\n")
    s = s.replace("static struct qcom_cc_desc cam_cc_sm8350_desc",
                  "static const struct qcom_cc_desc cam_cc_sm8350_desc")

    # gdscs before the desc
    i = s.index("static const struct qcom_reset_map cam_cc_sm8350_resets")
    s = s[:i] + GDSCS.lstrip("\n") + "\n" + s[i:]
    j = PROBE.index("static const struct of_device_id")
    regs_cfg, probe = PROBE[:j], PROBE[j:]
    k = s.index("static const struct qcom_cc_desc cam_cc_sm8350_desc")
    s = s[:k] + regs_cfg.lstrip("\n") + s[k:]

    s = s.replace('#include "reset.h"\n', '#include "gdsc.h"\n#include "reset.h"\n')
    s = s.replace("#include <linux/of_device.h>\n", "#include <linux/mod_devicetable.h>\n#include <linux/platform_device.h>\n")
    s = s.rstrip() + "\n\n" + probe
    open(sys.argv[2], "w").write(s)


if __name__ == "__main__":
    main()
