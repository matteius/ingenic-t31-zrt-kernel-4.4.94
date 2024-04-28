#ifndef __INGENIC_CLK_H
#define __INGENIC_CLK_H

#include <linux/clk-provider.h>
#include "clk-pll.h"
#include "clk-div.h"
#include "clk-bus.h"
#include "power-gate.h"
#include <soc/cpm.h>

struct clk;

struct ingenic_clk_provider {
    void __iomem *reg_base;
    struct clk_onecell_data clk_data;
    spinlock_t lock;
    struct device_node *node;
};

struct ingenic_clock_alias {
    unsigned int		id;
    const char		*dev_name;
    const char		*alias;
};

#define ALIAS(_id, dname, a)	\
	{							\
		.id		= _id,				\
		.dev_name	= dname,			\
		.alias		= a,				\
	}

#define MHZ (1000 * 1000)

struct ingenic_fixed_rate_clock {
    unsigned int		id;
    char			*name;
    const char		*parent_name;
    unsigned long		flags;
    unsigned long		fixed_rate;
};

#define FRATE(_id, cname, pname, f, frate)		\
	{						\
		.id		= _id,			\
		.name		= cname,		\
		.parent_name	= pname,		\
		.flags		= (f | CLK_IGNORE_UNUSED),			\
		.fixed_rate	= frate,		\
	}

struct ingenic_fixed_factor_clock {
    unsigned int		id;
    char			*name;
    const char		*parent_name;
    unsigned long		mult;
    unsigned long		div;
    unsigned long		flags;
};

#define FFACTOR(_id, cname, pname, m, d, f)		\
	{						\
		.id		= _id,			\
		.name		= cname,		\
		.parent_name	= pname,		\
		.mult		= m,			\
		.div		= d,			\
		.flags		= f,			\
	}

struct ingenic_mux_clock {
    unsigned int		id;
    const char		*dev_name;
    const char		*name;
    unsigned int 		*table;
    const char		**parent_names;
    u8			num_parents;
    unsigned long		flags;
    unsigned long		offset;
    u8			shift;
    u8			width;
    u8			mux_flags;
    const char		*alias;
};

#define __MUX(_id, dname, cname, tb, pnames, o, s, w, f, mf, a)	\
	{							\
		.id		= _id,				\
		.dev_name	= dname,			\
		.name		= cname,			\
		.table 		= tb,				\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f, 				\
		.offset		= o,				\
		.shift		= s,				\
		.width		= w,				\
		.mux_flags	= mf,				\
		.alias		= a,				\
	}

#define MUX(_id, cname, tb, pnames, o, s, w, f)			\
	__MUX(_id, NULL, cname, tb, pnames, o, s, w, f, 0, cname)

#define MUX_A(_id, cname, tb, pnames, o, s, w, a)			\
	__MUX(_id, NULL, cname, tb, pnames, o, s, w, 0, 0, a)

#define MUX_F(_id, cname, tb, pnames, o, s, w, f, mf)		\
	__MUX(_id, NULL, cname, tb, pnames, o, s, w, f, mf, NULL)

#define MUX_FA(_id, cname, tb, pnames, o, s, w, f, mf, a)		\
	__MUX(_id, NULL, cname, tb, pnames, o, s, w, f, mf, a)

struct ingenic_div_clock {
    unsigned int		id;
    const char		*dev_name;
    const char		*name;
    const char		*parent_name;
    unsigned long		flags;
    unsigned long		offset;
    u8			shift;
    u8			width;
    unsigned long		busy_offset;
    int			busy_shift;
    int			en_shift;
    int			stop_shift;
    int 			div_flags;
    const char		*alias;
    struct clk_div_table	*table;
};

#define __DIV(_id, dname, cname, pname, o, s, w, bs, e, st, f, df, a, t)	\
	{							\
		.id		= _id,				\
		.dev_name	= dname,			\
		.name		= cname,			\
		.parent_name	= pname,			\
		.flags		= (f | CLK_IGNORE_UNUSED),				\
		.offset		= o,				\
		.shift		= s,				\
		.width		= w,				\
		.div_flags	= df,				\
		.busy_shift	= bs,				\
		.en_shift	= e,				\
		.stop_shift	= st,				\
		.alias		= a,				\
		.table		= t,				\
	}

