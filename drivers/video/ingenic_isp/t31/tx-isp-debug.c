#include <linux/module.h>
#include <tx-isp-debug.h>
#include <linux/vmalloc.h>

#include <linux/proc_fs.h>

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/pwm.h>
#include <linux/file.h>
#include <linux/gpio.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kthread.h>
#include <linux/mfd/core.h>
#include <linux/mempolicy.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/i2c-gpio.h>
#include <linux/of_platform.h>

#include <linux/gpio.h>

#include <soc/gpio.h>
#include <soc/base.h>
#include <dt-bindings/interrupt-controller/t31-irq.h>

#include <linux/spi/spi.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <jz_proc.h>
#include <misc/isp_mem_map.h>

#include <txx-funcs.h>

/* -------------------debugfs interface------------------- */
static int print_level = ISP_WARNING_LEVEL;
module_param(print_level, int, S_IRUGO);
MODULE_PARM_DESC(print_level, "isp print level");

static int isp_clk = 100000000;
module_param(isp_clk, int, S_IRUGO);
MODULE_PARM_DESC(isp_clk, "isp clock freq");

extern int isp_ch0_pre_dequeue_time;
module_param(isp_ch0_pre_dequeue_time, int, S_IRUGO);
MODULE_PARM_DESC(isp_ch0_pre_dequeue_time, "isp pre dequeue time, unit ms");

extern int isp_ch0_pre_dequeue_interrupt_process;
module_param(isp_ch0_pre_dequeue_interrupt_process, int, S_IRUGO);
MODULE_PARM_DESC(isp_ch0_pre_dequeue_interrupt_process, "isp pre dequeue interrupt process");

extern int isp_ch0_pre_dequeue_valid_lines;
module_param(isp_ch0_pre_dequeue_valid_lines, int, S_IRUGO);
MODULE_PARM_DESC(isp_ch0_pre_dequeue_valid_lines, "isp pre dequeue valid lines");

extern int isp_ch1_dequeue_delay_time;
module_param(isp_ch1_dequeue_delay_time, int, S_IRUGO);
MODULE_PARM_DESC(isp_ch1_dequeue_delay_time, "isp pre dequeue time, unit ms");

extern int isp_day_night_switch_drop_frame_num;
module_param(isp_day_night_switch_drop_frame_num, int, S_IRUGO);
MODULE_PARM_DESC(isp_day_night_switch_drop_frame_num, "isp day night switch drop frame number");

extern int isp_memopt;
module_param(isp_memopt, int, S_IRUGO);
MODULE_PARM_DESC(isp_memopt, "isp memory optimize");

int isp_printf(unsigned int level, unsigned char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int r = 0;

	if(level >= print_level){
		va_start(args, fmt);

		vaf.fmt = fmt;
		vaf.va = &args;

		r = printk("%pV",&vaf);
		va_end(args);
		if(level >= ISP_ERROR_LEVEL)
			dump_stack();
	}
	return r;
}
EXPORT_SYMBOL(isp_printf);

int get_isp_clk(void)
{
	return isp_clk;
}

void private_dma_sync_single_for_device(struct device *dev,
										dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	printk("private_dma_sync_single_for_device: called with addr = %lx, size = %lu\n", addr, size);
	dma_sync_single_for_device(dev, addr, size, dir);
	return;
}
EXPORT_SYMBOL(private_dma_sync_single_for_device);


/* platform interfaces */
int private_platform_driver_register(struct platform_driver *drv)
{
	printk("private_platform_driver_register: called\n");
	return platform_driver_register(drv);
}
EXPORT_SYMBOL(private_platform_driver_register);

void private_platform_driver_unregister(struct platform_driver *drv)
{
	printk("private_platform_driver_unregister: called\n");
	platform_driver_unregister(drv);
}
EXPORT_SYMBOL(private_platform_driver_unregister);

