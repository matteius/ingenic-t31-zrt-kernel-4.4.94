#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <asm/io.h>

#define CIM_BASE_ADDR 0x13060000
#define CIM_REGISTER_SIZE 4

#define CIM_CIMCFG_OFFSET 0x0000
#define CIM_CIMCR2_OFFSET  0x0008
#define CIM_CIMST_OFFSET  0x0008
#define CIM_CIMDA_OFFSET  0x0020

#define CIM_RESET_VALUE 0x00000000
#define CIM_CIMST_VALUE  0x00150002
#define CIM_CIMDA_VALUE  0x00000000

struct cim_dev {
	void __iomem *base;
	struct clk *mclk;
};

static struct cdev cim_cdev;
static dev_t cim_dev_num;
static struct class *cim_class;
static struct device *cim_char_device;
static struct platform_device *cim_device;

static int cim_open(struct inode *inode, struct file *file) {
	// Open the device
}

static int cim_release(struct inode *inode, struct file *file) {
	// Release the device
}

static long cim_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	// Perform I/O control operations
}

static const struct file_operations cim_fops = {
		.owner = THIS_MODULE,
		.open = cim_open,
		.release = cim_release,
		.unlocked_ioctl = cim_ioctl,
};

static int cim_probe(struct platform_device *pdev) {
	struct cim_dev *cim;
	struct resource *res;
	u32 val;
	int ret;

	printk("CIM probe~!!!!!!!!!!!!!!!!!!!\n");

	cim = devm_kzalloc(&pdev->dev, sizeof(*cim), GFP_KERNEL);
	if (!cim)
		return -ENOMEM;

	printk("CIM to ioremap~!!!!!!!!!!!!!!!!!!!\n");
	cim->base = ioremap(CIM_BASE_ADDR, CIM_REGISTER_SIZE);
	if (!cim->base)
		return -ENOMEM;

	cim->mclk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(cim->mclk)) {
		dev_err(&pdev->dev, "Failed to get CIM MCLK\n");
		return PTR_ERR(cim->mclk);
	}

	clk_prepare_enable(cim->mclk);

	// Initialize CIM registers
	iowrite32(CIM_RESET_VALUE, cim->base + CIM_CIMCR2_OFFSET); // Write CIMCFG
	iowrite32(CIM_CIMST_VALUE, cim->base + CIM_CIMCR2_OFFSET); // Write CIMST
	//iowrite32(CIM_CIMDA_VALUE, cim->base + CIM_CIMCR2_OFFSET); // Write CIMDA

	val = ioread32(cim->base + CIM_CIMCR_OFFSET); // Read CIMCR
	printk("Initial CIMCR value: 0x%08x\n", val);

	val |= (1 << 0); // Set ENA bit to enable CIM
	val |= (1 << 2); // Set DMA_EN bit to enable DMA
	val |= (1 << 1); // Set RF_RST bit to reset RXFIFO
	iowrite32(val, cim->base + CIM_CIMCR_OFFSET); // Write CIMCR

	val = ioread32(cim->base + CIM_CIMCR_OFFSET); // Read CIMCR
	printk("CIMCR value after enabling CIM and DMA: 0x%08x\n", val);

	val &= ~(1 << 1); // Clear RF_RST bit after reset
	iowrite32(val, cim->base + CIM_CIMCR_OFFSET); // Write CIMCR

	val = ioread32(cim->base + CIM_CIMCR_OFFSET); // Read CIMCR
	pr_info("Final CIMCR value: 0x%08x\n", val);

	// Allocate device number
	ret = alloc_chrdev_region(&cim_dev_num, 0, 1, "cim");
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to allocate device number\n");
		return ret;
	}

	// Initialize cdev structure
	cdev_init(&cim_cdev, &cim_fops);
	cim_cdev.owner = THIS_MODULE;

	// Add character device to the system
	ret = cdev_add(&cim_cdev, cim_dev_num, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add character device\n");
		goto err_cdev_add;
	}

	// Create device class
	cim_class = class_create(THIS_MODULE, "cim");
	if (IS_ERR(cim_class)) {
		dev_err(&pdev->dev, "Failed to create device class\n");
		ret = PTR_ERR(cim_class);
		goto err_class_create;
	}

	// Create device
	cim_char_device = device_create(cim_class, NULL, cim_dev_num, NULL, "cim");
	if (IS_ERR(cim_char_device)) {
		dev_err(&pdev->dev, "Failed to create device\n");
		ret = PTR_ERR(cim_char_device);
		goto err_device_create;
	}

	platform_set_drvdata(pdev, cim);

	return 0;

	err_device_create:
	class_destroy(cim_class);
	err_class_create:
	cdev_del(&cim_cdev);
	err_cdev_add:
	unregister_chrdev_region(cim_dev_num, 1);

	clk_disable_unprepare(cim->mclk);
	iounmap(cim->base);

	return ret;
}

static int cim_remove(struct platform_device *pdev) {
	struct cim_dev *cim = platform_get_drvdata(pdev);

	device_destroy(cim_class, cim_dev_num);
	class_destroy(cim_class);
	cdev_del(&cim_cdev);
	unregister_chrdev_region(cim_dev_num, 1);

	clk_disable_unprepare(cim->mclk);
	iounmap(cim->base);

	return 0;
}

static const struct of_device_id cim_of_match[] = {
		{ .compatible = "ingenic,x1000-cim", },
		{ }
};

static struct platform_driver cim_driver = {
		.probe = cim_probe,
		.remove = cim_remove,
		.driver = {
				.name = "ingenic-cim",
				.of_match_table = cim_of_match,
		},
};

static int __init cim_init(void)
{
	int ret;

	cim_device = platform_device_alloc("cim", -1);
	if (!cim_device)
		return -ENOMEM;

	ret = platform_device_add(cim_device);
	if (ret) {
		platform_device_put(cim_device);
		return ret;
	}

	ret = platform_driver_register(&cim_driver);
	if (ret) {
		platform_device_unregister(cim_device);
		return ret;
	}

	return 0;
}

static void __exit cim_exit(void)
{
	platform_driver_unregister(&cim_driver);
	platform_device_unregister(cim_device);
}

module_init(cim_init);
module_exit(cim_exit);


MODULE_DEVICE_TABLE(of, cim_of_match);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matt Davis");
MODULE_DESCRIPTION("Ingenic X1000 CIM driver");