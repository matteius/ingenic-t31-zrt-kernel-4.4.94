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

#define CLK_FLG_ENABLE BIT(1)
static struct ingenic_clk_provider *ctx;


/*******************************************************************************
 *      FIXED CLK
 ********************************************************************************/
static struct ingenic_fixed_rate_clock t31_fixed_rate_ext_clks[] __initdata = {
        FRATE(CLK_EXT, "ext", NULL, CLK_SET_RATE_UNGATE, 24000000),
        FRATE(CLK_RTC_EXT, "rtc_ext", NULL, CLK_SET_RATE_UNGATE, 32768),
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



static struct ingenic_pll_clock t31_pll_clks[] __initdata = {
	PLL(CLK_PLL_APLL, "apll", "ext", &apll_hwdesc, t31_pll_rate_table),
	PLL(CLK_PLL_MPLL, "mpll", "ext", &mpll_hwdesc, t31_pll_rate_table),
	PLL(CLK_PLL_VPLL, "vpll", "ext", &vpll_hwdesc, t31_pll_rate_table),
};


/*******************************************************************************
 *      MUX
 ********************************************************************************/
PNAME(mux_table_0)	= {"stop",	    "ext",		"apll"};
PNAME(mux_table_1)	= {"stop",	    "sclka",	"mpll"};
PNAME(mux_table_2)	= {"sclka",	    "mpll",		"vpll"};
PNAME(mux_table_3)	= {"sclka", 	"mpll",     "ext"};
PNAME(mux_table_4)	= {"sclka", 	"vpll",		"ext"};
PNAME(mux_table_5)	= {"mux_ahb2",	"ext"};

static unsigned int ingenic_mux_table[] = {0, 1, 2, 3};


static struct ingenic_mux_clock t31_mux_clks[] __initdata = {
	MUX(CLK_MUX_SCLKA,	    "sclka",		ingenic_mux_table, mux_table_0, CPM_CPCCR,	    30, 2, 0),
	MUX(CLK_MUX_CPU_L2C,	"mux_cpu_l2c",	ingenic_mux_table, mux_table_1, CPM_CPCCR,	    28, 2, 0),
	MUX(CLK_MUX_AHB0,	    "mux_ahb0",		ingenic_mux_table, mux_table_1, CPM_CPCCR,	    26, 2, 0),
	MUX(CLK_MUX_AHB2,	    "mux_ahb2",		ingenic_mux_table, mux_table_1, CPM_CPCCR,	    24, 2, 0),