void private_platform_set_drvdata(struct platform_device *pdev, void *data)
{
	printk("private_platform_set_drvdata: called with pdev = %p\n", pdev);
	platform_set_drvdata(pdev, data);
}
EXPORT_SYMBOL(private_platform_set_drvdata);

void *private_platform_get_drvdata(struct platform_device *pdev)
{
	return platform_get_drvdata(pdev);
}

int private_platform_device_register(struct platform_device *pdev)
{
	return platform_device_register(pdev);
}


void private_platform_device_unregister(struct platform_device *pdev)
{
	platform_device_unregister(pdev);
}

struct resource *private_platform_get_resource(struct platform_device *dev,
											   unsigned int type, unsigned int num)
{
	return platform_get_resource(dev, type, num);
}

void private_dev_set_drvdata(struct device *dev, void *data)
{
	dev_set_drvdata(dev, data);
}

void* private_dev_get_drvdata(const struct device *dev)
{
	return dev_get_drvdata(dev);
}


int private_platform_get_irq(struct platform_device *dev, unsigned int num)
{
	int irq;
	struct resource *res;
	int i;
	const char *compatible;
	struct device_node *node;
	struct platform_device *actual_dev;

	// Log the entry into the function and the parameters
	printk("private_platform_get_irq: called with num = %d for device %s\n", num, dev_name(&dev->dev));

	// Log device name and ID
	printk("Device name: %s, ID: %d, ID auto-generated: %s\n", dev->name, dev->id, dev->id_auto ? "yes" : "no");

	// Log device resources
	printk("Number of resources: %d\n", dev->num_resources);
	for (i = 0; i < dev->num_resources; i++) {
		res = &dev->resource[i];
		printk("Resource %d: start = %pa, end = %pa, flags = %lx\n", i, &res->start, &res->end, res->flags);
	}

	// Log device properties
	if (dev->dev.of_node) {
		printk("Device has OF node: %pOF\n", dev->dev.of_node);
		if (of_property_read_string(dev->dev.of_node, "compatible", &compatible) == 0) {
			printk("Compatible: %s\n", compatible);
		} else {
			printk("Failed to read compatible property\n");
		}
	}

	// Log device driver information
	if (dev->dev.driver) {
		printk("Device driver: %s\n", dev->dev.driver->name);
	}

	if (strcmp(dev_name(&dev->dev), "isp-m0") == 0) {
		printk("Device is isp-m0\n");
		node = of_find_node_by_name(NULL, "isp-m0");
	} else if (strcmp(dev_name(&dev->dev), "isp-w02") == 0) {
		printk("Device is isp-m1\n");
		node = of_find_node_by_name(NULL, "isp-w02");
	} else {
		node = NULL;
	}

	if (node) {
		actual_dev = of_find_device_by_node(node);
		if (actual_dev) {
			printk("Found actual device: %s\n", dev_name(&actual_dev->dev));
			dev = actual_dev;
		} else {
			printk("Failed to find actual device for node %pOF\n", node);
		}
		of_node_put(node);
	}


	dev->driver_override = "ingenic,t31-isp";
	// Log driver override
	if (dev->driver_override) {
		printk("Driver override: %s\n", dev->driver_override);
	}

	// Log MFD cell pointer
	if (dev->mfd_cell) {
		printk("Device has MFD cell: %p\n", dev->mfd_cell);
	}

	// Log device ID entry
	if (dev->id_entry) {
		printk("Device ID entry: name = %s\n", dev->id_entry->name);
	}

	// Call the actual platform_get_irq function
	irq = platform_get_irq(dev, num);

	// Check if the returned IRQ is valid
	if (irq < 0)
		printk("private_platform_get_irq: failed to get IRQ, returned %d\n", irq);
	else
		printk("private_platform_get_irq: got IRQ %d for device %s\n", irq, dev_name(&dev->dev));

	// Return the obtained IRQ number
	return irq;
}
EXPORT_SYMBOL(private_platform_get_irq);

