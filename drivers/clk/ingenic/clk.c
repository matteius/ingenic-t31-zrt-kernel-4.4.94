#include <linux/slab.h>
#include <linux/clkdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>

#include "clk.h"

static LIST_HEAD(clock_reg_cache_list);

void ingenic_clk_save(void __iomem *base,
				    struct ingenic_clk_reg_dump *rd,
				    unsigned int num_regs)
{
	for (; num_regs > 0; --num_regs, ++rd)
		rd->value = readl(base + rd->offset);
}

void ingenic_clk_restore(void __iomem *base,
				      const struct ingenic_clk_reg_dump *rd,
				      unsigned int num_regs)
{
	for (; num_regs > 0; --num_regs, ++rd)
		writel(rd->value, base + rd->offset);
}

struct ingenic_clk_reg_dump *ingenic_clk_alloc_reg_dump(
						const unsigned long *rdump,
						unsigned long nr_rdump)
{
	struct ingenic_clk_reg_dump *rd;
	unsigned int i;

	rd = kcalloc(nr_rdump, sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return NULL;

	for (i = 0; i < nr_rdump; ++i)
		rd[i].offset = rdump[i];

	return rd;
}

static int _ingenic_register_clock(struct ingenic_clk_provider *ctx, unsigned idx)
{
    struct clk *clk;
    int err = -EINVAL;

    if (idx >= ctx->clocks.clk_num) {
        pr_err("%s: invalid clock index %u\n", __func__, idx);
        goto out;
    }

    // Determine the clock type based on the index
    switch (idx) {
        case CLK_EXT:
        case CLK_RTC_EXT:
            // Register fixed-rate clocks
            ingenic_clk_register_fixed_rate(ctx, &t31_fixed_rate_ext_clks[idx], 1);
            break;

        case CLK_PLL_APLL:
        case CLK_PLL_MPLL:
        case CLK_PLL_VPLL:
            // Register PLL clocks
            ingenic_clk_register_pll(ctx, &t31_pll_clks[idx - CLK_PLL_APLL], 1, ctx->base);
            break;

        case CLK_MUX_SCLKA:
        case CLK_MUX_CPU_L2C:
        case CLK_MUX_AHB0:
        case CLK_MUX_AHB2:
            // Register MUX clocks
            ingenic_clk_register_mux(ctx, &t31_mux_clks[idx - CLK_MUX_SCLKA], 1);
            break;

        case CLK_DIV_CPU:
        case CLK_DIV_L2C:
        case CLK_DIV_AHB0:
        case CLK_DIV_AHB2:
        case CLK_DIV_APB:
        case CLK_DIV_CPU_L2C_X1:
        case CLK_DIV_CPU_L2C_X2:
            // Register bus divider clocks
            ingenic_clk_register_bus_div(ctx, &t31_bus_div_clks[idx - CLK_DIV_CPU], 1);
            break;

        case CLK_DIV_DDR:
        case CLK_DIV_MACPHY:
        case CLK_DIV_LCD:
        case CLK_DIV_MSC0:
        case CLK_DIV_MSC1:
        case CLK_DIV_SFC:
        case CLK_DIV_SSI:
        case CLK_DIV_CIM:
        case CLK_DIV_ISP:
        case CLK_DIV_RSA:
        case CLK_DIV_EL150:
            // Register divider clocks
            ingenic_clk_register_cgu_div(ctx, &t31_div_clks[idx - CLK_DIV_DDR], 1);
            break;

        case CLK_DIV_I2ST:
        case CLK_DIV_I2SR:
            // Register fractional divider clocks
            ingenic_clk_register_fra_div(ctx, &t31_fdiv_clks[idx - CLK_DIV_I2ST], 1);
            break;

        case CLK_GATE_DDR:
        case CLK_GATE_TCU:
        case CLK_GATE_DES:
            // Register gate clocks
            ingenic_clk_register_gate(ctx, &t31_gate_clks[idx - CLK_GATE_DDR], 1);
            break;

        default:
            pr_err("%s: unsupported clock type for index %u\n", __func__, idx);
            goto out;
    }

    clk = ctx->clocks.clks[idx];
    if (IS_ERR(clk)) {
        pr_err("%s: failed to register clock with index %u\n", __func__, idx);
        err = PTR_ERR(clk);
        goto out;
    }

    err = 0;

    out:
    return err;
}

/* setup the essentials required to support clock lookup using ccf */
struct ingenic_clk_provider *__init ingenic_clk_init(struct device_node *np,
			void __iomem *base, unsigned long nr_clks)
{
	struct ingenic_clk_provider *ctx;
	struct clk **clk_table;
	int i;

	ctx = kzalloc(sizeof(struct ingenic_clk_provider), GFP_KERNEL);
	if (!ctx)
		panic("could not allocate clock provider context.\n");

    ctx->base = of_iomap(np, 0);
    if (!ctx->base) {
        pr_err("%s: failed to map CGU registers\n", __func__);
        goto err_out_free;
    }

	clk_table = kcalloc(nr_clks, sizeof(struct clk *), GFP_KERNEL);
	if (!clk_table)
		panic("could not allocate clock lookup table\n");

	for (i = 0; i < nr_clks; ++i)
		clk_table[i] = ERR_PTR(-ENOENT);

	ctx->base = base;
	ctx->clocks.clks = clk_table;
	ctx->clocks.clk_num = nr_clks;
	spin_lock_init(&ctx->lock);

    if (!ctx->clocks.clks) {
        err = -ENOMEM;
        goto err_out_free;
    }

    for (i = 0; i < ctx->clocks.clk_num; i++) {
        err = _ingenic_register_clock(ctx, i); // This is the method you are to implement
        if (err)
            goto err_out_unregister;
    }

    err = ingenic_clk_of_add_provider(ctx->np, ctx);
    if (err)
        goto err_out_unregister;

	return ctx;

    err_out_unregister:
    for (i = 0; i < cgu->clocks.clk_num; i++) {
        if (!cgu->clocks.clks[i])
            continue;
    if (cgu->clock_info[i].type & CGU_CLK_EXT)
        clk_put(cgu->clocks.clks[i]);
    else
        clk_unregister(cgu->clocks.clks[i]);
    }
    kfree(ctx->clocks.clks);
    err_out_free:
    kfree(ctx);
    err_out:
    return NULL;
}

void __init ingenic_clk_of_add_provider(struct device_node *np,
				struct ingenic_clk_provider *ctx)
{
	if (np) {
		if (of_clk_add_provider(np, of_clk_src_onecell_get,
					&ctx->clocks))
			panic("could not register clk provider\n");
	}
}

void ingenic_clk_of_dump(struct ingenic_clk_provider *ctx)
{
	struct clk_onecell_data * clocks;
	struct clk *clk;
	int i;

	clocks = &ctx->clocks;

	for(i = 0; i < clocks->clk_num; i++) {

		clk = clocks->clks[i];
		if(clk != ERR_PTR(-ENOENT)) {
			printk("clk->id: %d clk->name: %s \n", i,  __clk_get_name(clk));
		} else {
			printk("clk->id: %d , clk: %p\n", i, clk);
		}
	}
}


/* add a clock instance to the clock lookup table used for dt based lookup */
void ingenic_clk_add_lookup(struct ingenic_clk_provider *ctx, struct clk *clk,
				unsigned int id)
{
	if (ctx->clocks.clks && id) {
		ctx->clocks.clks[id] = clk;
	}
}

/* register a list of aliases */
void __init ingenic_clk_register_alias(struct ingenic_clk_provider *ctx,
				const struct ingenic_clock_alias *list,
				unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx, ret;

	if (!ctx->clocks.clks) {
		pr_err("%s: clock table missing\n", __func__);
		return;
	}

	for (idx = 0; idx < nr_clk; idx++, list++) {
		if (!list->id) {
			pr_err("%s: clock id missing for index %d\n", __func__,
				idx);
			continue;
		}

		clk = ctx->clocks.clks[list->id];
		if (!clk) {
			pr_err("%s: failed to find clock %d\n", __func__,
				list->id);
			continue;
		}

		ret = clk_register_clkdev(clk, list->alias, list->dev_name);
		if (ret)
			pr_err("%s: failed to register lookup %s\n",
					__func__, list->alias);
	}
}

/* register a list of fixed clocks */
void __init ingenic_clk_register_fixed_rate(struct ingenic_clk_provider *ctx,
		const struct ingenic_fixed_rate_clock *list,
		unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx, ret;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		clk = clk_register_fixed_rate(NULL, list->name,
			list->parent_name, list->flags, list->fixed_rate);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		ingenic_clk_add_lookup(ctx, clk, list->id);

		ret = clk_register_clkdev(clk, list->name, NULL);
		if (ret)
			pr_err("%s: failed to register clock lookup for %s",
				__func__, list->name);
	}
}

