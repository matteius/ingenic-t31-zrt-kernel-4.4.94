// SPDX-License-Identifier: GPL-2.0
/*
 * JZ47xx SoCs TCU clocks driver
 * Copyright (C) 2019 Paul Cercueil <paul@crapouillou.net>
 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clockchips.h>
#include <linux/mfd/ingenic-tcu.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <dt-bindings/clock/ingenic,tcu.h>

/* 8 channels max + watchdog + OST */
#define TCU_CLK_COUNT	10

#undef pr_fmt
#define pr_fmt(fmt) "ingenic-tcu-clk: " fmt

enum tcu_clk_parent {
	TCU_PARENT_PCLK,
	TCU_PARENT_RTC,
	TCU_PARENT_EXT,
};

struct ingenic_soc_info {
	unsigned int num_channels;
	bool has_ost;
	bool has_tcu_clk;
	bool allow_missing_tcu_clk;
};

struct ingenic_tcu_clk_info {
	struct clk_init_data init_data;
	u8 gate_bit;
	u8 tcsr_reg;
};

struct ingenic_tcu_clk {
	struct clk_hw hw;
	unsigned int idx;
	struct ingenic_tcu *tcu;
	const struct ingenic_tcu_clk_info *info;
};

struct ingenic_tcu {
	const struct ingenic_soc_info *soc_info;
	void __iomem *map;  // Direct memory-mapped I/O address
	struct clk *clk;

	struct clk_hw_onecell_data *clocks;
};

static struct ingenic_tcu *ingenic_tcu;

static inline struct ingenic_tcu_clk *to_tcu_clk(struct clk_hw *hw)
{
	return container_of(hw, struct ingenic_tcu_clk, hw);
}

static int ingenic_tcu_enable(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	writel(readl(tcu_clk->tcu->map + TCU_REG_TSCR) | BIT(tcu_clk->info->gate_bit),
		   tcu_clk->tcu->map + TCU_REG_TSCR);
	return 0;
}

static void ingenic_tcu_disable(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	writel(readl(tcu_clk->tcu->map + TCU_REG_TSSR) | BIT(tcu_clk->info->gate_bit),
		   tcu_clk->tcu->map + TCU_REG_TSSR);
}

static int ingenic_tcu_is_enabled(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	return readl(tcu_clk->tcu->map + TCU_REG_TSR) & BIT(tcu_clk->info->gate_bit);
}

static bool ingenic_tcu_enable_regs(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	struct ingenic_tcu *tcu = tcu_clk->tcu;
	bool enabled = false;

	// Check if enabled and ensure that register access is allowed.
	enabled = !!ingenic_tcu_is_enabled(hw);
	writel(readl(tcu->map + TCU_REG_TSCR) | BIT(info->gate_bit), tcu->map + TCU_REG_TSCR);

	return enabled;
}

static void ingenic_tcu_disable_regs(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	struct ingenic_tcu *tcu = tcu_clk->tcu;

	writel(readl(tcu->map + TCU_REG_TSSR) | BIT(info->gate_bit), tcu->map + TCU_REG_TSSR);
}

static u8 ingenic_tcu_get_parent(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	unsigned int val;

	val = readl(tcu_clk->tcu->map + info->tcsr_reg);

	return ffs(val & TCU_TCSR_PARENT_CLOCK_MASK) - 1;
}

static int ingenic_tcu_set_parent(struct clk_hw *hw, u8 idx)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	bool was_enabled;
	unsigned int val;

	was_enabled = ingenic_tcu_enable_regs(hw);

	val = readl(tcu_clk->tcu->map + info->tcsr_reg);
	val &= ~TCU_TCSR_PARENT_CLOCK_MASK;
	val |= BIT(idx);
	writel(val, tcu_clk->tcu->map + info->tcsr_reg);

	if (!was_enabled)
		ingenic_tcu_disable_regs(hw);

	return 0;
}

static unsigned long ingenic_tcu_recalc_rate(struct clk_hw *hw,
											 unsigned long parent_rate)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	unsigned int prescale;

	prescale = readl(tcu_clk->tcu->map + info->tcsr_reg);
	prescale = (prescale & TCU_TCSR_PRESCALE_MASK) >> TCU_TCSR_PRESCALE_LSB;

	return parent_rate >> (prescale * 2);
}