struct resource * private_request_mem_region(resource_size_t start, resource_size_t n,
											 const char *name)
{
	printk("private_request_mem_region: called with start = %lx, n = %lx and name = %s\n", start, n, name);
	return request_mem_region(start, n, name);
}
EXPORT_SYMBOL(private_request_mem_region);

void private_release_mem_region(resource_size_t start, resource_size_t n)
{
	printk("private_release_mem_region: called with start = %lx, n = %lx\n", start, n);
	release_mem_region(start, n);
}
EXPORT_SYMBOL(private_release_mem_region);

void __iomem *private_ioremap(phys_addr_t offset, unsigned long size) {
	printk("private_ioremap: called with offset: %lx, size: %lu\n", offset, size);
	volatile void __iomem *addr = ioremap(offset, size);

	mdelay(1000);

	// Print non-zero memory values from addr to addr + size
	unsigned int *ptr = (unsigned int *)addr;
	unsigned int num_values = size / sizeof(unsigned int);
	unsigned int i;
	int printed_values = 0;

	printk("Non-zero memory values from %p to %p:\n", addr, addr + size);
	for (i = 0; i < num_values; i++) {
		unsigned int value = ioread32((void __iomem *)ptr);
		if (value != 0) {
			if (printed_values % 4 == 0) {
				printk("%p: ", ptr);
			}
			printk("0x%08x ", *ptr);
			if ((printed_values + 1) % 4 == 0 || i == num_values - 1) {
				printk("\n");
			}
			printed_values++;
		}
		ptr += sizeof(unsigned int);
	}

	if (printed_values == 0) {
	printk("No non-zero memory values found.\n");
	}

	return addr;
}
EXPORT_SYMBOL(private_ioremap);

void private_iounmap(const volatile void __iomem *addr)
{
printk("private_iounmap: called with addr: %p\n", addr);
iounmap(addr);
}
EXPORT_SYMBOL(private_iounmap);

/* interrupt interfaces */
int private_request_threaded_irq(unsigned int irq, irq_handler_t handler,
								 irq_handler_t thread_fn, unsigned long irqflags,
								 const char *devname, void *dev_id)
{
	printk("private_request_threaded_irq: called with irq = %d, devname = %s\n", irq, devname);
	return request_threaded_irq(irq, handler, thread_fn, irqflags, devname, dev_id);
}
EXPORT_SYMBOL(private_request_threaded_irq);

void private_enable_irq(unsigned int irq)
{
	printk("private_enable_irq: called with irq = %d\n", irq);
	enable_irq(irq);
}
EXPORT_SYMBOL(private_enable_irq);

void private_disable_irq(unsigned int irq)
{
	printk("private_disable_irq: called with irq = %d\n", irq);
	disable_irq(irq);
}
EXPORT_SYMBOL(private_disable_irq);

void private_free_irq(unsigned int irq, void *dev_id)
{
	printk("private_free_irq: called with irq = %d\n", irq);
	free_irq(irq, dev_id);
}
EXPORT_SYMBOL(private_free_irq);

/* lock and mutex interfaces */
void __private_spin_lock_irqsave(spinlock_t *lock, unsigned long *flags)
{
	printk("private_spin_lock_irqsave: called with lock = %p\n", lock);
	raw_spin_lock_irqsave(spinlock_check(lock), *flags);
}
EXPORT_SYMBOL(__private_spin_lock_irqsave);

void private_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
	printk("private_spin_unlock_irqrestore: called with lock = %p\n", lock);
	spin_unlock_irqrestore(lock, flags);
}
EXPORT_SYMBOL(private_spin_unlock_irqrestore);

void private_spin_lock_init(spinlock_t *lock)
{
	printk("private_spin_lock_init: called with lock = %p\n", lock);
	spin_lock_init(lock);
}
EXPORT_SYMBOL(private_spin_lock_init);

