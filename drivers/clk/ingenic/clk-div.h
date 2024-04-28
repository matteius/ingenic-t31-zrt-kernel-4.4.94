#ifndef __INGENIC_CLK_CGU_DIVIDER_H
#define __INGENIC_CLK_CGU_DIVIDER_H

// Ensure consistent declarations
struct clk *clk_register_cgu_divider_table(struct ingenic_clk_provider *ctx,
                                           struct ingenic_div_clock *div_clk);

struct clk *clk_register_cgu_divider(struct ingenic_clk_provider *ctx,
                                     struct ingenic_div_clock *div_clk);


#endif /* __SAMSUNG_CLK_PLL_H */
