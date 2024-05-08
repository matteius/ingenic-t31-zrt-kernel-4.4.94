#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#define ISP_PHYS_ADDR 0x13300000
#define ISP_SIZE      0x1000

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

    res = request_mem_region(ISP_PHYS_ADDR, 1000, "mipi-phy");
    if (!res) {
        dev_err(&pdev->dev, "Failed to request ISP memory region\n");
        return -EBUSY;
    }

    isp_base = ioremap(ISP_PHYS_ADDR, ISP_SIZE);
    if (!isp_base) {
        dev_err(&pdev->dev, "Failed to remap ISP memory region\n");
        release_mem_region(ISP_PHYS_ADDR, ISP_SIZE);
        return -EFAULT;
    }

	// Read the value at offset 0xbc from isp_base
	unsigned int value = *(unsigned int *)((char *)isp_base + 0xbc);
	dev_info(&pdev->dev, "ISP memory value at offset 0xbc: 0x%08x\n", value);

	dev_info(&pdev->dev, "ISP memory region successfully remapped at %p\n", isp_base);
	return 0;
}

static const struct of_device_id isp_map_map_of_match[] = {
        { .compatible = "ingenic,isp-mem-map", },
        {},
};
MODULE_DEVICE_TABLE(of, isp_map_map_of_match);

static struct platform_driver isp_mem_map_driver = {
        .probe = isp_map_map_probe,
        .driver = {
                .name = "is_mem_map",
                .of_match_table = isp_map_map_of_match,
                .owner = THIS_MODULE,
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