#define DIV(_id, cname, pname, o, w, df, t)				\
	__DIV(_id, NULL, cname, pname, o, 0, w, 28, 29, 27, 0, df, cname, t)

#define DIV_EN(_id, cname, pname, o, s, w, bs, e)				\
	__DIV(_id, NULL, cname, pname, o, s, w, bs, e, 0, 0, NULL, NULL)

struct ingenic_bus_clock {
    unsigned int		id;
    const char		*dev_name;
    const char		*name;
    const char		*parent_name;
    unsigned long		flags;
    unsigned long		cfg_offset;
    u8			div_shift1;
    u8			div_width1;
    u8			div_shift2;
    u8			div_width2;
    int 	  		ce_shift;
    int 			busy_offset;
    int			busy_shift;
    int 			div_flags;
    int 			div_flags_2;
    const char		*alias;
    struct clk_div_table	*table;
};

#define __BUS_DIV(_id, dname, cname, pname, o, s1, w1, s2, w2, bo, bs, ce, f, df, df2, a, t)	\
	{							\
		.id		= _id,				\
		.dev_name	= dname,			\
		.name		= cname,			\
		.parent_name	= pname,			\
		.flags		= (f | CLK_IGNORE_UNUSED),	\
		.cfg_offset	= o,				\
		.div_shift1	= s1,				\
		.div_width1	= w1,				\
		.div_shift2	= s2,				\
		.div_width2	= w2,				\
		.ce_shift	= ce,				\
		.busy_offset	= bo,				\
		.busy_shift	= bs,				\
		.div_flags	= df,				\
		.alias		= a,				\
		.table		= t,				\
		.div_flags_2	= df2,				\
	}

#define BUS_DIV(_id, cname, pname, o, s1, w1, s2, w2, bo, bs, ce, df2)				\
	__BUS_DIV(_id, NULL, cname, pname, o, s1, w1, s2, w2, bo, bs, ce, CLK_GET_RATE_NOCACHE, 0, df2, cname, NULL)

struct ingenic_fra_div_clock {
    unsigned int		id;
    const char		*dev_name;
    const char		*name;
    const char		*parent_name;
    unsigned long		flags;
    unsigned long		offset;
    u8			mshift;
    u8			mwidth;
    u8			nshift;
    u8			nwidth;
    int 			div_flags;
    const char		*alias;
};

#define __FRA_DIV(_id, dname, cname, pname, f, o, ms, mw, ns, nw, df, a)	\
	{									\
		.id		= _id,		\
		.dev_name	= dname,	\
		.name		= cname,	\
		.parent_name	= pname,	\
		.flags		= f,		\
		.offset		= o,		\
		.mshift		= ms,		\
		.mwidth		= mw,		\
		.nshift		= ns,		\
		.nwidth		= nw,		\
		.div_flags	= df,		\
		.alias		= a,		\
	}

#define FRA_DIV(_id, cname, pname, o, ms, mw, ns, nw)	\
	__FRA_DIV(_id, NULL, cname, pname, 0, o, ms, mw, ns, nw, 0, cname)

struct ingenic_gate_clock {
    unsigned int		id;
    const char		*dev_name;
    const char		*name;
    const char		*parent_name;
    unsigned long		flags;
    unsigned long		offset;
    u8			bit_idx;
    int			sram_offset;
    int			sram_shift;
    u8			gate_flags;
    const char		*alias;
};

#define __GATE(_id, dname, cname, pname, o, b, f, so, sps, gf, a)	\
	{							\
		.id		= _id,				\
		.dev_name	= dname,			\
		.name		= cname,			\
		.parent_name	= pname,			\
		.flags		= f,				\
		.offset		= o,				\
		.bit_idx	= b,				\
		.sram_offset	= so,				\
		.sram_shift 	= sps,				\
		.gate_flags	= gf,				\
		.alias		= a,				\
	}

#define GATE(_id, cname, pname, o, b, f, gf)			\
	__GATE(_id, NULL, cname, pname, o, b, f, -1, -1, gf, cname)

