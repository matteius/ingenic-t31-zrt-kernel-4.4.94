#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/device.h>

#define ISP_PHYS_ADDR 0x13300000
#define ISP_SIZE      0x1000

#define CIM_BASE_ADDR 0x13060000
#define CIM_REGISTER_SIZE 0x10000

#define CIM_CIMCFG_OFFSET 0x0000
#define CIM_CIMCR_OFFSET  0x0004
#define CIM_CIMST_OFFSET  0x0008
#define CIM_CIMDA_OFFSET  0x0020

#define CIM_CIMCFG_VALUE 0x00000000
#define CIM_CIMST_VALUE  0x00150002
#define CIM_CIMDA_VALUE  0x00000000

struct cim_dev {
	void __iomem *base;
	struct clk *mclk;
};

volatile static void __iomem *isp_base;
volatile struct resource *res;
static struct platform_device *isp_mem_map_device;

volatile void __iomem *get_isp_base(void)
{
	return isp_base;
}
EXPORT_SYMBOL(get_isp_base);

volatile struct resource *get_isp_res(void)
{
	return res;
}
EXPORT_SYMBOL(get_isp_res);

static int isp_map_map_probe(struct platform_device *pdev)
{
	struct cim_dev *cim;
	u32 val;

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
	iowrite32(CIM_CIMCFG_VALUE, cim->base + CIM_CIMCFG_OFFSET); // Write CIMCFG
	iowrite32(CIM_CIMST_VALUE, cim->base + CIM_CIMST_OFFSET); // Write CIMST
	iowrite32(CIM_CIMDA_VALUE, cim->base + CIM_CIMDA_OFFSET); // Write CIMDA

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

	platform_set_drvdata(pdev, cim);

	return 0;
}

static int isp_map_map_remove(struct platform_device *pdev)
{
	struct cim_dev *cim = platform_get_drvdata(pdev);

	clk_disable_unprepare(cim->mclk);
	iounmap(cim->base);

	return 0;
}

static const struct of_device_id isp_map_map_of_match[] = {
		{ .compatible = "ingenic,isp-mem-map", },
		{},
};
MODULE_DEVICE_TABLE(of, isp_map_map_of_match);

static struct platform_driver isp_mem_map_driver = {
		.probe = isp_map_map_probe,
		.remove = isp_map_map_remove,
		.driver = {
				.name = "is_mem_map",
				.of_match_table = isp_map_map_of_match,
		},
};

static int __init isp_mem_map_init(void)
{
	int ret;

	isp_mem_map_device = platform_device_alloc("isp_mem_map", -1);
	if (!isp_mem_map_device)
		return -ENOMEM;

	ret = platform_device_add(isp_mem_map_device);
	if (ret) {
		platform_device_put(isp_mem_map_device);
		return ret;
	}

	ret = platform_driver_register(&isp_mem_map_driver);
	if (ret) {
		platform_device_unregister(isp_mem_map_device);
		return ret;
	}

	return 0;
}

static void __exit isp_mem_map_exit(void)
{
	platform_driver_unregister(&isp_mem_map_driver);
	platform_device_unregister(isp_mem_map_device);
}

module_init(isp_mem_map_init);
module_exit(isp_mem_map_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("map Driver for Mapping ISP Memory");