/* register a list of fixed factor clocks */
void __init ingenic_clk_register_fixed_factor(struct ingenic_clk_provider *ctx,
		const struct ingenic_fixed_factor_clock *list, unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		clk = clk_register_fixed_factor(NULL, list->name,
			list->parent_name, list->flags, list->mult, list->div);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		ingenic_clk_add_lookup(ctx, clk, list->id);
	}
}

/* register a list of mux clocks */
void __init ingenic_clk_register_mux(struct ingenic_clk_provider *ctx,
				const struct ingenic_mux_clock *list,
				unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx, ret;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		if(list->table) {
			clk = clk_register_mux_table(NULL, list->name, list->parent_names,
					list->num_parents, list->flags,
					ctx->base + list->offset,
					list->shift, BIT(list->width) - 1, list->mux_flags, list->table, &ctx->lock);
		} else {
			clk = clk_register_mux(NULL, list->name, list->parent_names,
					list->num_parents, list->flags,
					ctx->base + list->offset,
					list->shift, list->width, list->mux_flags, &ctx->lock);

		}
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		ingenic_clk_add_lookup(ctx, clk, list->id);

		/* register a clock lookup only if a clock alias is specified */
		if (list->alias) {
			ret = clk_register_clkdev(clk, list->alias,
						list->dev_name);
			if (ret)
				pr_err("%s: failed to register lookup %s\n",
						__func__, list->alias);
		}
	}
}

