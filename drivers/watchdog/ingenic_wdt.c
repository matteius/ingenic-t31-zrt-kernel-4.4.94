/*
 *  Copyright (C) 2017, bo.liu <bo.liu@ingenic.com>
 *  INGENIC Watchdog driver
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/string.h> // For strlen
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mfd/core.h>
#include <linux/mfd/ingenic-tcu.h>
#include <soc/xburst/reboot.h>
#include <jz_proc.h>
#include <soc/base.h>

#define DEFAULT_HEARTBEAT 5
#define MAX_HEARTBEAT     2048


static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned int heartbeat = DEFAULT_HEARTBEAT;
module_param(heartbeat, uint, 0);
MODULE_PARM_DESC(heartbeat,
		"Watchdog heartbeat period in seconds from 1 to "
		__MODULE_STRING(MAX_HEARTBEAT) ", default "
		__MODULE_STRING(DEFAULT_HEARTBEAT));

struct ingenic_wdt_drvdata {
	struct watchdog_device wdt;
	struct platform_device *pdev;
	const struct mfd_cell *cell;

	struct clk *ext_clk;
	struct notifier_block restart_handler;
};

static int ingenic_wdt_ping(struct watchdog_device *wdt_dev)
{
	ingenic_watchdog_set_count(0);
	return 0;
}

static inline int get_clk_div(unsigned int *timeout_value)
{
	int clk_div = TCU_PRESCALE_1;

	while (*timeout_value > 0xffff) {
		if (clk_div == TCU_PRESCALE_1024) {
			/* Requested timeout too high;
			 * use highest possible value. */
			*timeout_value = 0xffff;
			clk_div = -1;
			break;
		}
		*timeout_value >>= 2;
		clk_div += 1;
	}

	return clk_div;
}
static void wdt_config(struct ingenic_wdt_drvdata *drvdata,
					   unsigned int new_timeout)
{
	unsigned int rtc_clk_rate;
	//unsigned int ext_clk_rate;
	unsigned int timeout_value;
	unsigned int clk_src;
	int clk_div = 0;

    rtc_clk_rate = 24000000 / 512;
	clk_src = TCU_CLKSRC_RTC;
	//ext_clk_rate = clk_get_rate(drvdata->ext_clk);
	//timeout_value = ext_clk_rate * new_timeout;
	timeout_value = rtc_clk_rate * new_timeout;
	clk_div = get_clk_div(&timeout_value);
    if(clk_div < 0){
		printk("Requested timeout too high, use highest possible value\n");
		clk_div = TCU_PRESCALE_1024;
		timeout_value = 0xffff;
	}

    ingenic_watchdog_config((clk_div << 3) | clk_src, timeout_value);
}

static int ingenic_wdt_set_timeout(struct watchdog_device *wdt_dev,
								   unsigned int new_timeout)
{
	struct ingenic_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	wdt_config(drvdata, new_timeout);
	wdt_dev->timeout = new_timeout;
	return 0;
}

static int ingenic_wdt_start(struct watchdog_device *wdt_dev)
{
	struct ingenic_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	drvdata->cell->enable(drvdata->pdev);
	ingenic_wdt_set_timeout(wdt_dev, wdt_dev->timeout);
	return 0;
}

static int ingenic_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct ingenic_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	drvdata->cell->disable(drvdata->pdev);
	return 0;
}

static const struct watchdog_info ingenic_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "ingenic Watchdog",
};

static const struct watchdog_ops ingenic_wdt_ops = {
	.owner = THIS_MODULE,
	.start = ingenic_wdt_start,
	.stop = ingenic_wdt_stop,
	.ping = ingenic_wdt_ping,
	.set_timeout = ingenic_wdt_set_timeout,
};