static u8 ingenic_tcu_get_prescale(unsigned long rate, unsigned long req_rate)
{
	u8 prescale;

	for (prescale = 0; prescale < 5; prescale++)
		if ((rate >> (prescale * 2)) <= req_rate)
			return prescale;

	return 5; /* /1024 divider */
}

static long ingenic_tcu_round_rate(struct clk_hw *hw, unsigned long req_rate,
								   unsigned long *parent_rate)
{
	unsigned long rate = *parent_rate;
	u8 prescale;

	if (req_rate > rate)
		return rate;

	prescale = ingenic_tcu_get_prescale(rate, req_rate);

	return rate >> (prescale * 2);
}

static int ingenic_tcu_set_rate(struct clk_hw *hw, unsigned long req_rate,
								unsigned long parent_rate)
{
	bool was_enabled;
	unsigned int val;
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;

	u8 prescale = ingenic_tcu_get_prescale(parent_rate, req_rate);

	was_enabled = ingenic_tcu_enable_regs(hw);

	val = readl(tcu_clk->tcu->map + info->tcsr_reg);
	val &= ~TCU_TCSR_PRESCALE_MASK;
	val |= prescale << TCU_TCSR_PRESCALE_LSB;
	writel(val, tcu_clk->tcu->map + info->tcsr_reg);

	if (!was_enabled)
		ingenic_tcu_disable_regs(hw);

	return 0;
}

static const struct clk_ops ingenic_tcu_clk_ops = {
		.get_parent	= ingenic_tcu_get_parent,
		.set_parent	= ingenic_tcu_set_parent,

		.recalc_rate	= ingenic_tcu_recalc_rate,
		.round_rate	= ingenic_tcu_round_rate,
		.set_rate	= ingenic_tcu_set_rate,

		.enable		= ingenic_tcu_enable,
		.disable	= ingenic_tcu_disable,
		.is_enabled	= ingenic_tcu_is_enabled,
};

static const char * const ingenic_tcu_timer_parents[] = {
		[TCU_PARENT_PCLK] = "pclk",
		[TCU_PARENT_RTC]  = "rtc",
		[TCU_PARENT_EXT]  = "ext",
};

#define DEF_TIMER(_name, _gate_bit, _tcsr)				\
	{								\
		.init_data = {						\
			.name = _name,					\
			.parent_names = ingenic_tcu_timer_parents,	\
			.num_parents = ARRAY_SIZE(ingenic_tcu_timer_parents),\
			.ops = &ingenic_tcu_clk_ops,			\
			.flags = CLK_SET_RATE_UNGATE,			\
		},							\
		.gate_bit = _gate_bit,					\
		.tcsr_reg = _tcsr,					\
	}
static const struct ingenic_tcu_clk_info ingenic_tcu_clk_info[] = {
		[TCU_CLK_TIMER0] = DEF_TIMER("timer0", 0, TCU_REG_TCSRc(0)),
		[TCU_CLK_TIMER1] = DEF_TIMER("timer1", 1, TCU_REG_TCSRc(1)),
		[TCU_CLK_TIMER2] = DEF_TIMER("timer2", 2, TCU_REG_TCSRc(2)),
		[TCU_CLK_TIMER3] = DEF_TIMER("timer3", 3, TCU_REG_TCSRc(3)),
		[TCU_CLK_TIMER4] = DEF_TIMER("timer4", 4, TCU_REG_TCSRc(4)),
		[TCU_CLK_TIMER5] = DEF_TIMER("timer5", 5, TCU_REG_TCSRc(5)),
		[TCU_CLK_TIMER6] = DEF_TIMER("timer6", 6, TCU_REG_TCSRc(6)),
		[TCU_CLK_TIMER7] = DEF_TIMER("timer7", 7, TCU_REG_TCSRc(7)),
};

static const struct ingenic_tcu_clk_info ingenic_tcu_watchdog_clk_info =
		DEF_TIMER("wdt", 16, TCU_REG_WDT_TCSR);
