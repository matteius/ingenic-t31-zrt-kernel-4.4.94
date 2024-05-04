/*
 * Ingenic JZ4740 SoC CGU driver
 *
 * Copyright (c) 2015 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com> (gave inspiration to)
 * Author: Matt Davis <matteius@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/clkdev.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <dt-bindings/clock/ingenic-t31.h>
#include <asm/mach-jz4740/clock.h>
#include <soc/cpm.h>
#include <soc/base.h>

#include "cgu.h"


/* CGU register offsets */
#define CGU_REG_CPCCR		0x00
#define CGU_REG_LCR		0x04
#define CGU_REG_CPPCR		0x10
#define CGU_REG_CLKGR		0x20
#define CGU_REG_SCR		0x24
#define CGU_REG_I2SCDR		0x60
#define CGU_REG_LPCDR		0x64
#define CGU_REG_MSCCDR		0x68
#define CGU_REG_UHCCDR		0x6c
#define CGU_REG_SSICDR		0x74

/* bits within a PLL control register */
#define PLLCTL_M_SHIFT		23
#define PLLCTL_M_MASK		(0x1ff << PLLCTL_M_SHIFT)
#define PLLCTL_N_SHIFT		18
#define PLLCTL_N_MASK		(0x1f << PLLCTL_N_SHIFT)
#define PLLCTL_OD_SHIFT		16
#define PLLCTL_OD_MASK		(0x3 << PLLCTL_OD_SHIFT)
#define PLLCTL_STABLE		(1 << 10)
#define PLLCTL_BYPASS		(1 << 9)
#define PLLCTL_ENABLE		(1 << 8)

/* bits within the LCR register */
#define LCR_SLEEP		(1 << 0)

/* bits within the CLKGR register */
#define CLKGR_UDC		(1 << 11)


static struct ingenic_cgu *cgu;


static const s8 t31_pll_od_encoding[4] = {
        0x0, 0x1, -1, 0x3,
};

static const struct ingenic_cgu_clk_info t31_cgu_clocks[] = {

        /* Fixed Rate Clocks */

        [CLK_EXT] = { "ext", CGU_CLK_EXT },
        [CLK_RTC_EXT] = { "rtc_ext", CGU_CLK_EXT },

        /* PLL Clocks */

#define DEF_PLL(name) { \
	.reg = CPM_ ## name, \
    .m_shift = 20, \
    .m_bits = 12, \
    .m_offset = 0, \
    .n_shift = 14, \
    .n_bits = 6, \
    .n_offset = 0, \
    .od_shift = 11, \
    .od_bits = 3, \
    .od_max = 8, \
    .od_encoding = t31_pll_od_encoding, \
    .stable_bit = 3, \
    .bypass_bit = 0, \
    .enable_bit = 2, \
}

        [CLK_PLL_APLL] = {
                "apll", CGU_CLK_PLL,
                .parents = { CLK_EXT, -1, -1, -1 },
                .pll = DEF_PLL(CPAPCR),
        },

        [CLK_PLL_MPLL] = {
                "mpll", CGU_CLK_PLL,
                .parents = { CLK_EXT, -1, -1, -1 },
                .pll = DEF_PLL(CPMPCR),
        },

        [CLK_PLL_VPLL] = {
                "vpll", CGU_CLK_PLL,
                .parents = { CLK_EXT, -1, -1, -1 },
                .pll = DEF_PLL(CPVPCR),
        },