#define GATE_SRAM(_id, cname, pname, o, b, f, gf, so, sps)			\
	__GATE(_id, NULL, cname, pname, NULL, o, b, f, so, sps, gf, cname)

struct ingenic_gate_power {
    unsigned int		id;
    const char		*dev_name;
    const char		*name;
    const char		*parent_name;
    unsigned long		flags;
    unsigned long		offset;
    u8			ctrl_bit;
    u8			wait_bit;
    u8			gate_flags;
    const char		*alias;
    unsigned long 		power_flags;
};

#define __POWER(_id, dname, cname, pname, f, o, cb, wb, gf, a, pf)	\
	{							\
		.id		= _id,				\
		.dev_name	= dname,			\
		.name		= cname,			\
		.parent_name	= pname,			\
		.flags		= f,				\
		.offset		= o,				\
		.ctrl_bit	= cb,				\
		.wait_bit	= wb,				\
		.gate_flags	= gf,				\
		.alias		= a,				\
		.power_flags	= pf,				\
	}

#define POWER(_id, cname, pname, o, cb, wb, f, gf, pf)			\
	__POWER(_id, NULL, cname, pname, f, o, cb, wb, gf, cname, pf)

#define PNAME(x) static const char *x[] __initdata

struct ingenic_clk_reg_dump {
    u32	offset;
    u32	value;
};

struct ingenic_pll_clock {
    unsigned int		id;
    const char		*dev_name;
    const char		*name;
    const char		*parent_name;
    unsigned long		flags;
    int			con_offset;
    int			lock_offset;
    const char              *alias;
    struct ingenic_pll_hwdesc *hwdesc;
    struct ingenic_pll_rate_table *rate_table;
};

#define PLL(_id, _name, _parent_name, _hwdesc, _rtable)	\
{	\
	.id = _id,	\
	.dev_name = _name,	\
	.name = _name,	\
	.parent_name = _parent_name,	\
	.hwdesc = _hwdesc,	\
	.rate_table = _rtable,	\
}

struct ingenic_clock_reg_cache {
    struct list_head node;
    void __iomem *reg_base;
    struct ingenic_clk_reg_dump *rdump;
    unsigned int rd_num;
};

struct ingenic_cmu_info {
    struct ingenic_pll_clock *pll_clks;
    unsigned int nr_pll_clks;
    struct ingenic_mux_clock *mux_clks;
    unsigned int nr_mux_clks;
    struct ingenic_div_clock *div_clks;
    unsigned int nr_div_clks;
    struct ingenic_gate_clock *gate_clks;
    unsigned int nr_gate_clks;
    struct ingenic_fixed_rate_clock *fixed_clks;
    unsigned int nr_fixed_clks;
    struct ingenic_fixed_factor_clock *fixed_factor_clks;
    unsigned int nr_fixed_factor_clks;
    unsigned int nr_clk_ids;
    unsigned long *clk_regs;
    unsigned int nr_clk_regs;
};

struct ingenic_cpm_info {
    struct ingenic_power_clock *pwc_clks;
    unsigned int nr_pwc_clks;
};

struct ingenic_clk_provider *ingenic_clk_alloc_provider(struct device_node *node);
void ingenic_clk_register_provider(struct ingenic_clk_provider *p);

void ingenic_clk_register_fixed_factor(struct ingenic_clk_provider *p,
                                       struct ingenic_fixed_rate_clock *clks,
                                       unsigned int num);

void ingenic_clk_register_mux(struct ingenic_clk_provider *p,
                              struct ingenic_mux_clock *clks,
                              unsigned int num);

void ingenic_clk_register_bus_div(struct ingenic_clk_provider *p,
                                  struct ingenic_bus_clock *clks,
                                  unsigned int num);

void ingenic_clk_register_cgu_div(struct ingenic_clk_provider *p,
                                  struct ingenic_div_clock *clks,
                                  unsigned int num);

void ingenic_clk_register_fra_div(struct ingenic_clk_provider *p,
                                  struct ingenic_fra_div_clock *clks,
                                  unsigned int num);

void ingenic_clk_register_gate(struct ingenic_clk_provider *p,
                               struct ingenic_gate_clock *clks,
                               unsigned int num);

#endif /* __INGENIC_CLK_H */