	MUX(CLK_MUX_DDR,	    "mux_ddr",		ingenic_mux_table, mux_table_1, CPM_DDRCDR,	    30, 2, 0),
	MUX(CLK_MUX_EL150,	    "mux_el150",	ingenic_mux_table, mux_table_2, CPM_EL150CDR,   30, 2, 0),
	MUX(CLK_MUX_RSA,	    "mux_rsa",		ingenic_mux_table, mux_table_2, CPM_RSACDR,	    30, 2, 0),
	MUX(CLK_MUX_MACPHY,	    "mux_macphy",	ingenic_mux_table, mux_table_2, CPM_MACCDR,	    30, 2, 0),
	MUX(CLK_MUX_LCD,	    "mux_lcd",		ingenic_mux_table, mux_table_2, CPM_LPCDR,	    30, 2, 0),
	MUX(CLK_MUX_MSC0,	    "mux_msc0",		ingenic_mux_table, mux_table_2, CPM_MSC0CDR,	30, 2, 0),
	MUX(CLK_MUX_MSC1,	    "mux_msc1",		ingenic_mux_table, mux_table_2, CPM_MSC0CDR,	30, 2, 0),
	MUX(CLK_MUX_SSI,	    "mux_ssi",		ingenic_mux_table, mux_table_2, CPM_SSICDR,	    30, 2, 0),
	MUX(CLK_MUX_I2ST,	    "mux_i2st",		ingenic_mux_table, mux_table_2, CPM_I2STCDR,    30, 2, 0),
	MUX(CLK_MUX_ISP,	    "mux_isp",		ingenic_mux_table, mux_table_2, CPM_ISPCDR,	    30, 2, 0),
	MUX(CLK_MUX_I2SR,	    "mux_i2sr",		ingenic_mux_table, mux_table_2, CPM_I2STCDR,    30, 2, 0),
	MUX(CLK_MUX_CIM,	    "mux_cim",		ingenic_mux_table, mux_table_2, CPM_CIMCDR,	    30, 2, 0),

};

/*******************************************************************************
 *      DIV BUS
 ********************************************************************************/

static struct ingenic_bus_clock t31_bus_div_clks[] __initdata = {
	BUS_DIV(CLK_DIV_CPU,		"div_cpu",		    "mux_cpu_l2c",		CPM_CPCCR,	0,  4, 0, 0, CPM_CPCSR, 0, 22, BUS_DIV_SELF),
	BUS_DIV(CLK_DIV_L2C,		"div_l2c",		    "mux_cpu_l2c",		CPM_CPCCR,	4,  4, 0, 0, CPM_CPCSR, 0, 22, BUS_DIV_SELF),
	BUS_DIV(CLK_DIV_AHB0,		"div_ahb0",		    "mux_ahb0",		    CPM_CPCCR,	8,  4, 0, 0, CPM_CPCSR, 1, 21, BUS_DIV_SELF),
	BUS_DIV(CLK_DIV_AHB2,		"div_ahb2",		    "mux_ahb2",		    CPM_CPCCR,	12, 4, 0, 0, CPM_CPCSR, 2, 20, BUS_DIV_SELF),
	BUS_DIV(CLK_DIV_APB,		"div_apb",		    "mux_ahb2",		    CPM_CPCCR,	16, 4, 0, 0, CPM_CPCSR, 2, 20, BUS_DIV_SELF),
	BUS_DIV(CLK_DIV_CPU_L2C_X1,	"div_cpu_l2c_x1",	"mux_cpu_l2c",		CPM_CPCCR,	0,  4, 4, 4, CPM_CPCSR, 0, 22, BUS_DIV_ONE),
	BUS_DIV(CLK_DIV_CPU_L2C_X2,	"div_cpu_l2c_x2",	"mux_cpu_l2c",		CPM_CPCCR,	0,  4, 4, 4, CPM_CPCSR, 0, 22, BUS_DIV_TWO),
};


/*******************************************************************************
 *      DIV
 ********************************************************************************/

static struct clk_div_table t31_clk_div_table[256] = {};

static struct ingenic_div_clock t31_div_clks[] __initdata = {
	DIV(CLK_DIV_DDR,		"div_ddr",		"mux_ddr",		CPM_DDRCDR,	    4,  0, NULL),
	DIV(CLK_DIV_MACPHY,		"div_macphy",	"mux_macphy",	CPM_MACCDR,	    8,  0, NULL),
	DIV(CLK_DIV_LCD,		"div_lcd",		"mux_lcd",		CPM_LPCDR,	    8,  0, NULL),
	DIV(CLK_DIV_MSC0,		"div_msc0",		"mux_msc0",		CPM_MSC0CDR,	8,  0, t31_clk_div_table),
	DIV(CLK_DIV_MSC1,		"div_msc1",		"mux_msc1",		CPM_MSC1CDR,	8,  0, t31_clk_div_table),
	DIV(CLK_DIV_SFC,		"div_sfc",		"mux_ssi",		CPM_SSICDR,	    8,  0, NULL),
	DIV(CLK_DIV_SSI,		"div_ssi",		"mux_ssi",		CPM_SSICDR,	    8,  0, NULL),
	DIV(CLK_DIV_CIM,		"div_cim",		"mux_cim",		CPM_CIMCDR,	    8,  0, NULL),
	DIV(CLK_DIV_ISP,		"div_isp",		"mux_isp",		CPM_ISPCDR,	    4,  0, NULL),
	DIV(CLK_DIV_RSA,		"div_rsa",		"mux_rsa",		CPM_RSACDR,	    4,  0, NULL),
	DIV(CLK_DIV_EL150,		"div_el150",	"mux_el150",	CPM_EL150CDR,   4,  0, NULL),

};

/*******************************************************************************
 *      FRACTIONAL-DIVIDER
 ********************************************************************************/

static struct ingenic_fra_div_clock t31_fdiv_clks[] __initdata = {
	FRA_DIV(CLK_DIV_I2ST,	"div_i2st",	"mux_i2st",	CPM_I2STCDR,	20, 9, 0, 20),
	FRA_DIV(CLK_DIV_I2SR,	"div_i2sr",	"mux_i2sr",	CPM_I2SRCDR,	20, 9, 0, 20),
};

/*******************************************************************************
 *      GATE
 ********************************************************************************/
static struct ingenic_gate_clock t31_gate_clks[] __initdata = {

