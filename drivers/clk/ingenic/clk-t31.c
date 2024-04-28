#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <soc/cpm.h>
#include <soc/base.h>
#include <dt-bindings/clock/ingenic-t31.h>

#include <jz_proc.h>
#include "clk.h"
#include "cgu.h"

#define CLK_FLG_ENABLE BIT(1)
static struct ingenic_clk_provider *ctx;


/*******************************************************************************
 *      FIXED CLK
 ********************************************************************************/
static struct ingenic_fixed_rate_clock t31_fixed_rate_ext_clks[] __initdata = {
	FRATE(CLK_EXT, "ext", NULL, CLK_IS_ROOT, 24000000),
	FRATE(CLK_RTC_EXT, "rtc_ext", NULL, CLK_IS_ROOT, 32768),
};



/*******************************************************************************
 *      PLL
 ********************************************************************************/

static struct ingenic_pll_rate_table t31_pll_rate_table[] = {
	PLL_RATE(1500000000, 125, 1, 2, 1),
	PLL_RATE(1404000000, 117, 1, 2, 1),
	PLL_RATE(1392000000, 116, 1, 2, 1),
	PLL_RATE(1296000000, 108, 1, 2, 1),
	PLL_RATE(1200000000, 100, 1, 2, 1),
	PLL_RATE(1000000000, 125, 1, 3, 1),
	PLL_RATE(900000000, 75, 1, 2, 1),
	PLL_RATE(891000000, 297, 4, 2, 1),
	PLL_RATE(864000000, 72, 1, 2, 1),
	PLL_RATE(600000000, 75, 1, 3, 1),
};



/*PLL HWDESC*/
static struct ingenic_pll_hwdesc apll_hwdesc = \
	PLL_DESC(CPM_CPAPCR, 20, 12, 14, 6, 11, 3, 8, 3, 3, 0, 2);

static struct ingenic_pll_hwdesc mpll_hwdesc = \
	PLL_DESC(CPM_CPMPCR, 20, 12, 14, 6, 11, 3, 8, 3, 3, 0, 2);

static struct ingenic_pll_hwdesc vpll_hwdesc = \
	PLL_DESC(CPM_CPVPCR, 20, 12, 14, 6, 11, 3, 8, 3, 3, 0, 2);


static const u8 apll_od_encoding[] = { 1, 2, 3, 4, 6, 8 };
static const u8 mpll_od_encoding[] = { 1, 2, 3, 4, 6, 8 };
static const u8 vpll_od_encoding[] = { 1, 2, 3, 4, 6, 8 };

