/*
 * Ingenic Soc Setup
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Ingenic Semiconductor Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/irqchip.h>
#include <linux/clocksource.h>

#include <asm/prom.h>
#include <soc/base.h>

void __init cpm_reset(void)
{
}

int __init setup_init(void)
{
#if 0
	cpm_reset();
	/* Set bus priority here */
	*(volatile unsigned int *)0xb34f0240 = 0x00010003;
	*(volatile unsigned int *)0xb34f0244 = 0x00010003;
#endif

	return 0;
}
extern void __init init_all_clk(void);
/* used by linux-mti code */
extern void *get_fdt_addr(void);
void __init plat_mem_setup(void)
{
	/* use IO_BASE, so that we can use phy addr on hard manual
	 * directly with in(bwlq)/out(bwlq) in io.h.
	 */
	set_io_port_base(IO_BASE);
	ioport_resource.start	= 0x00000000;
	ioport_resource.end	= 0xffffffff;
	iomem_resource.start	= 0x00000000;
	iomem_resource.end	= 0xffffffff;
	setup_init();

	__dt_setup_arch(get_fdt_addr());

	return;
}
void __init device_tree_init(void)
{
	unflatten_and_copy_device_tree();
}

static int __init plat_of_populate(void)
{
	of_platform_default_populate(NULL, NULL, NULL);
	return 0;
}
arch_initcall(plat_of_populate);

void __init plat_time_init(void)
{
	of_clk_init(NULL);

	printk("=== enter clocksource_probe  ===\n");
	timer_probe();
}

void __init arch_init_irq(void)
{
	irqchip_init();
}

unsigned long ispmem_base = 0;
EXPORT_SYMBOL(ispmem_base);

unsigned long ispmem_size = 0;
EXPORT_SYMBOL(ispmem_size);

static int __init ispmem_parse(char *str)
{
	char *retptr;

	ispmem_size = memparse(str, &retptr);
	if(ispmem_size < 0) {
		ispmem_size = 0;
	}

	if (*retptr == '@')
		ispmem_base = memparse(retptr + 1, NULL);

	if(ispmem_base < 0) {
		printk("## no ispmem! ##\n");
	}
	return 1;
}
__setup("ispmem=", ispmem_parse);
