/* SPDX-License-Identifier: GPL-2.0-only */
/* Digilent HDMI encoder driver for PYNQ-Z2 - Minimal version
 * Ported to kernel 6.18.10 (2026)
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Digilent Inc.");
MODULE_DESCRIPTION("Digilent HDMI encoder driver for PYNQ-Z2 (minimal)");

struct digilent_hdmi {
	struct device *dev;
	struct clk *clk;
	struct i2c_adapter *i2c;
	unsigned int fmax;
};

static int digilent_hdmi_probe(struct platform_device *pdev)
{
	struct digilent_hdmi *hdmi;
	struct device_node *i2c_node;
	int ret;

	dev_info(&pdev->dev, "probe: starting\n");

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = &pdev->dev;

	/* Get clock reference */
	hdmi->clk = devm_clk_get(&pdev->dev, "clk");
	if (IS_ERR(hdmi->clk)) {
		dev_warn(&pdev->dev, "Clock not found (optional)\n");
		hdmi->clk = NULL;
	} else {
		dev_info(&pdev->dev, "Clock found\n");
	}

	/* Get max frequency property */
	ret = of_property_read_u32(pdev->dev.of_node, "digilent,fmax", &hdmi->fmax);
	if (ret) {
		hdmi->fmax = 150000;
		dev_info(&pdev->dev, "Using default fmax=%u KHz\n", hdmi->fmax);
	} else {
		dev_info(&pdev->dev, "fmax=%u KHz\n", hdmi->fmax);
	}

	/* Get I2C adapter for EDID */
	i2c_node = of_parse_phandle(pdev->dev.of_node, "digilent,edid-i2c", 0);
	if (i2c_node) {
		hdmi->i2c = of_find_i2c_adapter_by_node(i2c_node);
		of_node_put(i2c_node);
		if (!hdmi->i2c) {
			dev_warn(&pdev->dev, "I2C adapter not found (EDID unavailable)\n");
		} else {
			dev_info(&pdev->dev, "I2C adapter found\n");
		}
	}

	platform_set_drvdata(pdev, hdmi);
	dev_info(&pdev->dev, "probe: SUCCESS\n");

	return 0;
}

static void digilent_hdmi_remove(struct platform_device *pdev)
{
	struct digilent_hdmi *hdmi = platform_get_drvdata(pdev);

	if (hdmi->clk)
		clk_disable_unprepare(hdmi->clk);

	if (hdmi->i2c)
		i2c_put_adapter(hdmi->i2c);

	dev_info(&pdev->dev, "remove: done\n");
}

static const struct of_device_id digilent_hdmi_of_match[] = {
	{.compatible = "digilent,hdmi"},
	{}
};
MODULE_DEVICE_TABLE(of, digilent_hdmi_of_match);

static struct platform_driver digilent_hdmi_driver = {
	.driver = {
		.name = "digilent-hdmi",
		.of_match_table = digilent_hdmi_of_match,
	},
	.probe = digilent_hdmi_probe,
	.remove = digilent_hdmi_remove,
};

module_platform_driver(digilent_hdmi_driver);