static const struct ingenic_cgu_clk_info t31_clk_info[] = {
        [CLK_EXT] = { "ext", CGU_CLK_EXT },
        [CLK_RTC_EXT] = { "rtc_ext", CGU_CLK_EXT },

        /* PLL clocks */
        [CLK_PLL_APLL] = {
                .name = "apll", .type = CGU_CLK_PLL, .parents = { CLK_EXT, -1 },
                .pll = {
                        .reg = CPM_CPAPCR,
                        .m_shift = 20, .m_bits = 12, .m_offset = 0,
                        .n_shift = 14, .n_bits = 6, .n_offset = 0,
                        .od_shift = 11, .od_bits = 3, .od_max = 8, .od_encoding = apll_od_encoding,
                        .enable_bit = 8, .stable_bit = 3,
                },
        },
        [CLK_PLL_MPLL] = {
                .name = "mpll", .type = CGU_CLK_PLL, .parents = { CLK_EXT, -1 },
                .pll = {
                        .reg = CPM_CPMPCR,
                        .m_shift = 20, .m_bits = 12, .m_offset = 0,
                        .n_shift = 14, .n_bits = 6, .n_offset = 0,
                        .od_shift = 11, .od_bits = 3, .od_max = 8, .od_encoding = mpll_od_encoding,
                        .enable_bit = 8, .stable_bit = 3,
                },
        },
        [CLK_PLL_VPLL] = {
                .name = "vpll", .type = CGU_CLK_PLL, .parents = { CLK_EXT, -1 },
                .pll = {
                        .reg = CPM_CPVPCR,
                        .m_shift = 20, .m_bits = 12, .m_offset = 0,
                        .n_shift = 14, .n_bits = 6, .n_offset = 0,
                        .od_shift = 11, .od_bits = 3, .od_max = 8, .od_encoding = vpll_od_encoding,
                        .enable_bit = 8, .stable_bit = 3,
                },
        },

        /* Mux clocks */
        [CLK_MUX_SCLKA] = {
                .name = "sclka", .type = CGU_CLK_MUX, .parents = { -1, CLK_EXT, CLK_PLL_APLL },
                .mux = { .reg = CPM_CPCCR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_CPU_L2C] = {
                .name = "mux_cpu_l2c", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_CPCCR, .shift = 28, .bits = 2 },
        },
        [CLK_MUX_AHB0] = {
                .name = "mux_ahb0", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_CPCCR, .shift = 26, .bits = 2 },
        },
        [CLK_MUX_AHB2] = {
                .name = "mux_ahb2", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_CPCCR, .shift = 24, .bits = 2 },
        },
        [CLK_MUX_DDR] = {
                .name = "mux_ddr", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_DDRCDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_EL150] = {
                .name = "mux_el150", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_EL150CDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_RSA] = {
                .name = "mux_rsa", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_RSACDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_MACPHY] = {
                .name = "mux_macphy", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_MACCDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_LCD] = {
                .name = "mux_lcd", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_LPCDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_MSC0] = {
                .name = "mux_msc0", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_MSC0CDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_MSC1] = {
                .name = "mux_msc1", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_MSC1CDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_SSI] = {
                .name = "mux_ssi", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_SSICDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_I2ST] = {
                .name = "mux_i2st", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_I2STCDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_ISP] = {
                .name = "mux_isp", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_ISPCDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_I2SR] = {
                .name = "mux_i2sr", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_I2SRCDR, .shift = 30, .bits = 2 },
        },
        [CLK_MUX_CIM] = {
                .name = "mux_cim", .type = CGU_CLK_MUX, .parents = { -1, CLK_MUX_SCLKA, CLK_PLL_MPLL },
                .mux = { .reg = CPM_CIMCDR, .shift = 30, .bits = 2 },
        },