	GATE(CLK_GATE_DDR,	    "gate_ddr",		    "div_ddr",	    CPM_CLKGR,  31, CLK_IGNORE_UNUSED,	CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_TCU,	    "gate_tcu",		    "div_apb",	    CPM_CLKGR,  30, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_DES,	    "gate_des",		    "div_apb",	    CPM_CLKGR,  28, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_RSA,	    "gate_rsa",		    "div_rsa",	    CPM_CLKGR,  27, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_RISCV,    "gate_riscv",		"div_riscv",	CPM_CLKGR,  26, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_MIPI_CSI, "gate_csi",		    "div_ahb0",	    CPM_CLKGR,  25, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_LCD,	    "gate_lcd",		    "div_lcd",	    CPM_CLKGR,  24, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_ISP,	    "gate_isp",		    "div_isp",	    CPM_CLKGR,  23, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_PDMA,	    "gate_pdma",		"div_ahb2",	    CPM_CLKGR,  21, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_SFC,	    "gate_sfc",		    "div_sfc",	    CPM_CLKGR,  20, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_SSI1,	    "gate_ssi1",		"div_ssi",	    CPM_CLKGR,  19, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_HASH,	    "gate_hash",		"div_ahb2",	    CPM_CLKGR,  18, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_SLV,	    "gate_slv",		    "div_apb",	    CPM_CLKGR,  17, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_UART2,	"gate_uart2",		"div_apb",	    CPM_CLKGR,  16, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_UART1,	"gate_uart1",		"div_apb",	    CPM_CLKGR,  15, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_UART0,	"gate_uart0",		"div_apb",	    CPM_CLKGR,  14, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_SADC,	    "gate_sadc",		"div_apb",	    CPM_CLKGR,  13, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_DMIC,	    "gate_dmic",		"mux_apb",	    CPM_CLKGR,  12, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_AIC,	    "gate_aic",		    "div_i2st",	    CPM_CLKGR,  11, 0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_SMB1,	    "gate_i2c1",		"div_apb",	    CPM_CLKGR,  8,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_SMB0,	    "gate_i2c0",		"div_apb",	    CPM_CLKGR,  7,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_SSI0,	    "gate_ssi0",		"div_ssi",	    CPM_CLKGR,  6,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_MSC1,	    "gate_msc1",		"div_msc1",	    CPM_CLKGR,  5,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_MSC0,	    "gate_msc0",		"div_msc0",	    CPM_CLKGR,  4,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_OTG,	    "gate_otg",		    "div_ahb2",	    CPM_CLKGR,  3,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_EFUSE,	"gate_efuse",		"div_ahb2",	    CPM_CLKGR,  1,  0,			CLK_GATE_HIWORD_MASK),
	GATE(CLK_GATE_NEMC,	    "gate_nemc",		"div_ahb2",	    CPM_CLKGR,  0,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_CPU,	    "gate_cpu",		    "div_cpu",	    CPM_CLKGR1, 15, 0,  		CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_APB0,	    "gate_apb0",		"div_ahb0",	    CPM_CLKGR1, 14, CLK_IGNORE_UNUSED,	CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_OST,	    "gate_ost",		    "ext",		    CPM_CLKGR1, 11, CLK_IGNORE_UNUSED, 	CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_AHB0,	    "gate_ahb0",		"div_ahb0",	    CPM_CLKGR1, 10, CLK_IGNORE_UNUSED,	CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_AHB1,	    "gate_ahb1",		"div_ahb2",	    CPM_CLKGR1, 6,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_AES,	    "gate_aes",		    "div_ahb2",	    CPM_CLKGR1, 5,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_GMAC,	    "gate_gmac",		"div_macphy",	CPM_CLKGR1, 4,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_IPU,      "gate_ipu",         "div_ahb0",     CPM_CLKGR1, 2,  0,          CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_DTRNG,	"gate_dtrng",		"div_apb",	    CPM_CLKGR1, 1,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_GATE_EL150,	"gate_el150",		"div_el150",    CPM_CLKGR1, 0,  0, 			CLK_GATE_SET_TO_DISABLE),
	GATE(CLK_CE_I2ST,	    "ce_i2st",		    "div_i2st",	    CPM_I2STCDR, 29, 0, 0),
	GATE(CLK_CE_I2SR,	    "ce_i2sr",		    "div_i2sr",	    CPM_I2SRCDR, 29, 0, 0),
	GATE(CLK_GATE_USBPHY,   "gate_usbphy",      "div_apb",      CPM_OPCR,   23, 0,          CLK_GATE_SET_TO_DISABLE)
};

static void clk_div_table_generate(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(t31_clk_div_table); i++) {
		t31_clk_div_table[i].val = i;
		t31_clk_div_table[i].div = (i + 1) * 4;
	}

}

static const struct of_device_id ext_clk_match[] __initconst = {
	        { .compatible = "ingenic,fixed-clock", .data = (void *)0, },
			{},
};

