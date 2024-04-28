#ifndef __INGENIC_CLK_PLLV_H__
#define __INGENIC_CLK_PLLV_H__

#include "clk.h"

extern const struct clk_ops ingenic_pll_ro_ops;

#define PLL_RATE(_rate, _m, _n, _od1, _od0)	\
{					\
	.rate = _rate,			\
	.m = _m,			\
	.n = _n,			\
	.od1 = _od1,			\
	.od0 = _od0,			\
}

struct ingenic_pll_rate_table {
	unsigned int rate;
	unsigned int m;
	unsigned int n;
	unsigned int od1;
	unsigned int od0;
};

// Function declaration
struct clk *ingenic_clk_register_pll(struct ingenic_clk_provider *ctx,
                                     struct ingenic_pll_clock *pll_clk,
                                     void __iomem *base)
{
struct ingenic_clk_pll *pll;
struct clk *clk;
struct clk_init_data init;
int len;

pll = kzalloc(sizeof(*pll), GFP_KERNEL);
if (!pll) {
pr_err("%s: could not allocate pll clk %s\n",
__func__, pll_clk->name);
return ERR_PTR(-ENOMEM);
}

#define PLL_DESC(_regoff, _m, _m_w, _n, _n_w, _od1, _od1_w, _od0, _od0_w, _on, _en, _bs)	\
{				\
	.regoff = _regoff,	\
	.m_sft = _m,		\
	.m_width = _m_w,	\
	.n_sft = _n,		\
	.n_width = _n_w,	\
	.od1_sft = _od1,		\
	.od1_width = _od1_w,	\
	.od0_sft = _od0,		\
	.od0_width = _od0_w,	\
	.on_bit = _on,		\
	.en_bit =  _en,		\
	.bs_bit = _bs,	\
}

struct ingenic_pll_hwdesc {
	u32 regoff;
	u8 m_sft;
	u8 m_width;
	u8 n_sft;
	u8 n_width;
	u8 od1_sft;
	u8 od1_width;
	u8 od0_sft;
	u8 od0_width;
	u8 on_bit;
	u8 en_bit;
	s8 bs_bit;
};

#endif /*__INGENIC_CLK_PLLV1_H__*/