/* Bus dividers */
        [CLK_DIV_CPU] = {
                .name = "div_cpu", .type = CGU_CLK_DIV, .parents = { CLK_MUX_CPU_L2C, -1 },
                .div = { .reg = CPM_CPCCR, .shift = 0, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_L2C] = {
                .name = "div_l2c", .type = CGU_CLK_DIV, .parents = { CLK_MUX_CPU_L2C, -1 },
                .div = { .reg = CPM_CPCCR, .shift = 4, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_AHB0] = {
                .name = "div_ahb0", .type = CGU_CLK_DIV, .parents = { CLK_MUX_AHB0, -1 },
                .div = { .reg = CPM_CPCCR, .shift = 8, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_AHB2] = {
                .name = "div_ahb2", .type = CGU_CLK_DIV, .parents = { CLK_MUX_AHB2, -1 },
                .div = { .reg = CPM_CPCCR, .shift = 12, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_APB] = {
                .name = "div_apb", .type = CGU_CLK_DIV, .parents = { CLK_MUX_AHB2, -1 },
                .div = { .reg = CPM_CPCCR, .shift = 16, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_CPU_L2C_X1] = {
                .name = "div_cpu_l2c_x1", .type = CGU_CLK_DIV, .parents = { CLK_MUX_CPU_L2C, -1 },
                .div = { .reg = CPM_CPCCR, .shift = 0, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_CPU_L2C_X2] = {
                .name = "div_cpu_l2c_x2", .type = CGU_CLK_DIV, .parents = { CLK_MUX_CPU_L2C, -1 },
                .div = { .reg = CPM_CPCCR, .shift = 0, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },


/* Regular dividers */
        [CLK_DIV_DDR] = {
                .name = "div_ddr", .type = CGU_CLK_DIV, .parents = { CLK_MUX_DDR, -1 },
                .div = { .reg = CPM_DDRCDR, .shift = 4, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_MACPHY] = {
                .name = "div_macphy", .type = CGU_CLK_DIV, .parents = { CLK_MUX_MACPHY, -1 },
                .div = { .reg = CPM_MACCDR, .shift = 8, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_LCD] = {
                .name = "div_lcd", .type = CGU_CLK_DIV, .parents = { CLK_MUX_LCD, -1 },
                .div = { .reg = CPM_LPCDR, .shift = 8, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_MSC0] = {
                .name = "div_msc0", .type = CGU_CLK_DIV, .parents = { CLK_MUX_MSC0, -1 },
                .div = { .reg = CPM_MSC0CDR, .shift = 8, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_MSC1] = {
                .name = "div_msc1", .type = CGU_CLK_DIV, .parents = { CLK_MUX_MSC1, -1 },
                .div = { .reg = CPM_MSC1CDR, .shift = 8, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_SFC] = {
                .name = "div_sfc", .type = CGU_CLK_DIV, .parents = { CLK_MUX_SSI, -1 },
                .div = { .reg = CPM_SSICDR, .shift = 8, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_SSI] = {
                .name = "div_ssi", .type = CGU_CLK_DIV, .parents = { CLK_MUX_SSI, -1 },
                .div = { .reg = CPM_SSICDR, .shift = 8, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_CIM] = {
                .name = "div_cim", .type = CGU_CLK_DIV, .parents = { CLK_MUX_CIM, -1 },
                .div = { .reg = CPM_CIMCDR, .shift = 8, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_ISP] = {
                .name = "div_isp", .type = CGU_CLK_DIV, .parents = { CLK_MUX_ISP, -1 },
                .div = { .reg = CPM_ISPCDR, .shift = 4, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_RSA] = {
                .name = "div_rsa", .type = CGU_CLK_DIV, .parents = { CLK_MUX_RSA, -1 },
                .div = { .reg = CPM_RSACDR, .shift = 4, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_EL150] = {
                .name = "div_el150", .type = CGU_CLK_DIV, .parents = { CLK_MUX_EL150, -1 },
                .div = { .reg = CPM_EL150CDR, .shift = 4, .bits = 4, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },


/* Fractional dividers */
        [CLK_DIV_I2ST] = {
                .name = "div_i2st", .type = CGU_CLK_DIV, .parents = { CLK_MUX_I2ST, -1 },
                .div = { .reg = CPM_I2STCDR, .shift = 20, .bits = 9, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },
        [CLK_DIV_I2SR] = {
                .name = "div_i2sr", .type = CGU_CLK_DIV, .parents = { CLK_MUX_I2SR, -1 },
                .div = { .reg = CPM_I2SRCDR, .shift = 20, .bits = 9, .ce_bit = -1, .busy_bit = -1, .stop_bit = -1 },
        },

        /* Gates */
        [CLK_GATE_DDR] = {
                .name = "gate_ddr", .type = CGU_CLK_GATE, .parents = { CLK_DIV_DDR, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 31 },
        },
        [CLK_GATE_TCU] = {
                .name = "gate_tcu", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 30 },
        },
        [CLK_GATE_DES] = {
                .name = "gate_des", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 28 },
        },
        [CLK_GATE_RSA] = {
                .name = "gate_rsa", .type = CGU_CLK_GATE, .parents = { CLK_DIV_RSA, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 27 },
        },
        [CLK_GATE_RISCV] = {
                .name = "gate_riscv", .type = CGU_CLK_GATE, .parents = { CLK_DIV_CPU, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 26 },
        },
        [CLK_GATE_MIPI_CSI] = {
                .name = "gate_csi", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB0, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 25 },
        },
        [CLK_GATE_LCD] = {
                .name = "gate_lcd", .type = CGU_CLK_GATE, .parents = { CLK_DIV_LCD, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 24 },
        },
        [CLK_GATE_ISP] = {
                .name = "gate_isp", .type = CGU_CLK_GATE, .parents = { CLK_DIV_ISP, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 23 },
        },
        [CLK_GATE_PDMA] = {
                .name = "gate_pdma", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB2, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 21 },
        },
        [CLK_GATE_SFC] = {
                .name = "gate_sfc", .type = CGU_CLK_GATE, .parents = { CLK_DIV_SFC, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 20 },
        },
        [CLK_GATE_SSI1] = {
                .name = "gate_ssi1", .type = CGU_CLK_GATE, .parents = { CLK_DIV_SSI, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 19 },
        },
        [CLK_GATE_HASH] = {
                .name = "gate_hash", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB2, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 18 },
        },
        [CLK_GATE_SLV] = {
                .name = "gate_slv", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 17 },
        },
        [CLK_GATE_UART2] = {
                .name = "gate_uart2", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 16 },
        },
        [CLK_GATE_UART1] = {
                .name = "gate_uart1", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 15 },
        },
        [CLK_GATE_UART0] = {
                .name = "gate_uart0", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 14 },
        },
        [CLK_GATE_SADC] = {
                .name = "gate_sadc", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 13 },
        },
        [CLK_GATE_DMIC] = {
                .name = "gate_dmic", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 12 },
        },
        [CLK_GATE_AIC] = {
                .name = "gate_aic", .type = CGU_CLK_GATE, .parents = { CLK_DIV_I2ST, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 11 },
        },
        [CLK_GATE_SMB1] = {
                .name = "gate_i2c1", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 8 },
        },
        [CLK_GATE_SMB0] = {
                .name = "gate_i2c0", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 7 },
        },
        [CLK_GATE_SSI0] = {
                .name = "gate_ssi0", .type = CGU_CLK_GATE, .parents = { CLK_DIV_SSI, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 6 },
        },
        [CLK_GATE_MSC1] = {
                .name = "gate_msc1", .type = CGU_CLK_GATE, .parents = { CLK_DIV_MSC1, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 5 },
        },
        [CLK_GATE_MSC0] = {
                .name = "gate_msc0", .type = CGU_CLK_GATE, .parents = { CLK_DIV_MSC0, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 4 },
        },
        [CLK_GATE_OTG] = {
                .name = "gate_otg", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB2, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 3 },
        },
        [CLK_GATE_EFUSE] = {
                .name = "gate_efuse", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB2, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 1 },
        },
        [CLK_GATE_NEMC] = {
                .name = "gate_nemc", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB2, -1 },
                .gate = { .reg = CPM_CLKGR, .bit = 0 },
        },
        [CLK_GATE_CPU] = {
                .name = "gate_cpu", .type = CGU_CLK_GATE, .parents = { CLK_DIV_CPU, -1 },
                .gate = { .reg = CPM_CLKGR1, .bit = 15 },
        },
        [CLK_GATE_APB0] = {
                .name = "gate_apb0", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB0, -1 },
                .gate = { .reg = CPM_CLKGR1, .bit = 14 },
        },
        [CLK_GATE_OST] = {
                .name = "gate_ost", .type = CGU_CLK_GATE, .parents = { CLK_EXT, -1 },
                .gate = { .reg = CPM_CLKGR1, .bit = 11 },
        },
        [CLK_GATE_AHB0] = {
                .name = "gate_ahb0", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB0, -1 },
                .gate = { .reg = CPM_CLKGR1, .bit = 10 },
        },
        [CLK_GATE_AHB1] = {
                .name = "gate_ahb1", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB2, -1 },
                .gate = { .reg = CPM_CLKGR1, .bit = 6 },
        },
        [CLK_GATE_AES] = {
                .name = "gate_aes", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB2, -1 },
                .gate = { .reg = CPM_CLKGR1, .bit = 5 },
        },
        [CLK_GATE_GMAC] = {
                .name = "gate_gmac", .type = CGU_CLK_GATE, .parents = { CLK_DIV_MACPHY, -1 },
                .gate = { .reg = CPM_CLKGR1, .bit = 4 },
        },
        [CLK_GATE_IPU] = {
                .name = "gate_ipu", .type = CGU_CLK_GATE, .parents = { CLK_DIV_AHB0, -1 },
                .gate = { .reg = CPM_CLKGR1, .bit = 2 },
        },
        [CLK_GATE_DTRNG] = {
                .name = "gate_dtrng", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_CLKGR1, .bit = 1 },
        },
        [CLK_GATE_EL150] = {
                .name = "gate_el150", .type = CGU_CLK_GATE, .parents = { CLK_DIV_EL150, -1 },
                .gate = { .reg = CPM_CLKGR1, .bit = 0 },
        },
        [CLK_CE_I2ST] = {
                .name = "ce_i2st", .type = CGU_CLK_GATE, .parents = { CLK_DIV_I2ST, -1 },
                .gate = { .reg = CPM_I2STCDR, .bit = 29 },
        },
        [CLK_CE_I2SR] = {
                .name = "ce_i2sr", .type = CGU_CLK_GATE, .parents = { CLK_DIV_I2SR, -1 },
                .gate = { .reg = CPM_I2SRCDR, .bit = 29 },
        },
        [CLK_GATE_USBPHY] = {
                .name = "gate_usbphy", .type = CGU_CLK_GATE, .parents = { CLK_DIV_APB, -1 },
                .gate = { .reg = CPM_OPCR, .bit = 23 },
        },

};