void private_mutex_lock(struct mutex *lock)
{
	printk("private_mutex_lock: called with lock = %p\n", lock);
	mutex_lock(lock);
}
EXPORT_SYMBOL(private_mutex_lock);

void private_mutex_unlock(struct mutex *lock)
{
	printk("private_mutex_unlock: called with lock = %p\n", lock);
	mutex_unlock(lock);
}
EXPORT_SYMBOL(private_mutex_unlock);

void private_raw_mutex_init(struct mutex *lock, const char *name, struct lock_class_key *key)
{
	printk("private_raw_mutex_init: called with lock = %p\n", lock);
	__mutex_init(lock, name, key);
}
EXPORT_SYMBOL(private_raw_mutex_init);

/* clock interfaces */
struct clk * private_clk_get(struct device *dev, const char *id)
{
	printk("private_clk_get: called with id = %s\n", id);
	return clk_get(dev, id);
}
EXPORT_SYMBOL(private_clk_get);

struct clk * private_devm_clk_get(struct device *dev, const char *id)
{
	printk("private_devm_clk_get: called with id = %s\n", id);
	return devm_clk_get(dev, id);
}
EXPORT_SYMBOL(private_devm_clk_get);

int private_clk_enable(struct clk *clk)
{
	printk("private_clk_enable: called with clk = %p\n", clk);
	return clk_enable(clk);
}
EXPORT_SYMBOL(private_clk_enable);

int private_clk_prepare_enable(struct clk *clk)
{
	printk("private_clk_prepare_enable: called with clk = %p\n", clk);
	return clk_prepare_enable(clk);
}
EXPORT_SYMBOL(private_clk_prepare_enable);

int private_clk_is_enabled(struct clk *clk)
{
	printk("private_clk_is_enabled: called with clk = %p\n", clk);
	return __clk_is_enabled(clk);
}
EXPORT_SYMBOL(private_clk_is_enabled);

void private_clk_disable(struct clk *clk)
{
	printk("private_clk_disable: called with clk = %p\n", clk);
	clk_disable(clk);
}
EXPORT_SYMBOL(private_clk_disable);

void private_clk_disable_unprepare(struct clk *clk)
{
	printk("private_clk_disable_unprepare: called with clk = %p\n", clk);
	clk_disable_unprepare(clk);
}
EXPORT_SYMBOL(private_clk_disable_unprepare);

unsigned long private_clk_get_rate(struct clk *clk)
{
	printk("private_clk_get_rate: called with clk = %p\n", clk);
	return clk_get_rate(clk);
}
EXPORT_SYMBOL(private_clk_get_rate);

void private_clk_put(struct clk *clk)
{
	printk("private_clk_put: called with clk = %p\n", clk);
	return clk_put(clk);
}
EXPORT_SYMBOL(private_clk_put);

void private_devm_clk_put(struct device *dev, struct clk *clk)
{
	printk("private_devm_clk_put: called with clk = %p\n", clk);
	return devm_clk_put(dev, clk);
}
EXPORT_SYMBOL(private_devm_clk_put);

int private_clk_set_rate(struct clk *clk, unsigned long rate)
{
	printk("private_clk_set_rate: called with clk = %p, rate = %lu\n", clk, rate);
	return clk_set_rate(clk, rate);
}
EXPORT_SYMBOL(private_clk_set_rate);

/* i2c interfaces */
struct i2c_adapter* private_i2c_get_adapter(int nr)
{
	printk("private_i2c_get_adapter: called with nr = %d\n", nr);
	return i2c_get_adapter(nr);
}

void private_i2c_put_adapter(struct i2c_adapter *adap)
{
	printk("private_i2c_put_adapter: called with adap = %p\n", adap);
	i2c_put_adapter(adap);
}

int private_i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	printk("private_i2c_transfer: called with adap = %p, num = %d\n", adap, num);
	return i2c_transfer(adap, msgs, num);
}
EXPORT_SYMBOL(private_i2c_transfer);

