#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include "clk.h"

static int clk_bus_wait(void __iomem *reg, u8 shift)
{
unsigned int timeout = 0xfffff;

while (((readl(reg) >> shift) & 1) && timeout--);
if (!timeout) {
printk("WARNING : why cannot wait bus stable ???\n");
return -EIO;
} else {
return 0;
}
}

struct clk_bus_divider {
    struct clk_divider div;
    void __iomem *busy_reg;
    int busy_shift;
    int ce_shift;
    int shift1;
    int width1;
    int shift2;
    int width2;
    int div_flags;
    spinlock_t *lock;
};

static inline struct clk_bus_divider *to_clk_bus_divider(struct clk_divider *div)
{
    return container_of(div, struct clk_bus_divider, div);
}

unsigned long clk_bus_divider_recalc_rate(struct clk_divider *div, unsigned long parent_rate)
{
    struct clk_bus_divider *bus_div = to_clk_bus_divider(div);

    div->shift = bus_div->shift1;
    div->width = bus_div->width1;

    return clk_divider_recalc_rate(div, parent_rate);
}

long clk_bus_divider_round_rate(struct clk_divider *div, unsigned long rate, unsigned long *prate)
{
    return clk_divider_round_rate(div, rate, prate);
}

int clk_bus_divider_set_rate(struct clk_divider *div, unsigned long rate, unsigned long parent_rate)
{
    struct clk_bus_divider *bus_div = to_clk_bus_divider(div);
    int ret;
    unsigned long flags = 0;
    unsigned int val;
    int ce = bus_div->ce_shift;

    if (bus_div->lock) {
        spin_lock_irqsave(bus_div->lock, flags);
    }

    /* set bus rate */
    if (bus_div->div_flags == BUS_DIV_SELF) {
        ret = clk_divider_set_rate(div, rate, parent_rate);
    } else if (bus_div->div_flags == BUS_DIV_ONE) {
        div->shift = bus_div->shift1;
        div->width = bus_div->width1;
        ret = clk_divider_set_rate(div, rate, parent_rate);

        div->shift = bus_div->shift2;
        div->width = bus_div->width2;
        ret = clk_divider_set_rate(div, rate, parent_rate);
    } else if (bus_div->div_flags == BUS_DIV_TWO) {
        div->shift = bus_div->shift1;
        div->width = bus_div->width1;
        ret = clk_divider_set_rate(div, rate, parent_rate);

        div->shift = bus_div->shift2;
        div->width = bus_div->width2;
        ret = clk_divider_set_rate(div, rate / 2, parent_rate);
    }

    /* ce */
    if (ce > 0) {
        val = readl(div->reg);
        val |= (1 << ce);
        writel(val, div->reg);

        ret = clk_bus_wait(bus_div->busy_reg, bus_div->busy_shift);
        if (ret < 0) {
            pr_err("wait bus clk: (%s) stable timeout!\n", __clk_get_name(div->hw.clk));
        }

        val &= ~(1 << ce);
        writel(val, div->reg);
    }

    if (bus_div->lock) {
        spin_unlock_irqrestore(bus_div->lock, flags);
    }

    return ret;
}

static struct clk_ops clk_bus_divider_ops = {
        .recalc_rate = clk_bus_divider_recalc_rate,
        .round_rate = clk_bus_divider_round_rate,
        .set_rate = clk_bus_divider_set_rate,
};

struct clk *clk_register_bus_divider_table(struct device *dev, const char *name,
                                           const char *parent_name, unsigned long flags,
                                           void __iomem *reg, u8 shift1, u8 width1, u8 shift2, u8 width2,
void __iomem *busy_reg, u8 busy_shift, int ce_shift,
        u8 clk_divider_flags, u8 div_flags,
const struct clk_div_table *table,
        spinlock_t *lock)
{
struct clk_bus_divider *bus_div;
struct clk_init_data init = {};
struct clk *clk;

bus_div = kzalloc(sizeof(*bus_div), GFP_KERNEL);
if (!bus_div)
return ERR_PTR(-ENOMEM);

init.name = name;
init.ops = &clk_bus_divider_ops;
init.flags = flags;
init.parent_names = &parent_name;
init.num_parents = 1;

bus_div->div.reg = reg;
bus_div->div.shift = shift1;
bus_div->div.width = width1;
bus_div->shift1 = shift1;
bus_div->width1 = width1;
bus_div->shift2 = shift2;
bus_div->width2 = width2;
bus_div->div.flags = clk_divider_flags;
bus_div->div.table = table;
bus_div->busy_reg = busy_reg;
bus_div->busy_shift = busy_shift;
bus_div->ce_shift = ce_shift;
bus_div->div_flags = div_flags;
bus_div->lock = lock;

bus_div->div.hw.init = &init;

clk = clk_register(dev, &bus_div->div.hw);
if (IS_ERR(clk))
kfree(bus_div);

return clk;
}

struct clk *clk_register_bus_divider(struct device *dev, const char *name,
                                     const char *parent_name, unsigned long flags,
                                     void __iomem *reg, u8 shift1, u8 width1, u8 shift2, u8 width2,
void __iomem *busy_reg, u8 busy_shift, int ce_shift,
        u8 clk_divider_flags, u8 div_flags, spinlock_t *lock)
{
return clk_register_bus_divider_table(dev, name, parent_name, flags, reg, shift1,
        width1, shift2, width2, busy_reg, busy_shift, ce_shift,
        clk_divider_flags, div_flags, NULL, lock);
}