static struct ingenic_fixed_rate_clock t31_fixed_rate_clks[] __initdata = {
        { .name = "ext", .parent_name = NULL, .flags = CLK_IS_ROOT, .fixed_rate = 24000000 },
        { .name = "rtc_ext", .parent_name = NULL, .flags = CLK_IS_ROOT, .fixed_rate = 32768 },
};

static struct ingenic_cgu *cgu;

static inline u32 cgu_read(struct ingenic_cgu *cgu, unsigned reg)
{
    return readl(cgu->base + reg);
}

static int clocks_show(struct seq_file *m, void *v)
{
    int i;
    struct clk *clk;

    if (m->private != NULL) {
        /* Print register values */
        seq_printf(m, "CLKGR\t: %08x\n", cgu_read(cgu, CPM_CLKGR));
        seq_printf(m, "CLKGR1\t: %08x\n", cgu_read(cgu, CPM_CLKGR1));
        seq_printf(m, "LCR1\t: %08x\n", cgu_read(cgu, CPM_LCR));
    } else {
        seq_printf(m, " ID  NAME              FRE          sta     count   parent\n");
        for (i = 0; i < cgu->clocks.clk_num; i++) {
            clk = cgu->clocks.clks[i];
            if (!IS_ERR(clk)) {
                unsigned int rate = clk_get_rate(clk) / 1000;
                seq_printf(m, "%3d %-15s %4d.%03dMHz %7sable   %d %10s\n",
                           i, __clk_get_name(clk),
                           rate / 1000, rate % 1000,
                           __clk_is_enabled(clk) ? "en" : "dis",
                           __clk_get_enable_count(clk),
                           clk_get_parent(clk) ? __clk_get_name(clk_get_parent(clk)) : "root");
            }
        }
    }

    return 0;
}