static const struct ingenic_tcu_clk_info ingenic_tcu_ost_clk_info =
		DEF_TIMER("ost", 15, TCU_REG_OST_TCSR);
#undef DEF_TIMER

static int ingenic_tcu_register_clock(struct ingenic_tcu *tcu,
											 unsigned int idx, enum tcu_clk_parent parent,
											 const struct ingenic_tcu_clk_info *info,
											 struct clk_hw_onecell_data *clocks)
{
	struct ingenic_tcu_clk *tcu_clk;
	int err;
	unsigned int val;
	bool was_enabled;

	tcu_clk = kzalloc(sizeof(*tcu_clk), GFP_KERNEL);
	if (!tcu_clk)
		return -ENOMEM;

	tcu_clk->hw.init = &info->init_data;
	tcu_clk->idx = idx;
	tcu_clk->info = info;
	tcu_clk->tcu = tcu;

	// Ensure the timer is stopped and set its default parent clock
	was_enabled = ingenic_tcu_enable_regs(&tcu_clk->hw);
	val = readl(tcu->map + info->tcsr_reg);
	val &= ~0xffff;  // Assuming mask needs to clear specific bits
	val |= BIT(parent);
	writel(val, tcu->map + info->tcsr_reg);
	if (!was_enabled)
		ingenic_tcu_disable_regs(&tcu_clk->hw);

	err = clk_hw_register(NULL, &tcu_clk->hw);
	if (err) {
		kfree(tcu_clk);
		return err;
	}

	clocks->hws[idx] = &tcu_clk->hw;

	return 0;
}

static const struct ingenic_soc_info jz4740_soc_info = {
		.num_channels = 8,
		.has_ost = false,
		.has_tcu_clk = true,
};

static const struct ingenic_soc_info jz4725b_soc_info = {
		.num_channels = 6,
		.has_ost = true,
		.has_tcu_clk = true,
};

static const struct ingenic_soc_info jz4770_soc_info = {
		.num_channels = 8,
		.has_ost = true,
		.has_tcu_clk = false,
};

static const struct ingenic_soc_info x1000_soc_info = {
		.num_channels = 8,
		.has_ost = false, /* X1000 has OST, but it not belong TCU */
		.has_tcu_clk = true,
		.allow_missing_tcu_clk = true,
};

static const struct of_device_id ingenic_tcu_of_match[] = {
		{ .compatible = "ingenic,jz4740-tcu", .data = &jz4740_soc_info, },
		{ .compatible = "ingenic,jz4725b-tcu", .data = &jz4725b_soc_info, },
		{ .compatible = "ingenic,jz4760-tcu", .data = &jz4770_soc_info, },
		{ .compatible = "ingenic,jz4770-tcu", .data = &jz4770_soc_info, },
		{ .compatible = "ingenic,x1000-tcu", .data = &x1000_soc_info, },
		{ /* sentinel */ }
};