#undef DEF_PLL
        /* Mux Clocks */

        [CLK_MUX_SCLKA] = {
                "sclk_a", CGU_CLK_MUX,
                .parents = {CLK_PLL_APLL, CLK_EXT, CLK_RTC_EXT, -1 },
                .mux = {
                        .reg = CPM_CPCCR,
                        .shift = 30,
                        .bits = 2,
                },
        },


        [CLK_MUX_CPU_L2C] = {
                "mux_cpu_l2c", CGU_CLK_MUX,
                .parents = { CPM_CPCCR, CLK_MUX_SCLKA, CLK_PLL_MPLL, -1 },
                .mux = {
                        .reg = CPM_CPCCR,
                        .shift = 28,
                        .bits = 2,
                },
        },

        [CLK_MUX_AHB0] = {
                "mux_ahb0", CGU_CLK_MUX,
                .parents = { CPM_CPCCR, CLK_MUX_SCLKA, CLK_PLL_MPLL, -1 },
                .mux = {
                        .reg = CPM_CPCCR,
                        .shift = 26,
                        .bits = 2,
                },
        },

        [CLK_MUX_AHB2] = {
                "mux_ahb2", CGU_CLK_MUX,
                .parents = { CPM_CPCCR, CLK_MUX_SCLKA, CLK_PLL_MPLL, -1 },
                .mux = {
                        .reg = CPM_CPCCR,
                        .shift = 24,
                        .bits = 2,
                },
        },

        [CLK_MUX_DDR] = {
                "mux_ddr", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_DDRCDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_EL150] = {
                "mux_el150", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_EL150CDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_RSA] = {
                "mux_rsa", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_RSACDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_MACPHY] = {
                "mux_macphy", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_MACCDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_LCD] = {
                "mux_lcd", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_LPCDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_MSC0] = {
                "mux_msc0", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_MSC0CDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_MSC1] = {
                "mux_msc1", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_MSC1CDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_SSI] = {
                "mux_ssi", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_SSICDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_I2ST] = {
                "mux_i2st", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_I2STCDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_ISP] = {
                "mux_isp", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_ISPCDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_I2SR] = {
                "mux_i2sr", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_I2SRCDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        [CLK_MUX_CIM] = {
                "mux_cim", CGU_CLK_MUX,
                .parents = { CLK_MUX_SCLKA, CLK_PLL_MPLL, CLK_PLL_VPLL, -1 },
                .mux = {
                        .reg = CPM_CIMCDR,
                        .shift = 30,
                        .bits = 2,
                },
        },

        /* Divider Clocks */

        [CLK_DIV_CPU] = {
                "div_cpu", CGU_CLK_DIV,
                .parents = { CLK_MUX_CPU_L2C, -1, -1, -1 },
                .div = {
                        .reg = CPM_CPCCR,
                        .shift = 0,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = 0,
                        .busy_bit = 22,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_L2C] = {
                "div_l2c", CGU_CLK_DIV,
                .parents = { CLK_MUX_CPU_L2C, -1, -1, -1 },
                .div = {
                        .reg = CPM_CPCCR,
                        .shift = 4,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = 0,
                        .busy_bit = 22,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_AHB0] = {
                "div_ahb0", CGU_CLK_DIV,
                .parents = { CLK_MUX_AHB0, -1, -1, -1 },
                .div = {
                        .reg = CPM_CPCCR,
                        .shift = 8,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = 1,
                        .busy_bit = 21,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_AHB2] = {
                "div_ahb2", CGU_CLK_DIV,
                .parents = { CLK_MUX_AHB2, -1, -1, -1 },
                .div = {
                        .reg = CPM_CPCCR,
                        .shift = 12,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = 2,
                        .busy_bit = 20,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_APB] = {
                "div_apb", CGU_CLK_DIV,
                .parents = { CLK_MUX_AHB2, -1, -1, -1 },
                .div = {
                        .reg = CPM_CPCCR,
                        .shift = 16,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = 2,
                        .busy_bit = 20,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_CPU_L2C_X1] = {
                "div_cpu_l2c_x1", CGU_CLK_DIV,
                .parents = { CLK_MUX_CPU_L2C, -1, -1, -1 },
                .div = {
                        .reg = CPM_CPCCR,
                        .shift = 0,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = 4,
                        .busy_bit = 22,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_CPU_L2C_X2] = {
                "div_cpu_l2c_x2", CGU_CLK_DIV,
                .parents = { CLK_MUX_CPU_L2C, -1, -1, -1 },
                .div = {
                        .reg = CPM_CPCCR,
                        .shift = 0,
                        .div = 2,
                        .bits = 4,
                        .ce_bit = 4,
                        .busy_bit = 22,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_DDR] = {
                "div_ddr", CGU_CLK_DIV,
                .parents = { CLK_MUX_DDR, -1, -1, -1 },
                .div = {
                        .reg = CPM_DDRCDR,
                        .shift = 0,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_MACPHY] = {
                "div_macphy", CGU_CLK_DIV,
                .parents = { CLK_MUX_MACPHY, -1, -1, -1 },
                .div = {
                        .reg = CPM_MACCDR,
                        .shift = 8,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_LCD] = {
                "div_lcd", CGU_CLK_DIV,
                .parents = { CLK_MUX_LCD, -1, -1, -1 },
                .div = {
                        .reg = CPM_LPCDR,
                        .shift = 8,
                        .div = 1,
                        .bits = 5,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_MSC0] = {
                "div_msc0", CGU_CLK_DIV,
                .parents = { CLK_MUX_MSC0, -1, -1, -1 },
                .div = {
                        .reg = CPM_MSC0CDR,
                        .shift = 8,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_MSC1] = {
                "div_msc1", CGU_CLK_DIV,
                .parents = { CLK_MUX_MSC1, -1, -1, -1 },
                .div = {
                        .reg = CPM_MSC1CDR,
                        .shift = 8,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_SFC] = {
                "div_sfc", CGU_CLK_DIV,
                .parents = { CLK_MUX_SSI, -1, -1, -1 },
                .div = {
                        .reg = CPM_SSICDR,
                        .shift = 8,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_SSI] = {
                "div_ssi", CGU_CLK_DIV,
                .parents = { CLK_MUX_SSI, -1, -1, -1 },
                .div = {
                        .reg = CPM_SSICDR,
                        .shift = 8,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_CIM] = {
                "div_cim", CGU_CLK_DIV,
                .parents = { CLK_MUX_CIM, -1, -1, -1 },
                .div = {
                        .reg = CPM_CIMCDR,
                        .shift = 8,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_ISP] = {
                "div_isp", CGU_CLK_DIV,
                .parents = { CLK_MUX_ISP, -1, -1, -1 },
                .div = {
                        .reg = CPM_ISPCDR,
                        .shift = 4,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_RSA] = {
                "div_rsa", CGU_CLK_DIV,
                .parents = { CLK_MUX_RSA, -1, -1, -1 },
                .div = {
                        .reg = CPM_RSACDR,
                        .shift = 4,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_EL150] = {
                "div_el150", CGU_CLK_DIV,
                .parents = { CLK_MUX_EL150, -1, -1, -1 },
                .div = {
                        .reg = CPM_EL150CDR,
                        .shift = 4,
                        .div = 1,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        /* Fractional Divider Clocks */

        [CLK_DIV_I2ST] = {
                "div_i2st", CGU_CLK_DIV,
                .parents = { CLK_MUX_I2ST, -1, -1, -1 },
                .div = {
                        .reg = CPM_I2STCDR,
                        .shift = 20,
                        .div = 9,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        [CLK_DIV_I2SR] = {
                "div_i2sr", CGU_CLK_DIV,
                .parents = { CLK_MUX_I2SR, -1, -1, -1 },
                .div = {
                        .reg = CPM_I2SRCDR,
                        .shift = 20,
                        .div = 9,
                        .bits = 4,
                        .ce_bit = -1,
                        .busy_bit = -1,
                        .stop_bit = -1,
                },
        },

        /* Gate Clocks */

        [CLK_GATE_DDR] = {
                "gate_ddr", CGU_CLK_GATE,
                .parents = { CLK_DIV_DDR, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 31,
                },
        },

        [CLK_GATE_TCU] = {
                "gate_tcu", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 30,
                },
        },

        [CLK_GATE_DES] = {
                "gate_des", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 28,
                },
        },

        [CLK_GATE_RSA] = {
                "gate_rsa", CGU_CLK_GATE,
                .parents = { CLK_DIV_RSA, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 27,
                },
        },

        [CLK_GATE_RISCV] = {
                "gate_riscv", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 26,
                },
        },

        [CLK_GATE_MIPI_CSI] = {
                "gate_csi", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB0, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 25,
                },
        },

        [CLK_GATE_LCD] = {
                "gate_lcd", CGU_CLK_GATE,
                .parents = { CLK_DIV_LCD, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 24,
                },
        },

        [CLK_GATE_ISP] = {
                "gate_isp", CGU_CLK_GATE,
                .parents = { CLK_DIV_ISP, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 23,
                },
        },

        [CLK_GATE_PDMA] = {
                "gate_pdma", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB2, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 21,
                },
        },

        [CLK_GATE_SFC] = {
                "gate_sfc", CGU_CLK_GATE,
                .parents = { CLK_DIV_SFC, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 20,
                },
        },

        [CLK_GATE_SSI1] = {
                "gate_ssi1", CGU_CLK_GATE,
                .parents = { CLK_DIV_SSI, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 19,
                },
        },

        [CLK_GATE_HASH] = {
                "gate_hash", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB2, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 18,
                },
        },

        [CLK_GATE_SLV] = {
                "gate_slv", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 17,
                },
        },

        [CLK_GATE_UART2] = {
                "gate_uart2", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 16,
                },
        },

        [CLK_GATE_UART1] = {
                "gate_uart1", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 15,
                },
        },

        [CLK_GATE_UART0] = {
                "gate_uart0", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 14,
                },
        },

        [CLK_GATE_SADC] = {
                "gate_sadc", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 13,
                },
        },

        [CLK_GATE_DMIC] = {
                "gate_dmic", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 12,
                },
        },

        [CLK_GATE_AIC] = {
                "gate_aic", CGU_CLK_GATE,
                .parents = { CLK_DIV_I2ST, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 11,
                },
        },

        [CLK_GATE_SMB1] = {
                "gate_i2c1", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 8,
                },
        },

        [CLK_GATE_SMB0] = {
                "gate_i2c0", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 7,
                },
        },

        [CLK_GATE_SSI0] = {
                "gate_ssi0", CGU_CLK_GATE,
                .parents = { CLK_DIV_SSI, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 6,
                },
        },

        [CLK_GATE_MSC1] = {
                "gate_msc1", CGU_CLK_GATE,
                .parents = { CLK_DIV_MSC1, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 5,
                },
        },

        [CLK_GATE_MSC0] = {
                "gate_msc0", CGU_CLK_GATE,
                .parents = { CLK_DIV_MSC0, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 4,
                },
        },

        [CLK_GATE_OTG] = {
                "gate_otg", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB2, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 3,
                },
        },

        [CLK_GATE_EFUSE] = {
                "gate_efuse", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB2, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 1,
                },
        },

        [CLK_GATE_NEMC] = {
                "gate_nemc", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB2, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR,
                        .bit = 0,
                },
        },

        [CLK_GATE_CPU] = {
                "gate_cpu", CGU_CLK_GATE,
                .parents = { CLK_DIV_CPU, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR1,
                        .bit = 15,
                },
        },

        [CLK_GATE_APB0] = {
                "gate_apb0", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB0, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR1,
                        .bit = 14,
                },
        },

        [CLK_GATE_OST] = {
                "gate_ost", CGU_CLK_GATE,
                .parents = { CLK_EXT, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR1,
                        .bit = 11,
                },
        },

        [CLK_GATE_AHB0] = {
                "gate_ahb0", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB0, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR1,
                        .bit = 10,
                },
        },

        [CLK_GATE_AHB1] = {
                "gate_ahb1", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB2, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR1,
                        .bit = 6,
                },
        },

        [CLK_GATE_AES] = {
                "gate_aes", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB2, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR1,
                        .bit = 5,
                },
        },

        [CLK_GATE_GMAC] = {
                "gate_gmac", CGU_CLK_GATE,
                .parents = { CLK_DIV_MACPHY, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR1,
                        .bit = 4,
                },
        },

        [CLK_GATE_IPU] = {
                "gate_ipu", CGU_CLK_GATE,
                .parents = { CLK_DIV_AHB0, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR1,
                        .bit = 2,
                },
        },

        [CLK_GATE_DTRNG] = {
                "gate_dtrng", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR1,
                        .bit = 1,
                },
        },

        [CLK_GATE_EL150] = {
                "gate_el150", CGU_CLK_GATE,
                .parents = { CLK_DIV_EL150, -1, -1, -1 },
                .gate = {
                        .reg = CPM_CLKGR1,
                        .bit = 0,
                },
        },

        [CLK_CE_I2ST] = {
                "ce_i2st", CGU_CLK_GATE,
                .parents = { CLK_DIV_I2ST, -1, -1, -1 },
                .gate = {
                        .reg = CPM_I2STCDR,
                        .bit = 29,
                },
        },

        [CLK_CE_I2SR] = {
                "ce_i2sr", CGU_CLK_GATE,
                .parents = { CLK_DIV_I2SR, -1, -1, -1 },
                .gate = {
                        .reg = CPM_I2SRCDR,
                        .bit = 29,
                },
        },

        [CLK_GATE_USBPHY] = {
                "gate_usbphy", CGU_CLK_GATE,
                .parents = { CLK_DIV_APB, -1, -1, -1 },
                .gate = {
                        .reg = CPM_OPCR,
                        .bit = 23,
                },
        },
};



static void __init xburst_cgu_init(struct device_node *np)
{
    int retval;

    cgu = ingenic_cgu_new(t31_cgu_clocks,
                          ARRAY_SIZE(t31_cgu_clocks), np);
    if (!cgu) {
        pr_err("%s: failed to initialise CGU\n", __func__);
        return;
    }

    retval = ingenic_cgu_register_clocks(cgu);
    if (retval) {
        pr_err("%s: failed to register CGU Clocks\n", __func__);
        return;
    }
}
CLK_OF_DECLARE(jz4780_cgu, "ingenic,xburst-t31-cgu", xburst_cgu_init);