static int clocks_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, clocks_show, PDE_DATA(inode),8192);
}

static const struct file_operations clocks_proc_fops ={
	.read = seq_read,
	.open = clocks_open,
	.llseek = seq_lseek,
	.release = single_release,
};

/* Register t31 clocks. */
static void __init t31_clk_init(struct device_node *np)
{
    cgu = ingenic_cgu_new(t31_clk_info, ARRAY_SIZE(t31_clk_info), np);
    if (!cgu) {
        pr_err("%s: failed to initialize CGU\n", __func__);
        return;
    }

    if (ingenic_cgu_register_clocks(cgu)) {
        pr_err("%s: failed to register clocks\n", __func__);
        return;
    }

    /* Print clock rates or perform other operations */
    pr_info("apll = %lu, mpll = %lu, cpu_clk = %lu, ...\n",
            clk_get_rate(cgu->clocks.clks[CLK_PLL_APLL]),
            clk_get_rate(cgu->clocks.clks[CLK_PLL_MPLL]),
            clk_get_rate(cgu->clocks.clks[CLK_DIV_CPU]));
}

CLK_OF_DECLARE(t31_clk, "ingenic,t31-clocks", t31_clk_init);

static int __init init_clk_proc(void)
{
	struct proc_dir_entry *p;

	p = jz_proc_mkdir("clock");
	if (!p) {
		pr_warning("create_proc_entry for common clock failed!\n");
	} else {
		proc_create_data("clocks", 0600, p, &clocks_proc_fops, 0);
		proc_create_data("misc", 0600, p, &clocks_proc_fops, (void *)1);
	}
	return 0;
}

module_init(init_clk_proc);
