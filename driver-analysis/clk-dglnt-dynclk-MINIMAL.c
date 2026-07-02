/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal version - loads successfully, implements core functionality */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define OFST_DISPLAY_CTRL 0x0
#define OFST_DISPLAY_STATUS 0x4

struct dglnt_dynclk {
	void __iomem *base;
	struct clk_hw clk_hw;
	unsigned long freq;
};

static int dglnt_dynclk_enable(struct clk_hw *clk_hw)
{
	struct dglnt_dynclk *dglnt = container_of(clk_hw, struct dglnt_dynclk, clk_hw);
	unsigned int clock_state;

	if (dglnt->freq) {
		writel(1, dglnt->base + OFST_DISPLAY_CTRL);
		do {
			clock_state = readl(dglnt->base + OFST_DISPLAY_STATUS);
		} while (!clock_state);
	}
	return 0;
}

static void dglnt_dynclk_disable(struct clk_hw *clk_hw)
{
	struct dglnt_dynclk *dglnt = container_of(clk_hw, struct dglnt_dynclk, clk_hw);
	writel(0, dglnt->base + OFST_DISPLAY_CTRL);
}

static unsigned long dglnt_dynclk_recalc_rate(struct clk_hw *clk_hw,
					      unsigned long parent_rate)
{
	struct dglnt_dynclk *dglnt = container_of(clk_hw, struct dglnt_dynclk, clk_hw);
	return dglnt->freq;
}

static const struct clk_ops dglnt_dynclk_ops = {
	.enable = dglnt_dynclk_enable,
	.disable = dglnt_dynclk_disable,
	.recalc_rate = dglnt_dynclk_recalc_rate,
};

static int dglnt_dynclk_probe(struct platform_device *pdev)
{
	struct dglnt_dynclk *dglnt;
	struct clk_init_data init = {};
	struct resource *mem;
	int ret;

	dev_info(&pdev->dev, "probe: starting\n");

	if (!pdev->dev.of_node)
		return -ENODEV;

	dglnt = devm_kzalloc(&pdev->dev, sizeof(*dglnt), GFP_KERNEL);
	if (!dglnt)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dglnt->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(dglnt->base))
		return PTR_ERR(dglnt->base);

	dev_info(&pdev->dev, "probe: base=%pK\n", dglnt->base);

	dglnt->freq = 100000000;  /* Default: 100 MHz */

	init.name = "axi_dynclk";
	init.ops = &dglnt_dynclk_ops;
	init.flags = CLK_IGNORE_UNUSED;
	init.parent_names = NULL;
	init.num_parents = 0;

	dglnt->clk_hw.init = &init;

	dev_info(&pdev->dev, "probe: registering clock\n");
	ret = devm_clk_hw_register(&pdev->dev, &dglnt->clk_hw);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register: %d\n", ret);
		return ret;
	}

	dev_info(&pdev->dev, "probe: adding provider\n");
	ret = of_clk_add_hw_provider(pdev->dev.of_node, of_clk_hw_simple_get,
				     &dglnt->clk_hw);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add provider: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, dglnt);
	dev_info(&pdev->dev, "probe: SUCCESS\n");

	return 0;
}

static void dglnt_dynclk_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);
}

static const struct of_device_id dglnt_dynclk_ids[] = {
	{.compatible = "dglnt,axi-dynclk"},
	{}
};
MODULE_DEVICE_TABLE(of, dglnt_dynclk_ids);

static struct platform_driver dglnt_dynclk_driver = {
	.driver = {
		.name = "dglnt-dynclk",
		.of_match_table = dglnt_dynclk_ids,
	},
	.probe = dglnt_dynclk_probe,
	.remove = dglnt_dynclk_remove,
};

module_platform_driver(dglnt_dynclk_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Digilent / Ported to 6.18");
MODULE_DESCRIPTION("Digilent Dynamic Clock Driver (Minimal)");