static int clocks_show(struct seq_file *m, void *v)
{
	int i = 0;
	struct clk_onecell_data * clk_data = NULL;
	struct clk *clk = NULL;

	if(m->private != NULL) {
		seq_printf(m, "CLKGR\t: %08x\n", cpm_inl(CPM_CLKGR));
		seq_printf(m, "CLKGR1\t: %08x\n", cpm_inl(CPM_CLKGR1));
		seq_printf(m, "LCR1\t: %08x\n", cpm_inl(CPM_LCR));
	} else {
		seq_printf(m, " ID  NAME              FRE          sta     count   parent\n");
		clk_data = &ctx->clk_data;
		for(i = 0; i < clk_data->clk_num; i++) {
			clk = clk_data->clks[i];
			if (clk != ERR_PTR(-ENOENT)) {
				if (__clk_get_name(clk) == NULL) {
					seq_printf(m, "--------------------------------------------------------\n");
				} else {
					unsigned int mhz = _get_rate(__clk_get_name(clk)) / 1000;
					seq_printf(m, "%3d %-15s %4d.%03dMHz %7sable   %d %10s\n", i, __clk_get_name(clk)
								, mhz/1000, mhz%1000
								, __clk_get_flags(clk) & CLK_FLG_ENABLE? "en": "dis"
								, __clk_get_enable_count(clk)
								, clk_get_parent(clk)? __clk_get_name(clk_get_parent(clk)): "root");
				}
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
static void __init t31_clk_init(void)
{
    printk("t31 Clock Power Management Unit init!\n");
    struct device_node *np;

    np = of_find_compatible_node(NULL, NULL, "ingenic,t31-clocks");
    if (!np) {
        pr_err("Failed to find compatible node for t31 clocks\n");
        return;
    }

    struct ingenic_clk_provider *ctx;

    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        printk("%s: failed to allocate memory for CGU\n", __func__);
        goto err_out;

    printk("t31_clk_init: ctx = %p\n", ctx);
    ctx->reg_base = of_iomap(np, 0);
    if (!ctx->reg_base) {
        printk("%s: failed to map CGU registers\n", __func__);
        goto err_out_free;
    }

    ctx->np = np;
    // ctx->clock_info = clock_info;
    //ctx->clocks.clk_num = num_clocks;

    printk("spin_lock_init\n");
    spin_lock_init(&ctx->lock);

	/* Register Ext Clocks From DT */
    printk("ingenic_clk_of_register_fixed_ext\n");
	ingenic_clk_of_register_fixed_ext(ctx, t31_fixed_rate_ext_clks,
			                        ARRAY_SIZE(t31_fixed_rate_ext_clks), ext_clk_match);

	/* Register PLLs. */
    printk("ingenic_clk_register_pll\n");
	ingenic_clk_register_pll(ctx, t31_pll_clks,
				ARRAY_SIZE(t31_pll_clks), ctx->reg_base);


	/* Register Muxs */
    printk("ingenic_clk_register_mux\n");
	ingenic_clk_register_mux(ctx, t31_mux_clks, ARRAY_SIZE(t31_mux_clks));

	/* Register Bus Divs */
    printk("ingenic_clk_register_bus_div\n");
	ingenic_clk_register_bus_div(ctx, t31_bus_div_clks, ARRAY_SIZE(t31_bus_div_clks));

	/* Register Divs */
    printk("ingenic_clk_register_cgu_div\n");
	clk_div_table_generate();
	ingenic_clk_register_cgu_div(ctx, t31_div_clks, ARRAY_SIZE(t31_div_clks));

	/* Register Fractional Divs */
    printk("ingenic_clk_register_fra_div\n");
	ingenic_clk_register_fra_div(ctx, t31_fdiv_clks, ARRAY_SIZE(t31_fdiv_clks));

	/* Register Gates */
    printk("ingenic_clk_register_gate\n");
	ingenic_clk_register_gate(ctx, t31_gate_clks, ARRAY_SIZE(t31_gate_clks));

	/* Register Powers */
//	ingenic_power_register_gate(ctx, t31_gate_power, ARRAY_SIZE(t31_gate_power));

	ingenic_clk_of_add_provider(np, ctx);


	//ingenic_clk_of_dump(ctx);


	pr_info("=========== t31 clocks: =============\n"
		"\tapll     = %lu , mpll     = %lu\n"
		"\tcpu_clk  = %lu , l2c_clk  = %lu\n"
		"\tahb0_clk = %lu , ahb2_clk = %lu\n"
		"\tapb_clk  = %lu , ext_clk  = %lu, ddr_clk  = %lu\n\n",
		_get_rate("apll"),	_get_rate("mpll"),
		_get_rate("div_cpu"), _get_rate("div_l2c"),
		_get_rate("div_ahb0"), _get_rate("div_ahb2"),
		_get_rate("div_apb"), _get_rate("ext"), _get_rate("div_ddr"));


    err_out_free:
    kfree(ctx);
    err_out:
}

arch_initcall(t31_clk_init);


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