int private_i2c_register_driver(struct module *owner, struct i2c_driver *driver)
{
	printk("private_i2c_register_driver: called with owner = %p\n", owner);
	return i2c_register_driver(owner, driver);
}

void private_i2c_del_driver(struct i2c_driver *drv)
{
	printk("private_i2c_del_driver: called with drv = %p\n", drv);
	i2c_del_driver(drv);
}
EXPORT_SYMBOL(private_i2c_del_driver);

struct i2c_client *private_i2c_new_device(struct i2c_adapter *adap, struct i2c_board_info const *info)
{
	printk("private_i2c_new_device: called with adap = %p\n", adap);
	return i2c_new_device(adap, info);
}
EXPORT_SYMBOL(private_i2c_new_device);

void private_i2c_set_clientdata(struct i2c_client *dev, void *data)
{
	printk("private_i2c_set_clientdata: called with dev = %p\n", dev);
	i2c_set_clientdata(dev, data);
}
EXPORT_SYMBOL(private_i2c_set_clientdata);

struct sock *private_netlink_kernel_create(struct net *net, int unit, struct netlink_kernel_cfg *cfg)
{
	printk("private_netlink_kernel_create: called with unit = %d\n", unit);
	return netlink_kernel_create(net, unit, cfg);
}
EXPORT_SYMBOL(private_netlink_kernel_create);

void *private_i2c_get_clientdata(const struct i2c_client *dev)
{
	printk("private_i2c_get_clientdata: called with dev = %p\n", dev);
	return i2c_get_clientdata(dev);
}
EXPORT_SYMBOL(private_i2c_get_clientdata);

int private_i2c_add_driver(struct i2c_driver *drv)
{
	printk("private_i2c_add_driver: called with drv = %p\n", drv);
	return i2c_add_driver(drv);
}
EXPORT_SYMBOL(private_i2c_add_driver);

void private_i2c_unregister_device(struct i2c_client *client)
{
	printk("private_i2c_unregister_device: called with client = %p\n", client);
	i2c_unregister_device(client);
}
EXPORT_SYMBOL(private_i2c_unregister_device);

/* gpio interfaces */
int private_gpio_request(unsigned gpio, const char *label)
{
	printk("private_gpio_request: called with gpio = %d, label = %s\n", gpio, label);
	return gpio_request(gpio, label);
}
EXPORT_SYMBOL(private_gpio_request);

void private_gpio_free(unsigned gpio)
{
	printk("private_gpio_free: called with gpio = %d\n", gpio);
	gpio_free(gpio);
}
EXPORT_SYMBOL(private_gpio_free);

int private_gpio_direction_output(unsigned gpio, int value)
{
	printk("private_gpio_direction_output: called with gpio = %d, value = %d\n", gpio, value);
	return gpio_direction_output(gpio, value);
}
EXPORT_SYMBOL(private_gpio_direction_output);

int private_gpio_direction_input(unsigned gpio)
{
	printk("private_gpio_direction_input: called with gpio = %d\n", gpio);
	return gpio_direction_input(gpio);
}
EXPORT_SYMBOL(private_gpio_direction_input);

int private_gpio_set_debounce(unsigned gpio, unsigned debounce)
{
	printk("private_gpio_set_debounce: called with gpio = %d, debounce = %d\n", gpio, debounce);
	return gpio_set_debounce(gpio, debounce);
}
EXPORT_SYMBOL(private_gpio_set_debounce);

int private_jzgpio_set_func(enum gpio_port port, enum gpio_function func, unsigned long pins) {
	printk("private_jzgpio_set_func: called with port = %d, func = %d, pins = %lu\n", port, func, pins);
	// Call the real private_jzgpio_set_func function
	return jzgpio_set_func(port, func, pins);
}
EXPORT_SYMBOL(private_jzgpio_set_func);

