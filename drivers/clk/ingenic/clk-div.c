#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include "clk.h"

struct clk_cgu_divider {
    struct clk_divider div;
    int busy_shift;
    int en_shift;
    int stop_shift;
    spinlock_t *lock;
};

static int clk_cgu_wait(void __iomem *reg, u8 shift)
{
unsigned int timeout = 0xfffff;

while (((readl(reg) >> shift) & 1) && timeout--);
if (!timeout) {
printk("WARNING : why cannot wait cgu stable ???\n");
return -EIO;
} else {
return 0;
}
}

static int cgu_divider_enable(struct clk_cgu_divider *cgu_div)
{
    int ret;
    unsigned int val;
    int ce, stop;

    val = readl(cgu_div->div.reg);

    ce = cgu_div->en_shift;
    stop = cgu_div->stop_shift;

    val |= (1 << ce);
    val &= ~(1 << stop);
    writel(val, cgu_div->div.reg);

    ret = clk_cgu_wait(cgu_div->div.reg, cgu_div->busy_shift);
    if (ret < 0) {
        printk("wait cgu stable timeout!\n");
    }

    val &= ~(1 << ce);
    writel(val, cgu_div->div.reg);

    return ret;
}

static void cgu_divider_disable(struct clk_cgu_divider *cgu_div)
{
    unsigned int val;
    int ce, stop;

    val = readl(cgu_div->div.reg);

    ce = cgu_div->en_shift;
    stop = cgu_div->stop_shift;

    val |= (1 << ce);
    val |= (1 << stop);
    writel(val, cgu_div->div.reg);

    val &= ~(1 << ce);
    writel(val, cgu_div->div.reg);
}

static inline struct clk_cgu_divider *to_clk_cgu_divider(struct clk_divider *div)
{
    return container_of(div, struct clk_cgu_divider, div);
}

static unsigned long clk_cgu_divider_recalc_rate(struct clk_divider *div,
                                                 unsigned long parent_rate)
{
    return clk_divider_recalc_rate(div, parent_rate);
}

static long clk_cgu_divider_round_rate(struct clk_divider *div, unsigned long rate,
                                       unsigned long *prate)
{
    return clk_divider_round_rate(div, rate, prate);
}

static int clk_cgu_divider_set_rate(struct clk_divider *div, unsigned long rate,
                                    unsigned long parent_rate)
{
    struct clk_cgu_divider *cgu_div = to_clk_cgu_divider(div);
    int ret;
    unsigned long flags = 0;

    if (cgu_div->lock)
        spin_lock_irqsave(cgu_div->lock, flags);

    ret = clk_divider_set_rate(div, rate, parent_rate);

    cgu_divider_enable(cgu_div);

    if (cgu_div->lock)
        spin_unlock_irqrestore(cgu_div->lock, flags);

    return ret;
}

static void clk_cgu_divider_disable(struct clk_divider *div)
{
    struct clk_cgu_divider *cgu_div = to_clk_cgu_divider(div);
    unsigned long flags = 0;

    if (cgu_div->lock)
        spin_lock_irqsave(cgu_div->lock, flags);

    cgu_divider_disable(cgu_div);

    if (cgu_div->lock)
        spin_unlock_irqrestore(cgu_div->lock, flags);
}

static int clk_cgu_divider_enable(struct clk_divider *div)
{
    struct clk_cgu_divider *cgu_div = to_clk_cgu_divider(div);
    unsigned long flags = 0;

    if (cgu_div->lock)
        spin_lock_irqsave(cgu_div->lock, flags);

    cgu_divider_enable(cgu_div);

    if (cgu_div->lock)
        spin_unlock_irqrestore(cgu_div->lock, flags);

    return 0;
}

static struct clk_ops clk_cgu_divider_ops = {
        .recalc_rate = clk_cgu_divider_recalc_rate,
        .round_rate = clk_cgu_divider_round_rate,
        .set_rate = clk_cgu_divider_set_rate,
        .enable = clk_cgu_divider_enable,
        .disable = clk_cgu_divider_disable,
};

struct clk *clk_register_cgu_divider_table(struct ingenic_clk_provider *ctx,
                                           struct ingenic_div_clock *div_clk)
{
    struct clk_cgu_divider *cgu_div;
    struct clk *clk;
    struct clk_init_data init = {};

    cgu_div = kzalloc(sizeof(*cgu_div), GFP_KERNEL);
    if (!cgu_div)
        return ERR_PTR(-ENOMEM);

    init.name = div_clk->name;
    init.ops = &clk_cgu_divider_ops;
    init.flags = div_clk->flags | CLK_IS_BASIC;
    init.parent_names = &div_clk->parent_name;
    init.num_parents = 1;

    cgu_div->busy_shift = div_clk->busy_shift;
    cgu_div->en_shift = div_clk->en_shift;
    cgu_div->stop_shift = div_clk->stop_shift;
    cgu_div->lock = ctx->lock;

    cgu_div->div.reg = ctx->reg_base + div_clk->offset;
    cgu_div->div.shift = div_clk->shift;
    cgu_div->div.width = div_clk->width;
    cgu_div->div.table = div_clk->table;
    cgu_div->div.flags = div_clk->div_flags;

    cgu_div->div.hw.init = &init;

    clk = clk_register(NULL, &cgu_div->div.hw);
    if (IS_ERR(clk))
        kfree(cgu_div);

    return clk;
}

struct clk *clk_register_cgu_divider(struct ingenic_clk_provider *ctx,
                                     struct ingenic_div_clock *div_clk)
{
    return clk_register_cgu_divider_table(ctx, div_clk);
}