/* register a list of div clocks */
void __init ingenic_clk_register_cgu_div(struct ingenic_clk_provider *ctx,
				const struct ingenic_div_clock *list,
				unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx, ret;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		if (list->table)
			clk = clk_register_cgu_divider_table(NULL, list->name,
				list->parent_name, list->flags,
				ctx->base + list->offset,
				list->shift, list->width,
				list->busy_shift, list->en_shift, list->stop_shift,
				list->div_flags, list->table, &ctx->lock);
		else
			clk = clk_register_cgu_divider(NULL, list->name,
				list->parent_name, list->flags,
				ctx->base + list->offset,
				list->shift, list->width,
				list->busy_shift, list->en_shift, list->stop_shift,
				list->div_flags, &ctx->lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		ingenic_clk_add_lookup(ctx, clk, list->id);

		/* register a clock lookup only if a clock alias is specified */
		if (list->alias) {
			ret = clk_register_clkdev(clk, list->alias,
						list->dev_name);
			if (ret)
				pr_err("%s: failed to register lookup %s\n",
						__func__, list->alias);
		}
	}
}

/* register a list of div clocks */
void __init ingenic_clk_register_bus_div(struct ingenic_clk_provider *ctx,
				const struct ingenic_bus_clock *list,
				unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx, ret;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		if (list->table)
			clk = clk_register_bus_divider_table(NULL, list->name,
				list->parent_name, list->flags,
				ctx->base + list->cfg_offset,
				list->div_shift1, list->div_width1,
				list->div_shift2, list->div_width2,
				ctx->base + list->busy_offset,
				list->busy_shift, list->ce_shift,
				list->div_flags, list->div_flags_2,
				list->table, &ctx->lock);
		else
			clk = clk_register_bus_divider(NULL, list->name,
				list->parent_name, list->flags,
				ctx->base + list->cfg_offset,
				list->div_shift1, list->div_width1,
				list->div_shift2, list->div_width2,
				ctx->base + list->busy_offset,
				list->busy_shift, list->ce_shift,
				list->div_flags, list->div_flags_2,
				&ctx->lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		ingenic_clk_add_lookup(ctx, clk, list->id);

		/* register a clock lookup only if a clock alias is specified */
		if (list->alias) {
			ret = clk_register_clkdev(clk, list->alias,
						list->dev_name);
			if (ret)
				pr_err("%s: failed to register lookup %s\n",
						__func__, list->alias);
		}
	}
}

void __init ingenic_clk_register_fra_div(struct ingenic_clk_provider *ctx,
					const struct ingenic_fra_div_clock *list,
					unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx, ret;

	for (idx = 0; idx < nr_clk; idx++, list++) {
		clk = clk_register_fractional_divider(NULL,
				list->name, list->parent_name, list->flags,
				ctx->base + list->offset,
				list->mshift, list->mwidth, list->nshift, list->nwidth,
				list->div_flags, &ctx->lock);

		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
					list->name);
			continue;
		}

		ingenic_clk_add_lookup(ctx, clk, list->id);

		/* register a clock lookup only if a clock alias is specified */
		if (list->alias) {
			ret = clk_register_clkdev(clk, list->alias,
					list->dev_name);
			if (ret)
				pr_err("%s: failed to register lookup %s\n", __func__, list->alias);
		}
	}
}