#if 0
int private_jzgpio_ctrl_pull(enum gpio_port port, int enable_pull,unsigned long pins)
{
	printk("private_jzgpio_ctrl_pull: called with port = %d, enable_pull = %d, pins = %lu\n", port, enable_pull, pins);
	return jzgpio_ctrl_pull(port, enable_pull,pins);
}
#endif

/* system interfaces */
void private_msleep(unsigned int msecs)
{
	printk("private_msleep: called with msecs = %d\n", msecs);
	msleep(msecs);
}
EXPORT_SYMBOL(private_msleep);

bool private_capable(int cap)
{
	printk("private_capable: called with cap = %d\n", cap);
	return capable(cap);
}
EXPORT_SYMBOL(private_capable);

unsigned long long private_sched_clock(void)
{
	printk("private_sched_clock: called\n");
	return sched_clock();
}
EXPORT_SYMBOL(private_sched_clock);


bool private_try_module_get(struct module *module)
{
	printk("private_try_module_get: called\n");
	return try_module_get(module);
}
EXPORT_SYMBOL(private_try_module_get);

int private_request_module(bool wait, const char *fmt, ...)
{
	int ret = 0;
	struct va_format vaf;
	printk("private_request_module: called\n");
	va_list args;
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	ret =  __request_module(true,"%pV", &vaf);
	va_end(args);
	printk("private_request_module: return %d\n", ret);
	return ret;
}
EXPORT_SYMBOL(private_request_module);

void private_module_put(struct module *module)
{
	printk("private_module_put: called\n");
	module_put(module);
}
EXPORT_SYMBOL(private_module_put);


/* wait interfaces */
void private_init_completion(struct completion *x)
{
	printk("private_init_completion: called with x = %p\n", x);
	init_completion(x);
}
EXPORT_SYMBOL(private_init_completion);

void private_complete(struct completion *x)
{
	printk("private_complete: called with x = %p\n", x);
	complete(x);
}
EXPORT_SYMBOL(private_complete);

int private_wait_for_completion_interruptible(struct completion *x)
{
	printk("private_wait_for_completion_interruptible: called with x = %p\n", x);
	return wait_for_completion_interruptible(x);
}
EXPORT_SYMBOL(private_wait_for_completion_interruptible);

unsigned long private_wait_for_completion_timeout(struct completion *x, unsigned long timeover)
{
	printk("private_wait_for_completion_timeout: called with x = %p, timeover = %lu\n", x, timeover);
	return wait_for_completion_timeout(x, timeover);
}
EXPORT_SYMBOL(private_wait_for_completion_timeout);

int private_wait_event_interruptible(wait_queue_head_t *q, int (*state)(void *), void *data)
{
	printk("private_wait_event_interruptible: called with q = %p\n", q);
	return wait_event_interruptible((*q), state(data));
}
EXPORT_SYMBOL(private_wait_event_interruptible);

void private_wake_up_all(wait_queue_head_t *q)
{
	printk("private_wake_up_all: called with q = %p\n", q);
	wake_up_all(q);
}
EXPORT_SYMBOL(private_wake_up_all);

void private_wake_up(wait_queue_head_t *q)
{
	printk("private_wake_up: called with q = %p\n", q);
	wake_up(q);
}

void private_init_waitqueue_head(wait_queue_head_t *q)
{
	printk("private_init_waitqueue_head: called with q = %p\n", q);
	init_waitqueue_head(q);
}
EXPORT_SYMBOL(private_init_waitqueue_head);


/* misc driver interfaces */
int private_misc_register(struct miscdevice *mdev)
{
	printk("private_misc_register: called\n");
	return misc_register(mdev);
}
EXPORT_SYMBOL(private_misc_register);

void private_misc_deregister(struct miscdevice *mdev)
{
	printk("private_misc_deregister: called\n");
	misc_deregister(mdev);
}
EXPORT_SYMBOL(private_misc_deregister);