static int ingenic_tcu_probe(struct platform_device *pdev) {
	const struct of_device_id *id = of_match_node(ingenic_tcu_of_match, pdev->dev.of_node);
	struct ingenic_tcu *tcu;
	struct resource *res;
	void __iomem *base;
	int ret;
	int i;

	printk("ingenic_tcu_probe\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No memory resource for TCU\n");
		return -ENOENT;
	}

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "Failed to map TCU registers\n");
		return PTR_ERR(base);
	}

	tcu = devm_kzalloc(&pdev->dev, sizeof(*tcu), GFP_KERNEL);
	if (!tcu) {
		printk("ingenic_tcu_probe: ENOMEM\n");
		return -ENOMEM;
	}

	tcu->map = base;
	tcu->soc_info = id->data;

	if (tcu->soc_info->has_tcu_clk) {
		tcu->clk = devm_clk_get(&pdev->dev, "tcu");
		if (IS_ERR(tcu->clk)) {
			ret = PTR_ERR(tcu->clk);
			if (tcu->soc_info->allow_missing_tcu_clk && ret == -EINVAL) {
				printk("TCU clock missing from device tree, please update your device tree\n");
				tcu->clk = NULL;
			} else {
				dev_err(&pdev->dev, "Cannot get TCU clock from device tree\n");
				return ret;
			}
		} else {
			ret = clk_prepare_enable(tcu->clk);
			if (ret) {
				dev_err(&pdev->dev, "Unable to enable TCU clock\n");
				return ret;
			}
		}
	}

	tcu->clocks = devm_kzalloc(&pdev->dev, struct_size(tcu->clocks, hws, TCU_CLK_COUNT), GFP_KERNEL);
	if (!tcu->clocks) {
		dev_err(&pdev->dev, "Failed to allocate memory for clock data\n");
		return -ENOMEM;
	}

	tcu->clocks->num = TCU_CLK_COUNT;

	for (i = 0; i < tcu->soc_info->num_channels; i++) {
		printk("ingenic_tcu_probe:ingenic_tcu_register_clock\n");
		ret = ingenic_tcu_register_clock(tcu, i, TCU_PARENT_EXT,
										 &ingenic_tcu_clk_info[i],
										 tcu->clocks);
		if (ret) {
			pr_crit("cannot register clock %d\n", i);
			goto err_unregister_timer_clocks;
		}
	}

	/*
	 * We set EXT as the default parent clock for all the TCU clocks
	 * except for the watchdog one, where we set the RTC clock as the
	 * parent. Since the EXT and PCLK are much faster than the RTC clock,
	 * the watchdog would kick after a maximum time of 5s, and we might
	 * want a slower kicking time.
	 */
	ret = ingenic_tcu_register_clock(tcu, TCU_CLK_WDT, TCU_PARENT_RTC,
									 &ingenic_tcu_watchdog_clk_info,
									 tcu->clocks);
	if (ret) {
		printk("cannot register watchdog clock\n");
		goto err_unregister_timer_clocks;
	}

	if (tcu->soc_info->has_ost) {
		ret = ingenic_tcu_register_clock(tcu, TCU_CLK_OST,
										 TCU_PARENT_EXT,
										 &ingenic_tcu_ost_clk_info,
										 tcu->clocks);
		if (ret) {
			printk("cannot register ost clock\n");
			goto err_unregister_watchdog_clock;
		}
	}

	of_clk_add_hw_provider(pdev->dev.of_node, of_clk_hw_onecell_get, tcu->clocks);
	ingenic_tcu = tcu;

	return 0;

	err_unregister_ost_clock:
	if (tcu->soc_info->has_ost)
		clk_hw_unregister(tcu->clocks->hws[TCU_CLK_OST]);
	err_unregister_watchdog_clock:
	clk_hw_unregister(tcu->clocks->hws[TCU_CLK_WDT]);
	err_unregister_timer_clocks:
	for (i = 0; i < tcu->soc_info->num_channels; i++)
		if (tcu->clocks->hws[i])
			clk_hw_unregister(tcu->clocks->hws[i]);
	return ret;
}

static int __maybe_unused tcu_pm_suspend(void)
{
	struct ingenic_tcu *tcu = ingenic_tcu;

	if (tcu->clk)
		clk_disable(tcu->clk);

	return 0;
}

static void __maybe_unused tcu_pm_resume(void)
{
	struct ingenic_tcu *tcu = ingenic_tcu;

	if (tcu->clk)
		clk_enable(tcu->clk);
}

static struct syscore_ops __maybe_unused tcu_pm_ops = {
		.suspend = tcu_pm_suspend,
		.resume = tcu_pm_resume,
};


static struct platform_driver ingenic_tcu_driver = {
		.driver = {
				.name = "ingenic-tcu",
				.of_match_table = ingenic_tcu_of_match,
		},
		.probe = ingenic_tcu_probe,
};

static int ingenic_tcu_init(struct device_node *np) {
	int ret;

	printk("ingenic_tcu_init\n");

	ret = platform_driver_register(&ingenic_tcu_driver);
	if (ret) {
		pr_err("Failed to register Ingenic TCU driver: %d\n", ret);
		return ret;
	}

	return 0;
}

module_platform_driver(ingenic_tcu_driver);