/* register a list of gate clocks */
void __init ingenic_clk_register_gate(struct ingenic_clk_provider *ctx,
				const struct ingenic_gate_clock *list,
				unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx, ret;


	for (idx = 0; idx < nr_clk; idx++, list++) {

		clk = clk_register_gate(NULL, list->name, list->parent_name, list->flags,
				        ctx->base + list->offset, list->bit_idx,
					list->gate_flags, &ctx->lock);

		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		/* register a clock lookup only if a clock alias is specified */
		if (list->alias) {
			ret = clk_register_clkdev(clk, list->alias,
							list->dev_name);
			if (ret)
				pr_err("%s: failed to register lookup %s\n",
					__func__, list->alias);
		}

		ingenic_clk_add_lookup(ctx, clk, list->id);

	}
}

void __init ingenic_power_register_gate(struct ingenic_clk_provider *ctx,
				const struct ingenic_gate_power *list,
				unsigned int nr_clk)
{
	struct clk *clk;
	unsigned int idx, ret;

	for (idx = 0; idx < nr_clk; idx++, list++) {

		clk = power_register_gate(NULL, list->name, list->parent_name, list->flags,
				        ctx->base + list->offset, list->ctrl_bit, list->wait_bit,
					list->gate_flags, list->power_flags, &ctx->lock);

		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n", __func__,
				list->name);
			continue;
		}

		/* register a clock lookup only if a clock alias is specified */
		if (list->alias) {
			ret = clk_register_clkdev(clk, list->alias,
							list->dev_name);
			if (ret)
				pr_err("%s: failed to register lookup %s\n",
					__func__, list->alias);
		}

		ingenic_clk_add_lookup(ctx, clk, list->id);

	}

}
/*
 * obtain the clock speed of all external fixed clock sources from device
 * tree and register it
 */
void __init ingenic_clk_of_register_fixed_ext(struct ingenic_clk_provider *ctx,
			struct ingenic_fixed_rate_clock *fixed_rate_clk,
			unsigned int nr_fixed_rate_clk,
			const struct of_device_id *clk_matches)
{
	const struct of_device_id *match;
	struct device_node *clk_np;
	u32 freq;
	u32 index = 0;

	for_each_matching_node_and_match(clk_np, clk_matches, &match) {
		if (of_property_read_u32(clk_np, "clock-frequency", &freq))
			continue;
		fixed_rate_clk[index].fixed_rate = freq;
		index++;
	}
	ingenic_clk_register_fixed_rate(ctx, fixed_rate_clk, nr_fixed_rate_clk);
}

/* utility function to get the rate of a specified clock */
unsigned long _get_rate(const char *clk_name)
{
	struct clk *clk;

	clk = __clk_lookup(clk_name);
	if (!clk) {
		pr_err("%s: could not find clock %s\n", __func__, clk_name);
		return 0;
	}

	return clk_get_rate(clk);
}

#ifdef CONFIG_PM_SLEEP
static int ingenic_clk_suspend(void)
{
	struct ingenic_clock_reg_cache *reg_cache;

	list_for_each_entry(reg_cache, &clock_reg_cache_list, node)
		ingenic_clk_save(reg_cache->base, reg_cache->rdump,
				reg_cache->rd_num);
	return 0;
}

static void ingenic_clk_resume(void)
{
	struct ingenic_clock_reg_cache *reg_cache;

	list_for_each_entry(reg_cache, &clock_reg_cache_list, node)
		ingenic_clk_restore(reg_cache->base, reg_cache->rdump,
				reg_cache->rd_num);
}

static struct syscore_ops ingenic_clk_syscore_ops = {
	.suspend = ingenic_clk_suspend,
	.resume = ingenic_clk_resume,
};

static void ingenic_clk_sleep_init(void __iomem *base,
		const unsigned long *rdump,
		unsigned long nr_rdump)
{
	struct ingenic_clock_reg_cache *reg_cache;

	reg_cache = kzalloc(sizeof(struct ingenic_clock_reg_cache),
			GFP_KERNEL);
	if (!reg_cache)
		panic("could not allocate register reg_cache.\n");
	reg_cache->rdump = ingenic_clk_alloc_reg_dump(rdump, nr_rdump);

	if (!reg_cache->rdump)
		panic("could not allocate register dump storage.\n");

	if (list_empty(&clock_reg_cache_list))
		register_syscore_ops(&ingenic_clk_syscore_ops);

	reg_cache->base = base;
	reg_cache->rd_num = nr_rdump;
	list_add_tail(&reg_cache->node, &clock_reg_cache_list);
}

#else
static void ingenic_clk_sleep_init(void __iomem *base,
		const unsigned long *rdump,
		unsigned long nr_rdump) {}
#endif