static int ingenic_reset_handler(struct notifier_block *this, unsigned long mode,
                                 void *cmd)
{
    struct ingenic_wdt_drvdata *drvdata = container_of(this,
    struct ingenic_wdt_drvdata, restart_handler);
    struct watchdog_device *wdd = &drvdata->wdt;

    if (cmd && !strcmp(cmd, REBOOT_CMD_RECOVERY))
        ingenic_recovery_sign();
    else
        ingenic_reboot_sign();

    if (cmd && !strcmp(cmd, REBOOT_CMD_SOFTBURN))
        ingenic_softburn_sign();

    drvdata->cell->enable(drvdata->pdev);

    ingenic_watchdog_config((3 << 3)|TCU_CLKSRC_RTC, 4);
    while (1) {
        mdelay(500);
        pr_err("wdt reset failed, Never be here\n");
    }
    return NOTIFY_DONE;
}
#ifdef CONFIG_OF
static const struct of_device_id ingenic_wdt_of_matches[] = {
	{ .compatible = "ingenic,watchdog", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ingenic_wdt_of_matches)
#endif

static ssize_t watchdog_cmd_set(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos);
static int watchdog_cmd_open(struct inode *inode, struct file *file);
static const struct file_operations watchdog_cmd_fops ={
	.read = seq_read,
	.open = watchdog_cmd_open,
	.llseek = seq_lseek,
	.release = single_release,
	.write = watchdog_cmd_set,
};


static struct watchdog_device *m_wdt;
static int ingenic_wdt_probe(struct platform_device *pdev)
{
	struct ingenic_wdt_drvdata *drvdata;
	struct watchdog_device *ingenic_wdt;
	int ret;
	struct proc_dir_entry *proc;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct ingenic_wdt_drvdata),
			       GFP_KERNEL);
	if (!drvdata) {
		dev_err(&pdev->dev, "Unable to alloacate watchdog device\n");
		return -ENOMEM;
	}

	drvdata->cell = mfd_get_cell(pdev);
	if (!drvdata->cell) {
		dev_err(&pdev->dev, "Failed to get mfd cell\n");
		return -ENOMEM;
	}
	drvdata->pdev = pdev;
	if (heartbeat < 1 || heartbeat > MAX_HEARTBEAT)
		heartbeat = DEFAULT_HEARTBEAT;

	m_wdt = ingenic_wdt = &drvdata->wdt;
	ingenic_wdt->info = &ingenic_wdt_info;
	ingenic_wdt->ops = &ingenic_wdt_ops;
	ingenic_wdt->timeout = heartbeat;
	ingenic_wdt->min_timeout = 1;
	ingenic_wdt->max_timeout = MAX_HEARTBEAT;
	ingenic_wdt->parent = &pdev->dev;
	watchdog_set_nowayout(ingenic_wdt, nowayout);
	watchdog_set_drvdata(ingenic_wdt, drvdata);

	drvdata->ext_clk = devm_clk_get(&pdev->dev, "ext");
	if (IS_ERR(drvdata->ext_clk)) {
		dev_err(&pdev->dev, "cannot find EXT clock\n");
		ret = PTR_ERR(drvdata->ext_clk);
		goto err_out;
	}

	ret = watchdog_register_device(&drvdata->wdt);
	if (ret < 0)
		goto err_disable_ext_clk;

	platform_set_drvdata(pdev, drvdata);

	drvdata->restart_handler.notifier_call = ingenic_reset_handler;
	drvdata->restart_handler.priority = WDT_RESET_PROR;
	ret = register_restart_handler(&drvdata->restart_handler);
	if (ret)
		dev_warn(&pdev->dev,
				 "cannot register restart handler (err=%d)\n", ret);

	/* proc info */
	proc = jz_proc_mkdir("watchdog");
	if (!proc) {
		printk("create mdio info failed!\n");
	}
	proc_create_data("reset", S_IRUGO, proc, &watchdog_cmd_fops, NULL);

	return 0;

err_disable_ext_clk:
	clk_put(drvdata->ext_clk);
err_out:
	return ret;
}

static int ingenic_wdt_remove(struct platform_device *pdev)
{
	struct ingenic_wdt_drvdata *drvdata = platform_get_drvdata(pdev);

	ingenic_wdt_stop(&drvdata->wdt);
	watchdog_unregister_device(&drvdata->wdt);
	clk_put(drvdata->ext_clk);
	devm_kfree(&pdev->dev, drvdata);
	return 0;
}

static struct platform_driver ingenic_wdt_driver = {
	.probe = ingenic_wdt_probe,
	.remove = ingenic_wdt_remove,
	.driver = {
		.name = "ingenic,watchdog",
		.of_match_table = of_match_ptr(ingenic_wdt_of_matches),
	},
};

module_platform_driver(ingenic_wdt_driver);

MODULE_AUTHOR("bo.liu <bo.liu@ingenic.com>");
MODULE_DESCRIPTION("ingenic Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ingenic-wdt");


/* cmd */
#define WATCHDOG_CMD_BUF_SIZE 100
static uint8_t watchdog_cmd_buf[100];

static int watchdog_cmd_show(struct seq_file *m, void *v)
{
    // Calculate the length of the string plus the newline character
    int len = strlen(watchdog_cmd_buf) + 1;

    // Now, print the string with seq_printf
    seq_printf(m, "%s\n", watchdog_cmd_buf);

    // Return the calculated length
    return len;
}


static ssize_t watchdog_cmd_set(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos)
{
    int cmd_value = 0;
    unsigned int reg04 = 0,reglc = 0;

	char *buf = kzalloc((count+1), GFP_KERNEL);
	if(!buf)
		return -ENOMEM;
	if(copy_from_user(buf, buffer, count))
	{
		kfree(buf);
		return EFAULT;
	}
	cmd_value = simple_strtoull(buf, NULL, 0);

	mdelay(1000);
	if(1 == cmd_value) {
		reg04 = inl(TCU_IOBASE + 0x04);
		if(reg04 & 0x1) {
			outl(0 << 0, TCU_IOBASE + 0x4);
		}

		outl(0x1a, TCU_IOBASE + 0xc);

		reglc = inl(TCU_IOBASE + 0x1c);
		if(reglc & 0x10000) {
			outl(1 << 16, TCU_IOBASE + 0x3c);
			outl(1 << 16, TCU_IOBASE + 0x2c);
		}
		outl(0x0000, TCU_IOBASE + 0x0);
		outl(0x0000, TCU_IOBASE + 0x8);

		reg04 = inl(TCU_IOBASE + 0x04);
		if(!(reg04 & 0x1)) {
			outl(1 << 0, TCU_IOBASE + 0x4);
		}
		printk("watchdog reboot system!!!\n");
	}

	kfree(buf);
	return count;
}
static int watchdog_cmd_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, watchdog_cmd_show, PDE_DATA(inode),8192);
}