struct proc_dir_entry *private_proc_create_data(const char *name, umode_t mode,
												struct proc_dir_entry *parent,
												const struct file_operations *proc_fops,
												void *data)
{
	printk("private_proc_create_data: called with name = %s\n", name);
	return proc_create_data(name, mode, parent, proc_fops, data);
//	return NULL;
}
EXPORT_SYMBOL(private_proc_create_data);

//malloc
void *private_vmalloc(unsigned long size)
{
	printk("private_vmalloc: called with size = %lu\n", size);
	void *addr = vmalloc(size);
	return addr;
}
EXPORT_SYMBOL(private_vmalloc);

void private_vfree(const void *addr)
{
	printk("private_vfree: called with addr = %p\n", addr);
	vfree(addr);
}
EXPORT_SYMBOL(private_vfree);

void * private_kmalloc(size_t s, gfp_t gfp)
{
	printk("private_kmalloc: called with size = %lu\n", s);
	void *addr = kmalloc(s, gfp);
	return addr;
}
EXPORT_SYMBOL(private_kmalloc);

void private_kfree(void *p){
	printk("private_kfree: called with addr = %p\n", p);
	kfree(p);
}
EXPORT_SYMBOL(private_kfree);

//copy user
long private_copy_from_user(void *to, const void __user *from, long size)
{
printk("private_copy_from_user: called with size = %lu\n", size);
return copy_from_user(to, from,size);
}
EXPORT_SYMBOL(private_copy_from_user);

long private_copy_to_user(void __user *to, const void *from, long size)
{
printk("private_copy_to_user: called with size = %lu\n", size);
return copy_to_user(to, from, size);
}
EXPORT_SYMBOL(private_copy_to_user);

/* file ops */
struct file *private_filp_open(const char *filename, int flags, umode_t mode)
{
	printk("private_filp_open: called with filename = %s\n", filename);
	return filp_open(filename, flags, mode);
}
EXPORT_SYMBOL(private_filp_open);

int private_filp_close(struct file *filp, fl_owner_t id)
{
	printk("private_filp_close: called\n");
	return filp_close(filp, id);
}
EXPORT_SYMBOL(private_filp_close);

ssize_t private_vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
printk("private_vfs_read: called\n");
return vfs_read(file, buf, count, pos);
}
EXPORT_SYMBOL(private_vfs_read);

ssize_t private_vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
printk("private_vfs_write: called\n");
return vfs_write(file, buf, count, pos);
}

loff_t private_vfs_llseek(struct file *file, loff_t offset, int whence)
{
	printk("private_vfs_llseek: called\n");
	return vfs_llseek(file, offset, whence);
}
EXPORT_SYMBOL(private_vfs_llseek);

mm_segment_t private_get_fs(void)
{
	printk("private_get_fs: called\n");
	return get_fs();
}
EXPORT_SYMBOL(private_get_fs);

void private_set_fs(mm_segment_t val)
{
	printk("private_set_fs: called\n");
	set_fs(val);
}
EXPORT_SYMBOL(private_set_fs);

void private_dma_cache_sync(struct device *dev, void *vaddr, size_t size,
							enum dma_data_direction direction)
{
	printk("private_dma_cache_sync: called\n");
	dma_cache_sync(dev, vaddr, size, direction);
}
EXPORT_SYMBOL(private_dma_cache_sync);

void private_getrawmonotonic(struct timespec *ts)
{
	printk("private_getrawmonotonic: called\n");
	getrawmonotonic(ts);
}
EXPORT_SYMBOL(private_getrawmonotonic);

/* kthread interfaces */

bool private_kthread_should_stop(void)
{
	printk("private_kthread_should_stop: called\n");
	return kthread_should_stop();
}
EXPORT_SYMBOL(private_kthread_should_stop);

struct task_struct* private_kthread_run(int (*threadfn)(void *data), void *data, const char namefmt[])
{
	printk("private_kthread_run: called\n");
	return kthread_run(threadfn, data, namefmt);
}
EXPORT_SYMBOL(private_kthread_run);

int private_kthread_stop(struct task_struct *k)
{
	printk("private_kthread_stop: called\n");
	return kthread_stop(k);
}
EXPORT_SYMBOL(private_kthread_stop);

/* proc file interfaces */
ssize_t private_seq_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
printk("private_seq_read: called\n");
return seq_read(file, buf, size, ppos);
}
EXPORT_SYMBOL(private_seq_read);

loff_t private_seq_lseek(struct file *file, loff_t offset, int whence)
{
	printk("private_seq_lseek: called\n");
	return seq_lseek(file, offset, whence);
}
EXPORT_SYMBOL(private_seq_lseek);

int private_single_release(struct inode *inode, struct file *file)
{
	printk("private_single_release: called\n");
	return single_release(inode, file);
}
EXPORT_SYMBOL(private_single_release);

int private_single_open_size(struct file *file, int (*show)(struct seq_file *, void *), void *data, size_t size)
{
	printk("private_single_open_size: called\n");
	return single_open_size(file, show, data, size);
}
EXPORT_SYMBOL(private_single_open_size);

struct proc_dir_entry* private_jz_proc_mkdir(char *s)
{
	printk("private_jz_proc_mkdir: called\n");
	return jz_proc_mkdir(s);
}
EXPORT_SYMBOL(private_jz_proc_mkdir);

void private_proc_remove(struct proc_dir_entry *de)
{
	printk("private_proc_remove: called\n");
	proc_remove(de);
}
EXPORT_SYMBOL(private_proc_remove);

void private_seq_printf(struct seq_file *m, const char *fmt, ...)
{
	printk("private_seq_printf: called\n");
	struct va_format vaf;
	va_list args;
	int r = 0;
	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	seq_printf(m, "%pV", &vaf);
	r = m->count;
	va_end(args);
}
EXPORT_SYMBOL(private_seq_printf);

unsigned long long private_simple_strtoull(const char *cp, char **endp, unsigned int base)
{
	printk("private_simple_strtoull: called\n");
	return simple_strtoull(cp, endp,  base);
}
EXPORT_SYMBOL(private_simple_strtoull);

extern unsigned long ispmem_base;
extern unsigned long ispmem_size;

static void get_isp_priv_mem(unsigned int *phyaddr, unsigned int *size)
{
	*phyaddr = ispmem_base;
	*size = ispmem_size;
}

void private_get_isp_priv_mem(unsigned int *phyaddr, unsigned int *size)
{
	get_isp_priv_mem(phyaddr, size);
}




/* struct net *private_get_init_net(void) */
/* { */
/* 	return get_init_net(); */
/* } */

ktime_t private_ktime_set(const long secs, const unsigned long nsecs)
{
	printk("private_ktime_set: called\n");
	return ktime_set(secs, nsecs);
}
EXPORT_SYMBOL(private_ktime_set);

void private_set_current_state(unsigned int state)
{
	printk("private_set_current_state: called\n");
	__set_current_state(state);
	return;
}
EXPORT_SYMBOL(private_set_current_state);


int private_schedule_hrtimeout(ktime_t *ex, const enum hrtimer_mode mode)
{
	printk("private_schedule_hrtimeout: called\n");
	return schedule_hrtimeout(ex, mode);
}
EXPORT_SYMBOL(private_schedule_hrtimeout);

bool private_schedule_work(struct work_struct *work)
{
	printk("private_schedule_work: called\n");
	return schedule_work(work);
}
EXPORT_SYMBOL(private_schedule_work);


void private_do_gettimeofday(struct timeval *tv)
{
	printk("private_do_gettimeofday: called\n");
	do_gettimeofday(tv);
	return;
}
EXPORT_SYMBOL(private_do_gettimeofday);
