/*
 *  ingenic On-Chip MAC Driver
 *
 *  Copyright (C) 2010 - 2011  Ingenic Semiconductor Inc.
 *
 *  Licensed under the GPL-2 or later.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/crc32.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <jz_proc.h>
#include "ingenic_mac.h"
#include <dt-bindings/net/ingenic_gmac.h>

DEFINE_SEMAPHORE(mutex_mdio);
int debug_enable = 0;
#define INGENIC_MAC_DRV_NAME		"dwc-mac"
#define INGENIC_MAC_DRV_VERSION		"1.0"
#define INGENIC_MAC_DRV_DESC		"Ingenic on-chip Ethernet MAC driver"
module_param(debug_enable, int, 0644);
MODULE_AUTHOR("Lutts Wolf <slcao@ingenic.cn>");
MODULE_VERSION(INGENIC_MAC_DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(INGENIC_MAC_DRV_DESC);
MODULE_ALIAS("platform:"INGENIC_MAC_DRV_NAME);
#define COPYBREAK_DEFAULT 256
static unsigned int copybreak __read_mostly = COPYBREAK_DEFAULT;
module_param(copybreak, uint, 0644);
MODULE_PARM_DESC(copybreak,
		"Maximum size of packet that is copied to a new buffer on receive");

#define INGENIC_MAC_RX_BUFFER_WRITE	16	/* Must be power of 2 */

static synopGMACdevice *g_gmacdev;
/* Generate the bit field mask from msb to lsb */
#define BITS_H2L(msb, lsb)  ((0xFFFFFFFF >> (32-((msb)-(lsb)+1))) << (lsb))

static const struct of_device_id ingenic_mac_dt_match[];

static int ingenic_mac_phy_hwrst(struct platform_device *pdev, bool init)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ingenic_mac_local *lp = netdev_priv(ndev);
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	enum of_gpio_flags flags;
	int ret = 0;
	struct pinctrl *p = pinctrl_get(dev);
	struct pinctrl_state *state = NULL;

	if (IS_ERR_OR_NULL(p)) {
		dev_warn(&pdev->dev, "can not get pinctrl\n");
		return 0;
	}

	if (init)
		lp->reset_gpio = -ENODEV;

	if (!(lp->reset_gpio < 0))
		goto hw_reset;

	lp->reset_gpio = of_get_named_gpio_flags(np, "ingenic,rst-gpio", 0, &flags);
	if (lp->reset_gpio < 0)
		goto out;
	ret = devm_gpio_request(dev, lp->reset_gpio, "mac-hw-rst");
	if (ret) {
		lp->reset_gpio = ret;
		dev_err(dev, "ingenic mac-hw-rst gpio request failed errno(%d)\n", ret);
		goto out;
	}
	lp->reset_lvl = flags & OF_GPIO_ACTIVE_LOW ? 0 : 1;

#define DEFAULT_RESET_MS 10
	ret = of_property_read_u32(np, "ingenic,rst-ms", &lp->reset_ms);
	if (ret < 0)
		lp->reset_ms = DEFAULT_RESET_MS;
#undef DEFAULT_RESET_MS

hw_reset:
	state = pinctrl_lookup_state(p, "reset");
	if (!IS_ERR_OR_NULL(state))
		pinctrl_select_state(p, state);

	gpio_direction_output(lp->reset_gpio, lp->reset_lvl);
	if (in_atomic())
		mdelay(lp->reset_ms);
	else
		msleep(lp->reset_ms);
	gpio_direction_output(lp->reset_gpio, !lp->reset_lvl);

	state = pinctrl_lookup_state(p, PINCTRL_STATE_DEFAULT);
	if (!IS_ERR_OR_NULL(state))
		pinctrl_select_state(p, state);

out:
	pinctrl_put(p);
	return ret;
}

static inline unsigned char str2hexnum(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return 0; /* foo */
}

static inline void str2eaddr(unsigned char *ea, unsigned char *str)
{
	int i;

	for (i = 0; i < 6; i++) {
		unsigned char num;

		if ((*str == '.') || (*str == ':'))
			str++;
		num  = str2hexnum(*str++) << 4;
		num |= str2hexnum(*str++);
		ea[i] = num;
	}
}

static int bootargs_ethaddr = 0;
static unsigned char ethaddr_hex[6];

static int __init ethernet_addr_setup(char *str)
{
	if (!str) {
		printk("ethaddr not set in command line\n");
		return -1;
	}
	bootargs_ethaddr = 1;
	str2eaddr(ethaddr_hex, str);

	return 0;
}

__setup("ethaddr=", ethernet_addr_setup);

/* debug routines */
__attribute__((__unused__)) static void ingenic_mac_dump_pkt_data(unsigned char *data, int len) {
	int i = 0;
	printk("\t0x0000: ");
	for (i = 0; i < len; i++) {
		printk("%02x", data[i]);

		if (i % 2)
			printk(" ");

		if ( (i != 0) && ((i % 16) == 15) )
			printk("\n\t0x%04x: ", i+1);
	}
	printk("\n");
}

__attribute__((__unused__)) static void ingenic_mac_dump_skb_data(struct sk_buff *skb) {
	printk("\n\n===================================\n");
	printk("head = 0x%08x, data = 0x%08x, tail = 0x%08x, end = 0x%08x\n",
			(unsigned int)(skb->head), (unsigned int)(skb->data),
			(unsigned int)(skb->tail), (unsigned int)(skb->end));
	printk("len = %d\n", skb->len);
	ingenic_mac_dump_pkt_data(skb->data, skb->len);
	printk("\n=====================================\n");
}

struct ingenic_mac_reg
{
	u32    addr;
	char   * name;
};

static struct ingenic_mac_reg mac[] =
{
	{ 0x0000, "                  Config" },
	{ 0x0004, "            Frame Filter" },
	{ 0x0008, "             MAC HT High" },
	{ 0x000C, "              MAC HT Low" },
	{ 0x0010, "               GMII Addr" },
	{ 0x0014, "               GMII Data" },
	{ 0x0018, "            Flow Control" },
	{ 0x001C, "                VLAN Tag" },
	{ 0x0020, "            GMAC Version" },
	{ 0x0024, "            GMAC Debug  " },
	{ 0x0028, "Remote Wake-Up Frame Filter" },
	{ 0x002C, "  PMT Control and Status" },
	{ 0x0030, "  LPI Control and status" },
	{ 0x0034, "      LPI Timers Control" },
	{ 0x0038, "        Interrupt Status" },
	{ 0x003c, "        Interrupt Mask" },
	{ 0x0040, "          MAC Addr0 High" },
	{ 0x0044, "           MAC Addr0 Low" },
	{ 0x0048, "          MAC Addr1 High" },
	{ 0x004c, "           MAC Addr1 Low" },
	{ 0x0100, "           MMC Ctrl Reg " },
	{ 0x010c, "        MMC Intr Msk(rx)" },
	{ 0x0110, "        MMC Intr Msk(tx)" },
	{ 0x0200, "    MMC Intr Msk(rx ipc)" },
	{ 0x0738, "          AVMAC Ctrl Reg" },
	{ 0x00D8, "           RGMII C/S Reg" },
	{ 0, 0 }
};
static struct ingenic_mac_reg dma0[] =
{
	{ 0x0000, "[CH0] CSR0   Bus Mode" },
	{ 0x0004, "[CH0] CSR1   TxPlDmnd" },
	{ 0x0008, "[CH0] CSR2   RxPlDmnd" },
	{ 0x000C, "[CH0] CSR3    Rx Base" },
	{ 0x0010, "[CH0] CSR4    Tx Base" },
	{ 0x0014, "[CH0] CSR5     Status" },
	{ 0x0018, "[CH0] CSR6    Control" },
	{ 0x001C, "[CH0] CSR7 Int Enable" },
	{ 0x0020, "[CH0] CSR8 Missed Fr." },
	{ 0x0024, "[CH0] Recv Intr Wd.Tm." },
	{ 0x0028, "[CH0] AXI Bus Mode   " },
	{ 0x002c, "[CH0] AHB or AXI Status" },
	{ 0x0048, "[CH0] CSR18 Tx Desc  " },
	{ 0x004C, "[CH0] CSR19 Rx Desc  " },
	{ 0x0050, "[CH0] CSR20 Tx Buffer" },
	{ 0x0054, "[CH0] CSR21 Rx Buffer" },
	{ 0x0058, "CSR22 HWCFG          " },
	{ 0, 0 }
};

__attribute__((__unused__)) static void ingenic_mac_dump_dma_regs(synopGMACdevice *gmacdev, const char *func, int line)
{
	struct ingenic_mac_reg *reg = dma0;

	printk("======================DMA Regs start===================\n");
	printk("func = %s line = %d\n", func, line);
	while(reg->name) {
		printk("===>%s:\t0x%08x\n", reg->name, synopGMACReadReg((u32 *)gmacdev->DmaBase,reg->addr));
		reg++;
	}
	printk("======================DMA Regs end===================\n");
}

__attribute__((__unused__)) static void ingenic_mac_dump_mac_regs(synopGMACdevice *gmacdev, const char *func, int line)
{
	struct ingenic_mac_reg *reg = mac;

	printk("======================MAC Regs start===================\n");
	printk("func = %s line = %d\n", func, line);
	while(reg->name) {
		printk("===>%s:\t0x%08x\n", reg->name, synopGMACReadReg((u32 *)gmacdev->MacBase,reg->addr));
		reg++;
	}
	printk("======================MAC Regs end===================\n");
}

__attribute__((__unused__)) static void ingenic_mac_dump_all_regs(synopGMACdevice *gmacdev, const char *func, int line) {
	ingenic_mac_dump_dma_regs(gmacdev, func, line);
	ingenic_mac_dump_mac_regs(gmacdev, func, line);
}

__attribute__((__unused__)) static void ingenic_mac_dump_dma_buffer_info(struct ingenic_mac_buffer *buffer_info) {
	printk("\tbuffer_info(%p):\n", buffer_info);
	printk("\t\tskb = %p\n", buffer_info->skb);
	printk("\t\tdma = 0x%08x\n", buffer_info->dma);
	printk("\t\tlen = %u\n", buffer_info->length);
	printk("\t\ttrans = %d\n", buffer_info->transfering);
	printk("\t\tinvalid = %d\n", buffer_info->invalid);
}

__attribute__((__unused__)) static void ingenic_mac_dump_dma_desc(DmaDesc *desc) {
	printk("\tdma desc(%p):\n", desc);
	printk("\t\tstatus = 0x%08x\n", desc->status);
	printk("\t\tbuffer1 = 0x%08x\n", desc->buffer1);
	printk("\t\tlength = %u\n", desc->length);
}

__attribute__((__unused__)) static void ingenic_mac_dump_dma_desc2(DmaDesc *desc, struct ingenic_mac_buffer *buffer_info) {
	printk("desc: %p, status: 0x%08x buf1: 0x%08x dma: 0x%08x len: %u bi: %p skb: %p trans: %d inv: %d\n",
			desc, desc->status, desc->buffer1, buffer_info->dma, desc->length,
			buffer_info, buffer_info->skb, buffer_info->transfering, buffer_info->invalid);
}

__attribute__((__unused__)) static void ingenic_mac_dump_rx_desc(struct ingenic_mac_local *lp) {
	int i = 0;
	printk("\n===================rx====================\n");
	printk("count = %d, next_to_use = %d next_to_clean = %d\n",
			lp->rx_ring.count, lp->rx_ring.next_to_use, lp->rx_ring.next_to_clean);
	for (i = 0; i < INGENIC_MAC_RX_DESC_COUNT; i++) {
		DmaDesc *desc = lp->rx_ring.desc + i;
		struct ingenic_mac_buffer *b = lp->rx_ring.buffer_info + i;

#if 0
		printk("desc %d:\n", i);
		ingenic_mac_dump_dma_desc(desc);
		ingenic_mac_dump_dma_buffer_info(b);
#endif
		ingenic_mac_dump_dma_desc2(desc, b);
	}
	printk("\n=========================================\n");
}

__attribute__((__unused__)) static void ingenic_mac_dump_tx_desc(struct ingenic_mac_local *lp) {
	int i = 0;
	printk("\n===================tx====================\n");
	printk("count = %d, next_to_use = %d next_to_clean = %d\n",
			lp->tx_ring.count, lp->tx_ring.next_to_use, lp->tx_ring.next_to_clean);
	for (i = 0; i < INGENIC_MAC_TX_DESC_COUNT; i++) {
		DmaDesc *desc = lp->tx_ring.desc + i;
		struct ingenic_mac_buffer *b = lp->tx_ring.buffer_info + i;

#if 0
		printk("desc %d:\n", i);
		ingenic_mac_dump_dma_desc(desc);
		ingenic_mac_dump_dma_buffer_info(b);
#endif
		ingenic_mac_dump_dma_desc2(desc, b);
	}
	printk("\n=========================================\n");
}

__attribute__((__unused__)) static void ingenic_mac_dump_all_desc(struct ingenic_mac_local *lp) {
	ingenic_mac_dump_rx_desc(lp);
	ingenic_mac_dump_tx_desc(lp);
}

__attribute__((__unused__)) static int get_rx_index_by_desc(struct ingenic_mac_local *lp, DmaDesc *desc) {
	int i = 0;

	for (i = 0; i < INGENIC_MAC_RX_DESC_COUNT; i++) {
		if ( (lp->rx_ring.desc + i) == desc)
			return i;
	}

	BUG_ON(i == INGENIC_MAC_RX_DESC_COUNT);
	return -1;
}

__attribute__((__unused__)) static void ingenic_mac_phy_dump(struct ingenic_mac_local *lp) {
	u16 phy[] = {0, 1, 4, 5, 6, 9, 16, 17, 18, 20, 21, 24, 0x1c};

	u16 data[sizeof(phy) / sizeof(u16)];
	int i;

	printk("\n-------->PHY dump: %08X\n", lp->phydev->phy_id);
	for (i = 0; i < sizeof(phy) / sizeof(u16); i++)
		data[i] = lp->mii_bus->read(lp->mii_bus, lp->phydev->addr, phy[i]);

	for (i = 0; i < sizeof(phy) / sizeof(u16); i++)
		printk("PHY reg%d, value %04X\n", phy[i], data[i]);
}


static void ingenic_mac_restart_rx_dma(struct ingenic_mac_local *lp) {
	synopGMAC_enable_dma_rx(lp->gmacdev);
}

static void ingenic_mac_alloc_rx_buffers(struct ingenic_mac_local *lp, int cleaned_count,
		int restart_dma) {
	int i = 0;
	struct ingenic_mac_buffer *buffer_info;
	struct sk_buff *skb;
	struct ingenic_mac_rx_ring *rx_ring = &lp->rx_ring;
	DmaDesc *rx_desc;
	DmaDesc *first_desc;
	int first;

	first = rx_ring->next_to_use;
	i = rx_ring->next_to_use;
	rx_desc = INGENIC_MAC_RX_DESC(*rx_ring, i);
	first_desc = rx_desc;
	buffer_info = &rx_ring->buffer_info[i];

	while (cleaned_count--) {
		skb = buffer_info->skb;
		if (skb) {
			skb_trim(skb, 0);
			goto map_skb;
		}

		skb = netdev_alloc_skb_ip_align(lp->netdev, lp->netdev->mtu + ETHERNET_HEADER + ETHERNET_CRC);
		if (unlikely(!skb)) {
			/* Better luck next round */
			lp->alloc_rx_buff_failed++;
			break;
		}

		buffer_info->skb = skb;
		buffer_info->length = skb_tailroom(skb);
map_skb:
		buffer_info->dma = dma_map_single(&lp->netdev->dev,
				skb->data, skb_tailroom(skb),
				DMA_FROM_DEVICE);
		if (dma_mapping_error(&lp->netdev->dev, buffer_info->dma)) {
			dev_err(&lp->netdev->dev, "Rx DMA map failed\n");
			lp->alloc_rx_buff_failed++;
			break;
		}

		rx_desc->length |= ((skb_tailroom(skb) <<DescSize1Shift) & DescSize1Mask) |
			((0 << DescSize2Shift) & DescSize2Mask);
		rx_desc->buffer1 = cpu_to_le32(buffer_info->dma);

		rx_desc->extstatus = 0;
		rx_desc->reserved1 = 0;
		rx_desc->timestamplow = 0;
		rx_desc->timestamphigh = 0;


		/* clr invalid first, then start transfer */
		buffer_info->invalid = 0;

		/* start transfer */
		rx_desc->status = DescOwnByDma;

		/* next */
		if (unlikely(++i == rx_ring->count))
			i = 0;

		wmb();

		rx_desc = INGENIC_MAC_RX_DESC(*rx_ring, i);
		buffer_info = &rx_ring->buffer_info[i];
	}

	if (likely(rx_ring->next_to_use != i)) {
		rx_ring->next_to_use = i;
		/* sanity check: ensure next_to_use is not used */
		rx_desc->buffer1 = cpu_to_le32(0);
		rx_desc->status &= ~DescOwnByDma;
		buffer_info->invalid = 1;
		wmb();

		/* assure that if there's any buffer space, dma is enabled */
		if (likely(restart_dma))
			ingenic_mac_restart_rx_dma(lp);
	}
}

static int desc_list_init_rx(struct ingenic_mac_local *lp) {
	synopGMACdevice *gmacdev = lp->gmacdev;
	int i;
	int size;

	/* rx init */
	lp->rx_ring.count = INGENIC_MAC_RX_DESC_COUNT;

	size = lp->rx_ring.count * sizeof(struct ingenic_mac_buffer);
	lp->rx_ring.buffer_info = vmalloc(size);
	if (!lp->rx_ring.buffer_info) {
		printk(KERN_ERR "Unable to allocate memory for the receive descriptor ring\n");
		return -ENOMEM;
	}
	memset(lp->rx_ring.buffer_info, 0, size);

	lp->rx_ring.desc = dma_alloc_noncoherent(&lp->netdev->dev,
			lp->rx_ring.count * sizeof(DmaDesc),
			&lp->rx_ring.dma, GFP_KERNEL);

	if (lp->rx_ring.desc == NULL) {
		vfree(lp->rx_ring.buffer_info);
		lp->rx_ring.buffer_info = NULL;
		return -ENOMEM;
	}

	dma_cache_wback_inv((unsigned long)lp->rx_ring.desc,
			lp->rx_ring.count * sizeof(DmaDesc));

	/* we always use uncached address for descriptors */
	lp->rx_ring.desc = (DmaDesc *)CKSEG1ADDR(lp->rx_ring.desc);

	for (i = 0; i < INGENIC_MAC_RX_DESC_COUNT; i++) {
		DmaDesc *desc = lp->rx_ring.desc + i;

		synopGMAC_rx_desc_init_ring(desc, i == (INGENIC_MAC_RX_DESC_COUNT - 1));
	}

	lp->rx_ring.next_to_use = lp->rx_ring.next_to_clean = 0;
	ingenic_mac_alloc_rx_buffers(lp, INGENIC_MAC_DESC_UNUSED(&lp->rx_ring), 0);

	synopGMACWriteReg((u32 *)gmacdev->DmaBase,DmaRxBaseAddr, lp->rx_ring.dma);

	return 0;
}

static void desc_list_free_rx(struct ingenic_mac_local *lp) {
	struct ingenic_mac_buffer *b;
	int i = 0;

	if (lp->rx_ring.desc)
		dma_free_noncoherent(&lp->netdev->dev,
				lp->rx_ring.count * sizeof(DmaDesc),
				(void *)CKSEG0ADDR(lp->rx_ring.desc),
				lp->rx_ring.dma);

	if (lp->rx_ring.buffer_info) {
		b = lp->rx_ring.buffer_info;

		for(i = 0; i < INGENIC_MAC_RX_DESC_COUNT; i++) {
			if (b[i].skb) {
				if (b[i].dma) {
					dma_unmap_single(&lp->netdev->dev, b[i].dma,
							b[i].length, DMA_FROM_DEVICE);
					b[i].dma = 0;
				}

				dev_kfree_skb_any(b[i].skb);
				b[i].skb = NULL;
				b[i].time_stamp = 0;
			}
		}
	}
	vfree(lp->rx_ring.buffer_info);
	lp->rx_ring.buffer_info = NULL;
	lp->rx_ring.next_to_use = lp->rx_ring.next_to_clean = 0;
}

/* must be called from interrupt handler */
static void ingenic_mac_take_desc_ownership_rx(struct ingenic_mac_local *lp) {
	synopGMACdevice *gmacdev = lp->gmacdev;
	int i = 0;

	/* must called with interrupts disabled */
	BUG_ON(synopGMAC_get_interrupt_mask(gmacdev));

	for (i = 0; i < INGENIC_MAC_RX_DESC_COUNT; i++) {
		DmaDesc *desc = lp->rx_ring.desc + i;
		struct ingenic_mac_buffer *b = lp->rx_ring.buffer_info + i;

		if (!b->invalid) {
			synopGMAC_take_desc_ownership(desc);
		}
	}

	//synopGMACWriteReg((u32 *)gmacdev->DmaBase,DmaRxBaseAddr, lp->rx_ring.dma);//new add for t40
}

/* MUST ensure that rx is stopped ans rx_dma is disabled */
static void desc_list_reinit_rx(struct ingenic_mac_local *lp) {
	DmaDesc *desc;
	int i = 0;

	//TODO: BUG_ON(!ingenic_mac_rx_dma_stopped());

	for (i = 0; i < INGENIC_MAC_RX_DESC_COUNT; i++) {
		desc = lp->rx_ring.desc + i;

		/* owned by DMA, can fill data */
		synopGMAC_rx_desc_init_ring(desc, i == (INGENIC_MAC_RX_DESC_COUNT - 1));
	}

	lp->rx_ring.next_to_use = lp->rx_ring.next_to_clean = 0;
	ingenic_mac_alloc_rx_buffers(lp, INGENIC_MAC_DESC_UNUSED(&lp->rx_ring), 0);

	synopGMACWriteReg((u32 *)lp->gmacdev->DmaBase,DmaRxBaseAddr, lp->rx_ring.dma);//new add for t40
}

static int desc_list_init_tx(struct ingenic_mac_local *lp) {
	synopGMACdevice *gmacdev = lp->gmacdev;
	int i;
	int size;

	/* tx init */
	lp->tx_ring.count = INGENIC_MAC_TX_DESC_COUNT;

	size = lp->tx_ring.count * sizeof(struct ingenic_mac_buffer);
	lp->tx_ring.buffer_info = vmalloc(size);
	if (!lp->tx_ring.buffer_info) {
		printk(KERN_ERR"Unable to allocate memory for the receive descriptor ring\n");
		return -ENOMEM;
	}
	memset(lp->tx_ring.buffer_info, 0, size);

	lp->tx_ring.desc = dma_alloc_noncoherent(&lp->netdev->dev,
			lp->tx_ring.count * sizeof(DmaDesc),
			&lp->tx_ring.dma, GFP_KERNEL);

	if (lp->tx_ring.desc == NULL) {
		vfree(lp->tx_ring.buffer_info);
		lp->tx_ring.buffer_info = NULL;
		return -ENOMEM;
	}

	dma_cache_wback_inv((unsigned long)lp->tx_ring.desc,
			lp->tx_ring.count * sizeof(DmaDesc));

	/* we always use uncached address for descriptors */
	lp->tx_ring.desc = (DmaDesc *)CKSEG1ADDR(lp->tx_ring.desc);

	for (i = 0; i < INGENIC_MAC_TX_DESC_COUNT; i++) {
		DmaDesc *desc = lp->tx_ring.desc + i;

		synopGMAC_tx_desc_init_ring(desc, i == (INGENIC_MAC_TX_DESC_COUNT - 1));
	}

	lp->tx_ring.next_to_use = lp->tx_ring.next_to_clean = 0;
	synopGMACWriteReg((u32 *)gmacdev->DmaBase,DmaTxBaseAddr,(u32)lp->tx_ring.dma);
	return 0;
}

__attribute__((__unused__)) static int get_tx_index_by_desc(struct ingenic_mac_local *lp, DmaDesc *desc) {
	int i = 0;

	for (i = 0; i < INGENIC_MAC_TX_DESC_COUNT; i++) {
		if ( (lp->tx_ring.desc + i) == desc)
			return i;
	}

	BUG_ON(i == INGENIC_MAC_TX_DESC_COUNT);
	return -1;
}

static void ingenic_mac_unmap_and_free_tx_resource(struct ingenic_mac_local *lp,
		struct ingenic_mac_buffer *buffer_info)
{
	buffer_info->transfering = 0;

	if (buffer_info->skb) {
		if (buffer_info->dma) {
			if (buffer_info->mapped_as_page)
				dma_unmap_page(&lp->netdev->dev, buffer_info->dma,
						buffer_info->length, DMA_TO_DEVICE);
			else
				dma_unmap_single(&lp->netdev->dev, buffer_info->dma,
						buffer_info->length, DMA_TO_DEVICE);
			buffer_info->dma = 0;
		}
		dev_kfree_skb_any(buffer_info->skb);
		buffer_info->skb = NULL;
	}
	buffer_info->time_stamp = 0;
}

static void desc_list_free_tx(struct ingenic_mac_local *lp) {
	struct ingenic_mac_buffer *b;
	int i = 0;

	if (lp->tx_ring.desc)
		dma_free_noncoherent(&lp->netdev->dev,
				lp->tx_ring.count * sizeof(DmaDesc),
				(void *)CKSEG0ADDR(lp->tx_ring.desc),
				lp->tx_ring.dma);

	if (lp->tx_ring.buffer_info) {
		b = lp->tx_ring.buffer_info;

		for (i = 0; i < INGENIC_MAC_TX_DESC_COUNT; i++) {
			// panic("===>ahha, testing! please do not goes here(%s:%d)!!!\n", __func__, __LINE__);
			ingenic_mac_unmap_and_free_tx_resource(lp, b + i);
		}

	}
	vfree(lp->tx_ring.buffer_info);
	lp->tx_ring.buffer_info = NULL;
	lp->tx_ring.next_to_use = lp->tx_ring.next_to_clean = 0;
}

/* must called in interrupt handler */
static void ingenic_mac_take_desc_ownership_tx(struct ingenic_mac_local *lp) {
	synopGMACdevice *gmacdev = lp->gmacdev;
	int i = 0;

	/* must called with interrupts disabled */
	BUG_ON(synopGMAC_get_interrupt_mask(gmacdev));

	for (i = 0; i < INGENIC_MAC_TX_DESC_COUNT; i++) {
		DmaDesc *desc = lp->tx_ring.desc + i;
		struct ingenic_mac_buffer *b = lp->tx_ring.buffer_info + i;

		if (!b->invalid) {
			synopGMAC_take_desc_ownership(desc);
		}
	}
}

/* must assure that tx transfer are stopped and tx_dma is disabled */
static void desc_list_reinit_tx(struct ingenic_mac_local *lp) {
	int i = 0;
	DmaDesc *desc;
	struct ingenic_mac_buffer *b;

	//TODO: BUG_ON(!ingenic_mac_tx_dma_stopped());

	for (i = 0; i < INGENIC_MAC_TX_DESC_COUNT; i++) {
		desc = lp->tx_ring.desc + i;
		b = lp->tx_ring.buffer_info + i;

		/* owned by CPU, no valid data */
		synopGMAC_tx_desc_init_ring(desc, i == (INGENIC_MAC_TX_DESC_COUNT - 1));

		ingenic_mac_unmap_and_free_tx_resource(lp, b);
	}

	lp->tx_ring.next_to_use = lp->tx_ring.next_to_clean = 0;
}

static void desc_list_free(struct ingenic_mac_local *lp)
{
	desc_list_free_rx(lp);
	desc_list_free_tx(lp);
}

static void desc_list_reinit(struct ingenic_mac_local *lp) {
	desc_list_reinit_rx(lp);
	desc_list_reinit_tx(lp);
	//ingenic_mac_dump_all_desc(lp);
}

static int desc_list_init(struct ingenic_mac_local *lp)
{
	if (desc_list_init_rx(lp) < 0)
		goto init_error;

	if (desc_list_init_tx(lp) < 0)
		goto init_error;

	//ingenic_mac_dump_all_desc(lp);
	return 0;

init_error:
	desc_list_free(lp);
	printk(KERN_ERR INGENIC_MAC_DRV_NAME ": kmalloc failed\n");
	return -ENOMEM;
}


/*---PHY CONTROL AND CONFIGURATION-----------------------------------------*/
static void ingenic_mac_adjust_link(struct net_device *dev)
{
	struct ingenic_mac_local *lp = netdev_priv(dev);
	struct phy_device *phydev = lp->phydev;
	synopGMACdevice *gmacdev = lp->gmacdev;
	unsigned long flags;
	int new_state = 0;

	//printk("===>ajust link, old_duplex = %d, old_speed = %d, old_link = %d\n",
	//       lp->old_duplex, lp->old_speed, lp->old_link);

	spin_lock_irqsave(&lp->link_lock, flags);
	if (phydev->link) {
		/* Now we make sure that we can be in full duplex mode.
		 * If not, we operate in half-duplex mode. */
		if (phydev->duplex != lp->old_duplex) {
			new_state = 1;

			if (phydev->duplex) {
				synopGMAC_set_full_duplex(gmacdev);
				//synopGMAC_rx_own_enable(gmacdev);
				//synopGMAC_set_Inter_Frame_Gap(gmacdev, GmacInterFrameGap7);
			} else {
				synopGMAC_set_half_duplex(gmacdev);
				//synopGMAC_rx_own_disable(gmacdev);
				//synopGMAC_set_Inter_Frame_Gap(gmacdev, GmacInterFrameGap4);
			}

			lp->old_duplex = phydev->duplex;
		}

		if (phydev->speed != lp->old_speed) {
			switch (phydev->speed) {
				case 1000:
					synopGMAC_select_speed1000(gmacdev);
					break;
				case 100:
					synopGMAC_select_speed100(gmacdev);
					break;
				case 10:
					synopGMAC_select_speed10(gmacdev);
					break;
				default:
					printk(KERN_ERR "GMAC PHY speed NOT match!\n");
					synopGMAC_select_speed100(gmacdev);
			}

			new_state = 1;
			lp->old_speed = phydev->speed;
		}

		if (!lp->old_link) {
			new_state = 1;
			lp->old_link = 1;
			netif_carrier_on(dev);
		}
	} else if (lp->old_link) {
		new_state = 1;
		lp->old_link = 0;
		lp->old_speed = 0;
		lp->old_duplex = -1;
		netif_carrier_off(dev);
	}

	if (new_state)
		phy_print_status(phydev);

	//printk("===>ajust link, new_duplex = %d, new_speed = %d, new_link = %d\n",
	//       lp->old_duplex, lp->old_speed, lp->old_link);

	spin_unlock_irqrestore(&lp->link_lock, flags);

}

static int mii_probe(struct net_device *dev)
{
	struct ingenic_mac_local *lp = netdev_priv(dev);
	struct phy_device *phydev = NULL;
	int phy_interface;
	int i;

	/* search for connect PHY device */
	for (i = 0; i < PHY_MAX_ADDR; i++) {
		struct phy_device *const tmp_phydev = lp->mii_bus->phy_map[i];

		if (!tmp_phydev)
			continue; /* no PHY here... */

		phydev = tmp_phydev;
		break; /* found it */
	}

	/* now we are supposed to have a proper phydev, to attach to... */
	if (!phydev) {
		printk(KERN_INFO "%s: Don't found any phy device at all\n",
				dev->name);
		return -ENODEV;
	}

	if(lp->interface == RMII)
		phy_interface = PHY_INTERFACE_MODE_RMII;
	else if(lp->interface == RGMII)
		phy_interface = PHY_INTERFACE_MODE_RGMII;
	else if(lp->interface == GMII)
		phy_interface = PHY_INTERFACE_MODE_GMII;
	else
		phy_interface = PHY_INTERFACE_MODE_MII;

	phydev = phy_connect(dev, dev_name(&phydev->dev), &ingenic_mac_adjust_link,
			phy_interface);
	if (IS_ERR(phydev)) {
		printk(KERN_ERR "%s: Could not attach to PHY\n", dev->name);
		return PTR_ERR(phydev);
	}

	/* mask with MAC supported features */
	phydev->supported &= (SUPPORTED_10baseT_Half
			| SUPPORTED_10baseT_Full
			| SUPPORTED_100baseT_Half
			| SUPPORTED_100baseT_Full
			| SUPPORTED_1000baseT_Half
			| SUPPORTED_1000baseT_Full
			| SUPPORTED_Autoneg
			| SUPPORTED_Pause | SUPPORTED_Asym_Pause
			| SUPPORTED_MII
			| SUPPORTED_TP);

	phydev->advertising = phydev->supported;

	lp->old_link = 0;
	lp->old_speed = 0;
	lp->old_duplex = -1;
	lp->phydev = phydev;
#ifdef DUMP_DEBUG
	ingenic_mac_phy_dump(lp);
#endif

	return 0;
}

/**
 * ingenic_mac_update_stats - Update the board statistics counters
 * @lp: board private structure
 **/

void ingenic_mac_update_stats(struct ingenic_mac_local *lp)
{
	if ((lp->old_link == 0) || (lp->old_speed == 0) || (lp->old_duplex == -1))
		return;

#if 0
	//spin_lock_irqsave(&lp->stats_lock, flags);
	//spin_lock(&lp->stats_lock);

	/* Fill out the OS statistics structure */
	lp->net_stats.multicast = REG32(MAC_STAT_RMCA);
	lp->net_stats.collisions = REG32(MAC_STAT_RBCA);

	/* Rx Errors */

	/* RLEC on some newer hardware can be incorrect so build
	 * our own version based on RUC and ROC */
	lp->net_stats.rx_errors = lp->stats.rxerrc +
		lp->stats.crcerrs + lp->stats.algnerrc +
		lp->stats.ruc + lp->stats.roc +
		lp->stats.cexterr;
	lp->net_stats.rx_length_errors = REG32(MAC_STAT_RFLR);
	lp->net_stats.rx_crc_errors = REG32(MAC_STAT_RFCS);
	lp->net_stats.rx_frame_errors = REG32(MAC_STAT_RALN);
	lp->net_stats.rx_missed_errors = lp->stats.mpc;

	/* Tx Errors */
	lp->stats.txerrc = lp->stats.ecol + lp->stats.latecol;
	lp->net_stats.tx_errors = lp->stats.txerrc;
	lp->net_stats.tx_aborted_errors = lp->stats.ecol;
	lp->net_stats.tx_window_errors = lp->stats.latecol;
	lp->net_stats.tx_carrier_errors = lp->stats.tncrs;
	if (hw->bad_tx_carr_stats_fd &&
		lp->link_duplex == FULL_DUPLEX) {
		lp->net_stats.tx_carrier_errors = 0;
		lp->stats.tncrs = 0;
	}

	/* Tx Dropped needs to be maintained elsewhere */

	/* Phy Stats */
	if (hw->media_type == e1000_media_type_copper) {
		if ((lp->link_speed == SPEED_1000) &&
			(!e1000_read_phy_reg(hw, PHY_1000T_STATUS, &phy_tmp))) {
			phy_tmp &= PHY_IDLE_ERROR_COUNT_MASK;
			lp->phy_stats.idle_errors += phy_tmp;
		}

		if ((hw->mac_type <= e1000_82546) &&
			(hw->phy_type == e1000_phy_m88) &&
			!e1000_read_phy_reg(hw, M88E1000_RX_ERR_CNTR, &phy_tmp))
			lp->phy_stats.receive_errors += phy_tmp;
	}

	/* Management Stats */
	if (hw->has_smbus) {
		lp->stats.mgptc += er32(MGTPTC);
		lp->stats.mgprc += er32(MGTPRC);
		lp->stats.mgpdc += er32(MGTPDC);
	}

	//spin_unlock_irqrestore(&lp->stats_lock, flags);
	//spin_unlock(&lp->stats_lock);
#endif
}

/**
 * ingenic_mac_watchdog - Timer Call-back
 * @data: pointer to lp cast into an unsigned long
 **/
static void ingenic_mac_watchdog(unsigned long data) {
	struct ingenic_mac_local *lp = (struct ingenic_mac_local *)data;

	ingenic_mac_update_stats(lp);

	mod_timer(&lp->watchdog_timer, round_jiffies(jiffies + 5 * HZ));
}

static int __ingenic_mac_maybe_stop_tx(struct net_device *netdev, int size)
{
	struct ingenic_mac_local *lp = netdev_priv(netdev);
	struct ingenic_mac_tx_ring *tx_ring = &lp->tx_ring;

	netif_stop_queue(netdev);
	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available. */
	if (likely(INGENIC_MAC_DESC_UNUSED(tx_ring) < size))
		return -EBUSY;

	/* A reprieve! */
	netif_start_queue(netdev);
	++lp->restart_queue;
	return 0;
}

static int ingenic_mac_maybe_stop_tx(struct net_device *netdev,
		struct ingenic_mac_tx_ring *tx_ring, int size)
{
	if (likely(INGENIC_MAC_DESC_UNUSED(tx_ring) >= size))
		return 0;
	return __ingenic_mac_maybe_stop_tx(netdev, size);
}

static int ingenic_mac_tx_map(struct ingenic_mac_local *lp,
		struct ingenic_mac_tx_ring *tx_ring,
		struct sk_buff *skb)
{
	struct net_device *pdev = lp->netdev;
	struct ingenic_mac_buffer *buffer_info;
	unsigned int len = skb_headlen(skb);
	unsigned int offset = 0, count = 0, i;
	unsigned int f, segs;
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;

	i = tx_ring->next_to_use;

	buffer_info = &tx_ring->buffer_info[i];
	buffer_info->length = len;
	buffer_info->time_stamp = jiffies;
	buffer_info->dma = dma_map_single(&pdev->dev,
			skb->data + offset,
			len, DMA_TO_DEVICE);
	buffer_info->mapped_as_page = false;
	if (dma_mapping_error(&pdev->dev, buffer_info->dma))
		goto dma_error;
	segs = skb_shinfo(skb)->gso_segs ? : 1;
	tx_ring->buffer_info[i].skb = skb;
	tx_ring->buffer_info[i].segs = segs;
	//tx_ring->buffer_info[i].transfering = 1;//x2000 not have
	count++;

	for (f = 0; f < nr_frags; f++) {
		struct skb_frag_struct *frag;

//		struct page *p;
		frag = &skb_shinfo(skb)->frags[f];
		len = frag->size;
		offset = frag->page_offset;
		i++;
		if (i == tx_ring->count)
			i = 0;
		buffer_info = &tx_ring->buffer_info[i];
		buffer_info->length = len;
		buffer_info->time_stamp = jiffies;
		buffer_info->dma = dma_map_page(&pdev->dev, (struct page *)frag, offset, len, DMA_TO_DEVICE);

		buffer_info->mapped_as_page = true;
		if (dma_mapping_error(&pdev->dev, buffer_info->dma))
			goto dma_unwind;
		segs = skb_shinfo(skb)->gso_segs ? : 1;
		tx_ring->buffer_info[i].skb = skb;
		tx_ring->buffer_info[i].segs = segs;
		tx_ring->buffer_info[i].transfering = 1;

		count++;
	}


	return count;
dma_unwind:
	dev_err(&pdev->dev, "Tx DMA map failed at dma_unwind\n");
	while(count-- > 0) {
		i--;
		if (i == 0) {
			i = tx_ring->count;
		}
		buffer_info = &tx_ring->buffer_info[i];
		if (buffer_info->dma) {
			if (buffer_info->mapped_as_page)
				dma_unmap_page(&pdev->dev, buffer_info->dma,
						buffer_info->length, DMA_TO_DEVICE);
			else
				dma_unmap_single(&pdev->dev, buffer_info->dma,
						buffer_info->length, DMA_TO_DEVICE);
			buffer_info->dma = 0;
		}
		if (buffer_info->skb) {
			dev_kfree_skb_any(buffer_info->skb);
			buffer_info->skb = NULL;
		}
		buffer_info->time_stamp = 0;
	}

dma_error:
	dev_err(&pdev->dev, "Tx DMA map failed at dma_error\n");
	buffer_info->dma = 0;
	return -ENOMEM;
}

static void ingenic_mac_restart_tx_dma(struct ingenic_mac_local *lp) {
	/* TODO: clear error status bits if any */
	synopGMACdevice *gmacdev = lp->gmacdev;
	u32 data;

	data = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaControl);
	if (data & DmaTxStart) {
		synopGMAC_resume_dma_tx(gmacdev);
	} else {
		synopGMAC_enable_dma_tx(gmacdev);
	}

	/* ensure irq is enabled */
	synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
}

static void ingenic_mac_tx_queue(struct ingenic_mac_local *lp,
		struct ingenic_mac_tx_ring *tx_ring)
{
	DmaDesc *tx_desc = NULL;
	struct ingenic_mac_buffer *buffer_info;
	unsigned int i;

	i = tx_ring->next_to_use;

	buffer_info = &tx_ring->buffer_info[i];
	tx_desc = INGENIC_MAC_TX_DESC(*tx_ring, i);

	tx_desc->length |= (((cpu_to_le32(buffer_info->length) <<DescSize1Shift) & DescSize1Mask)
			| ((0 <<DescSize2Shift) & DescSize2Mask));  // buffer2 is not used
	tx_desc->buffer1 = cpu_to_le32(buffer_info->dma);
	tx_desc->buffer2 = 0;
	tx_desc->status |=  (DescTxFirst | DescTxLast | DescTxIntEnable); //ENH_DESC
	tx_desc->status |= DescOwnByDma;//ENH_DESC

	wmb();

	buffer_info->transfering = 1;

	if (unlikely(++i == tx_ring->count)) i = 0;
	tx_ring->next_to_use = i;

	wmb();
	ingenic_mac_restart_tx_dma(lp);
}

static int ingenic_mac_hard_start_xmit(struct sk_buff *skb,
		struct net_device *netdev)
{
	struct ingenic_mac_local *lp = netdev_priv(netdev);
	synopGMACdevice *gmacdev = lp->gmacdev;
	struct ingenic_mac_tx_ring *tx_ring;
	unsigned int first;
	int count = 1;

	tx_ring = &lp->tx_ring;

#if 0
	/* this can be cacelled for we should support it */
	if (unlikely(skb->len <= 0)) {
		printk(JZMAC_DRV_NAME ": WARNING: skb->len < 0\n");
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
#endif

	/* this can be cacelled for we should support it*/
	/*
	   if (skb_shinfo(skb)->nr_frags) {
	   printk(JZMAC_DRV_NAME ": WARNING: fragment packet do not handled!!!\n");
	   dev_kfree_skb_any(skb);
	   return NETDEV_TX_OK;
	   }
	   */

	/* need: count + 2 desc gap to keep tail from touching
	 * head, otherwise try next time */
	if (unlikely(ingenic_mac_maybe_stop_tx(netdev, tx_ring, count + 2)))
		return NETDEV_TX_BUSY;

	first = tx_ring->next_to_use;
	count = ingenic_mac_tx_map(lp, tx_ring, skb);

	if (likely(count)) {
		ingenic_mac_tx_queue(lp, tx_ring);
		/* Make sure there is space in the ring for the next send.*/
		ingenic_mac_maybe_stop_tx(netdev, tx_ring, MAX_SKB_FRAGS + 2);
#ifdef DUMP_DEBUG
		ingenic_mac_dump_all_regs(gmacdev, __func__, __LINE__);
#endif
	} else {
		dev_kfree_skb_any(skb);
		tx_ring->buffer_info[first].time_stamp = 0;
		tx_ring->next_to_use = first;
	}

#ifdef DUMP_DEBUG
	ingenic_mac_dump_all_regs(gmacdev, __func__, __LINE__);
	ingenic_mac_phy_dump(lp);
	//ingenic_mac_dump_all_desc(lp);
#endif

	return NETDEV_TX_OK;
}

static bool ingenic_mac_clean_tx_irq(struct ingenic_mac_local *lp) {
	struct net_device *netdev = lp->netdev;
	struct ingenic_mac_buffer *buffer_info;
	struct ingenic_mac_tx_ring *tx_ring = &lp->tx_ring;
	DmaDesc *desc;
	unsigned int i;
	unsigned int count = 0;
	unsigned int total_tx_bytes=0, total_tx_packets=0;

	i = tx_ring->next_to_clean;
	desc = INGENIC_MAC_TX_DESC(*tx_ring, i);
	buffer_info = &tx_ring->buffer_info[i];

	while (buffer_info->transfering &&
			!synopGMAC_is_desc_owned_by_dma(desc) &&
			(count < tx_ring->count)) {

		buffer_info->transfering = 0;
		count++;

		if(synopGMAC_is_desc_valid(desc->status)){
			total_tx_packets ++;
			total_tx_bytes += buffer_info->length;
		}

		ingenic_mac_unmap_and_free_tx_resource(lp, buffer_info);
		synopGMAC_tx_desc_init_ring(desc, i == (tx_ring->count - 1));

		i++;
		if (unlikely(i == tx_ring->count)) i = 0;

		desc = INGENIC_MAC_TX_DESC(*tx_ring, i);
		buffer_info = &tx_ring->buffer_info[i];
	}

	tx_ring->next_to_clean = i;

#define TX_WAKE_THRESHOLD 16
	if (unlikely(count && netif_carrier_ok(netdev) &&
				INGENIC_MAC_DESC_UNUSED(tx_ring) >= TX_WAKE_THRESHOLD)) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (netif_queue_stopped(netdev)) {
			netif_wake_queue(netdev);
			++lp->restart_queue;
		}
	}

	lp->total_tx_bytes += total_tx_bytes;
	lp->total_tx_packets += total_tx_packets;
	lp->net_stats.tx_bytes += total_tx_bytes;
	lp->net_stats.tx_packets += total_tx_packets;

	return (count < tx_ring->count);
}

static bool ingenic_mac_clean_rx_irq(struct ingenic_mac_local *lp,
		int *work_done, int work_to_do) {

	struct net_device *netdev = lp->netdev;
	struct ingenic_mac_rx_ring *rx_ring = &lp->rx_ring;
	DmaDesc *rx_desc, *next_rxd;
	struct ingenic_mac_buffer *buffer_info, *next_buffer;
	u32 length;
	unsigned int i;
	unsigned int rx_desc_i;
	int cleaned_count = 0;
	bool cleaned = false;
	unsigned int total_rx_bytes=0, total_rx_packets=0;

	i = rx_ring->next_to_clean;
	rx_desc = INGENIC_MAC_RX_DESC(*rx_ring, i);
	buffer_info = &rx_ring->buffer_info[i];

	/* except the slot not used, if transfer done, buffer_info->invalid is always 0 */
	while ((!synopGMAC_is_desc_owned_by_dma(rx_desc)) && (!buffer_info->invalid)) {
		struct sk_buff *skb;

		if (*work_done >= work_to_do)
			break;
		(*work_done)++;
		rmb();  /* read descriptor and rx_buffer_info after status DD */

		buffer_info->invalid = 1;
		skb = buffer_info->skb;
		buffer_info->skb = NULL; /* cleaned */

		if(synopGMAC_is_rx_desc_valid(rx_desc->status))
			prefetch(skb->data - NET_IP_ALIGN);

		rx_desc_i = i;
		if (++i == rx_ring->count) i = 0;

		next_rxd = INGENIC_MAC_RX_DESC(*rx_ring, i);
		prefetch(next_rxd);

		next_buffer = &rx_ring->buffer_info[i];

		cleaned = true;
		cleaned_count++;

		if(!synopGMAC_is_rx_desc_valid(rx_desc->status)) {
			/* save the skb in buffer_info as good */
			buffer_info->skb = skb;
			//printk("====>invalid pkt\n");
			goto invalid_pkt;
		}

		length = synopGMAC_get_rx_desc_frame_length(rx_desc->status);
		synopGMAC_rx_desc_init_ring(rx_desc, rx_desc_i == (rx_ring->count - 1));
#if 0
		printk("============================================\n");
		ingenic_mac_dump_pkt_data((unsigned char *)CKSEG1ADDR(buffer_info->dma),
				length - 4);
		printk("============================================\n");
#endif
		dma_unmap_single(&lp->netdev->dev,
				buffer_info->dma, buffer_info->length,
				DMA_FROM_DEVICE);
		buffer_info->dma = 0;


		/* adjust length to remove Ethernet CRC, this must be
		 * done after the TBI_ACCEPT workaround above */
		length -= 4;

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += length;
		total_rx_packets++;

		/* code added for copybreak, this should improve
		 * performance for small packets with large amounts
		 * of reassembly being done in the stack */
		if (length < copybreak) {
			struct sk_buff *new_skb =
				netdev_alloc_skb_ip_align(netdev, length);

			if (new_skb) {
				skb_copy_to_linear_data_offset(new_skb,
						-NET_IP_ALIGN,
						(skb->data -
						 NET_IP_ALIGN),
						(length +
						 NET_IP_ALIGN));
				/* save the skb in buffer_info as good */
				buffer_info->skb = skb;
				skb = new_skb;
			}
			/* else just continue with the old one */
		}

		/* end copybreak code */
		skb_put(skb, length);
		skb->protocol = eth_type_trans(skb, netdev);

		//ingenic_mac_dump_skb_data(skb);
		netif_receive_skb(skb); //x2000 delete
//		napi_gro_receive(&lp->napi, skb); //x2000 add
		//netdev->last_rx = jiffies;

invalid_pkt:
		rx_desc->status = 0;
		/* return some buffers to hardware, one at a time is too slow */
		if (unlikely(cleaned_count >= INGENIC_MAC_RX_BUFFER_WRITE)) {
			ingenic_mac_alloc_rx_buffers(lp, cleaned_count, 1);
			cleaned_count = 0;
		}

		/* use prefetched values */
		rx_desc = next_rxd;
		buffer_info = next_buffer;
	}

	rx_ring->next_to_clean = i;

	cleaned_count = INGENIC_MAC_DESC_UNUSED(rx_ring);
	if (cleaned_count)
		ingenic_mac_alloc_rx_buffers(lp, cleaned_count, 1);

	lp->total_rx_packets += total_rx_packets;
	lp->total_rx_bytes += total_rx_bytes;
	lp->net_stats.rx_bytes += total_rx_bytes;
	lp->net_stats.rx_packets += total_rx_packets;

	return cleaned;
}

/**
 * ingenic_mac_clean - NAPI Rx polling callback
 **/
static int ingenic_mac_clean(struct napi_struct *napi, int budget) {
	struct ingenic_mac_local *lp = container_of(napi, struct ingenic_mac_local, napi);
	synopGMACdevice *gmacdev = lp->gmacdev;
	int tx_cleaned = 0;
	int work_done = 0;

	spin_lock(&lp->napi_poll_lock);

	tx_cleaned = ingenic_mac_clean_tx_irq(lp);

	ingenic_mac_clean_rx_irq(lp, &work_done, budget);

	if (!tx_cleaned)
		work_done = budget;

	//printk("===>workdone = %d, budget = %d\n", work_done, budget);
	//ingenic_mac_dump_all_regs(gmacdev, __func__, __LINE__);

	/* If budget not fully consumed, exit the polling mode */
	if (work_done < budget) {
		napi_complete(napi);
		synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
	}

	spin_unlock(&lp->napi_poll_lock);
	return work_done;
}

/* interrupt routine to handle rx and error signal */
static irqreturn_t ingenic_mac_interrupt(int irq, void *data)
{
	struct net_device *netdev = data;
	struct ingenic_mac_local *lp = netdev_priv(netdev);
	synopGMACdevice *gmacdev = lp->gmacdev;
	u32 interrupt,dma_status_reg;
	u32 mac_interrupt_status;
	u32 rgmii_interrupt_status;

	/* Read the Dma interrupt status to know whether the interrupt got generated by our device or not*/
	dma_status_reg = synopGMACReadReg((u32 *)gmacdev->DmaBase, DmaStatus);
	mac_interrupt_status = synopGMACReadReg((u32 *)gmacdev->MacBase, GmacInterruptStatus);

	//printk("===>enter %s:%d DmaStatus = 0x%08x\n", __func__, __LINE__, dma_status_reg);
	if(dma_status_reg == 0)
		return IRQ_NONE;

	synopGMAC_disable_interrupt_all(gmacdev);


	if(dma_status_reg & GmacPmtIntr){
		dev_err(&netdev->dev, "%s:: Interrupt due to PMT module\n",__FUNCTION__);
		//synopGMAC_linux_powerup_mac(gmacdev);
	}

	if(dma_status_reg & GmacMmcIntr){
		dev_err(&netdev->dev, "%s:: Interrupt due to MMC module\n",__FUNCTION__);
		dev_err(&netdev->dev, "%s:: synopGMAC_rx_int_status = %08x\n",
				__FUNCTION__,synopGMAC_read_mmc_rx_int_status(gmacdev));
		dev_err(&netdev->dev, "%s:: synopGMAC_tx_int_status = %08x\n",
				__FUNCTION__,synopGMAC_read_mmc_tx_int_status(gmacdev));
	}

	if(dma_status_reg & GmacLineIntfIntr){
		if (mac_interrupt_status & GmacRgmiiIntSts) {
			rgmii_interrupt_status = synopGMACReadReg((u32 *)gmacdev->MacBase, GmacRGMIIControl);
			dev_dbg(&netdev->dev, "%s:Interrupts to RGMII/SGMII: 0x%08x\n",__FUNCTION__,rgmii_interrupt_status);
		} else
			dev_err(&netdev->dev, "%s:: Interrupt due to GMAC LINE module\n",__FUNCTION__);
	}

	/* Now lets handle the DMA interrupts*/
	interrupt = synopGMAC_get_interrupt_type(gmacdev);
	dev_dbg(&netdev->dev, "%s:Interrupts to be handled: 0x%08x\n",__FUNCTION__,interrupt);


	if(interrupt & synopGMACDmaError){
		dev_err(&netdev->dev, "%s::Fatal Bus Error Inetrrupt Seen\n",__FUNCTION__);
		/* do nothing here, let tx_timeout to handle it */
	}

	if(interrupt & synopGMACDmaRxAbnormal){
		dev_dbg(&netdev->dev, "%s::Abnormal Rx Interrupt Seen\n",__FUNCTION__);
		synopGMAC_resume_dma_rx(gmacdev);
	}

	if(interrupt & synopGMACDmaRxStopped){
		// Receiver gone in to stopped state
		// why Rx Stopped? no enough descriptor? but why no enough descriptor need cause an interrupt?
		// we have no enough descriptor because we can't handle packets that fast, isn't it?
		// So I think if DmaRxStopped Interrupt can disabled
		dev_info(&netdev->dev, "%s::Receiver stopped seeing Rx interrupts\n",__FUNCTION__);

		synopGMAC_enable_dma_rx(gmacdev);
	}

	if(interrupt & synopGMACDmaTxAbnormal){
		dev_dbg(&netdev->dev, "%s::Abnormal Tx Interrupt Seen\n",__FUNCTION__);
	}

	if(interrupt & synopGMACDmaTxStopped){
		dev_err(&netdev->dev, "%s::Transmitter stopped sending the packets\n",__FUNCTION__);
		synopGMAC_disable_dma_tx(gmacdev);
		ingenic_mac_dump_all_desc(lp);
		ingenic_mac_take_desc_ownership_tx(lp);
		synopGMAC_enable_dma_tx(gmacdev);
	}

	if (likely(napi_schedule_prep(&lp->napi))) {
		lp->total_tx_bytes = 0;
		lp->total_tx_packets = 0;
		lp->total_rx_bytes = 0;
		lp->total_rx_packets = 0;
		dev_dbg(&netdev->dev, "enter %s:%d, call __napi_schedule\n", __func__, __LINE__);
		__napi_schedule(&lp->napi);
	} else {
		/* this really should not happen! if it does it is basically a
		 * bug, but not a hard error, so enable ints and continue */
		synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
	}

	return IRQ_HANDLED;
}

/* MAC control and configuration */

static void ingenic_mac_stop_activity(struct ingenic_mac_local *lp) {
	synopGMACdevice *gmacdev = lp->gmacdev;
	synopGMAC_disable_interrupt_all(gmacdev);

	// Disable the Dma in rx path
	synopGMAC_disable_dma_rx(gmacdev);
	ingenic_mac_take_desc_ownership_rx(lp);
	//msleep(100);	      // Allow any pending buffer to be read by host
	//synopGMAC_rx_disable(gmacdev);

	synopGMAC_disable_dma_tx(gmacdev);
	ingenic_mac_take_desc_ownership_tx(lp);
	//msleep(100);	      // allow any pending transmission to complete
	// Disable the Mac for both tx and rx
	//synopGMAC_tx_disable(gmacdev);
}

static void ingenic_mac_disable(struct ingenic_mac_local *lp) {
	/* First ensure that the upper network stack is stopped */
	/* can be netif_tx_disable when NETIF_F_LLTX is removed */
	netif_stop_queue(lp->netdev); /* tx */
	napi_disable(&lp->napi); /* rx */

	ingenic_mac_stop_activity(lp);

	del_timer_sync(&lp->watchdog_timer); //x2000 delete

#if 0 // t40 0  x2000 open
	spin_lock(&lp->napi_poll_lock);
	desc_list_reinit(lp);
	spin_unlock(&lp->napi_poll_lock);
#endif
}

static void ingenic_mac_init(struct ingenic_mac_local *lp) {
	synopGMACdevice *gmacdev = lp->gmacdev;
	/*
	 * disable the watchdog and gab to receive frames up to 16384 bytes
	 * to adjust IP protocol
	 */
	synopGMAC_wd_disable(gmacdev);
	synopGMAC_jab_disable(gmacdev);

	/* cancel to set Frame Burst Enable for now we use duplex mode */
	//synopGMAC_frame_burst_enable(gmacdev);

	/* set jumbo to allow to receive Jumbo frames of 9,018 bytes */
	//synopGMAC_jumbo_frame_disable(gmacdev);
	synopGMAC_jumbo_frame_enable(gmacdev);

	/* for we try to use duplex */
	synopGMAC_rx_own_disable(gmacdev);
	synopGMAC_loopback_off(gmacdev);
	/* default to full duplex, I think this will be the common case */
	synopGMAC_set_full_duplex(gmacdev);
	/* here retry enabe may useless */
	synopGMAC_retry_enable(gmacdev);
	synopGMAC_pad_crc_strip_disable(gmacdev);
	synopGMAC_back_off_limit(gmacdev,GmacBackoffLimit0);
	synopGMAC_deferral_check_disable(gmacdev);
	synopGMAC_tx_enable(gmacdev);
	synopGMAC_rx_enable(gmacdev);

	/* default to 100M, I think this will be the common case */
	synopGMAC_select_mii(gmacdev);
	synopGMAC_select_speed100(gmacdev);

	/* Frame Filter Configuration */
	synopGMAC_frame_filter_enable(gmacdev);
	synopGMAC_set_pass_control(gmacdev,GmacPassControl0);
	synopGMAC_broadcast_enable(gmacdev);
	synopGMAC_src_addr_filter_disable(gmacdev);
	synopGMAC_multicast_disable(gmacdev);
	synopGMAC_dst_addr_filter_normal(gmacdev);
	synopGMAC_multicast_hash_filter_disable(gmacdev);
	synopGMAC_promisc_disable(gmacdev);
	synopGMAC_unicast_hash_filter_disable(gmacdev);

	/*Flow Control Configuration*/
#if 1  /* t40 old */
	synopGMAC_unicast_pause_frame_detect_enable(gmacdev);
	synopGMAC_rx_flow_control_enable(gmacdev);
	synopGMAC_tx_flow_control_enable(gmacdev);
#else  /* x2000 code */
	synopGMAC_unicast_pause_frame_detect_disable(gmacdev);
	synopGMAC_rx_flow_control_disable(gmacdev);
	synopGMAC_tx_flow_control_disable(gmacdev);
#endif
}

static void ingenic_mac_configure(struct ingenic_mac_local *lp) {
	synopGMACdevice *gmacdev = lp->gmacdev;
#ifdef ENH_DESC_8W
#if 1  /* t40 before */
	/* pbl32 incr with rxthreshold 128 and Desc is 8 Words */
	synopGMAC_dma_bus_mode_init(gmacdev, DmaBurstLength32 | DmaDescriptorSkip2 | DmaDescriptor8Words | DmaFixedBurstEnable | 0x02000000);
	/* pbl32 incr with rxthreshold 128 and Desc is 8 Words */
	//synopGMAC_dma_bus_mode_init(gmacdev, DmaBurstLength32 | DmaDescriptorSkip2 | DmaDescriptor8Words | DmaFixedBurstEnable);
#else  /* x2000 change */
#ifndef CONFIG_INGENIC_MAC_AXI_BUS //x2000 add 
	synopGMAC_dma_bus_mode_init(gmacdev, DmaBurstLength32 | DmaDescriptorSkip2 | DmaDescriptor8Words | DmaFixedBurstEnable | 0x02000000); //pbl32 incr with rxthreshold 128 and Desc is 8 Words
#else
	synopGMAC_dma_bus_mode_init(gmacdev, DmaBurstLength32 | DmaDescriptorSkip1 | DmaDescriptor8Words);
#endif
#endif
#else
	/* pbl32 incr with rxthreshold 128 */
	synopGMAC_dma_bus_mode_init(gmacdev, DmaBurstLength32 | DmaDescriptorSkip2);
#endif
	/* DmaRxThreshCtrl128 is ok for the RX FIFO is configured to 256 Bytes */
	synopGMAC_dma_control_init(gmacdev,DmaStoreAndForward |DmaTxSecondFrame|DmaRxThreshCtrl128);

	/*Initialize the mac interface*/

	ingenic_mac_init(lp);
	//synopGMAC_pause_control(gmacdev); // This enables the pause control in Full duplex mode of operation

#ifdef IPC_OFFLOAD
	/*IPC Checksum offloading is enabled for this driver. Should only be used if Full Ip checksumm offload engine is configured in the hardware*/
	synopGMAC_enable_rx_chksum_offload(gmacdev);  	//Enable the offload engine in the receive path
	synopGMAC_rx_tcpip_chksum_drop_enable(gmacdev); // This is default configuration, DMA drops the packets if error in encapsulated ethernet payload
	// The FEF bit in DMA control register is configured to 0 indicating DMA to drop the errored frames.
	/*Inform the Linux Networking stack about the hardware capability of checksum offloading*/
	netdev->features = NETIF_F_HW_CSUM;
#endif

	synopGMAC_clear_interrupt(gmacdev);
	/*
	   Disable the interrupts generated by MMC and IPC counters.
	   If these are not disabled ISR should be modified accordingly to handle these interrupts.
	   */
	synopGMAC_disable_mmc_tx_interrupt(gmacdev, 0xFFFFFFFF);
	synopGMAC_disable_mmc_rx_interrupt(gmacdev, 0xFFFFFFFF);
	synopGMAC_disable_mmc_ipc_rx_interrupt(gmacdev, 0xFFFFFFFF);
}

/*
 * Enable Interrupts, Receive, and Transmit(The same sequence as ingenic_mac_open, only a bit different)
 */
static void ingenic_mac_enable(struct ingenic_mac_local *lp) {
	synopGMACdevice *gmacdev = lp->gmacdev;
	desc_list_init(lp); //x2000 delete
	ingenic_mac_configure(lp);

	napi_enable(&lp->napi);
	synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
	/* we only enable rx here */
	synopGMAC_enable_dma_rx(gmacdev);
	/* We can accept TX packets again */
	lp->netdev->trans_start = jiffies;
	netif_wake_queue(lp->netdev);
}

static void ingenic_mac_reinit_locked(struct ingenic_mac_local *lp)
{
	WARN_ON(in_interrupt());
	ingenic_mac_disable(lp);
	ingenic_mac_enable(lp);
}

/* Our watchdog timed out. Called by the networking layer */
static void ingenic_mac_tx_timeout(struct net_device *dev)
{
	struct ingenic_mac_local *lp = netdev_priv(dev);
    printk("%s,%d: \n", __func__, __LINE__);
	/* Do the reset outside of interrupt context */
	lp->tx_timeout_count++;
	schedule_work(&lp->reset_task);
}

static void ingenic_mac_reset_task(struct work_struct *work)
{
	struct ingenic_mac_local *lp =
		container_of(work, struct ingenic_mac_local, reset_task);

	ingenic_mac_reinit_locked(lp);
}

static void setup_mac_addr(synopGMACdevice *gmacdev, u8 *mac_addr) {
	synopGMAC_set_mac_addr(gmacdev,
			       GmacAddr0High,GmacAddr0Low,
			       mac_addr);
}

static int ingenic_mac_set_mac_address(struct net_device *dev, void *p) {
	struct ingenic_mac_local *lp = netdev_priv(dev);
	synopGMACdevice *gmacdev = lp->gmacdev;

	struct sockaddr *addr = p;
	if (netif_running(dev))
		return -EBUSY;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	setup_mac_addr(gmacdev, dev->dev_addr);
	return 0;
}

/*
 * Open and Initialize the interface
 *
 * Set up everything, reset the card, etc..
 */
static int ingenic_mac_open(struct net_device *dev)
{
	struct ingenic_mac_local *lp = netdev_priv(dev);
	synopGMACdevice *gmacdev = lp->gmacdev;
	int retval;

	pr_debug("%s: %s\n", dev->name, __func__);

	retval = phy_read(lp->phydev, MII_BMCR);
	retval &= ~(1 << 11);
	phy_write(lp->phydev, MII_BMCR, retval);

	/*
	 * Check that the address is valid.  If its not, refuse
	 * to bring the device up.  The user must specify an
	 * address using ifconfig eth0 hw ether xx:xx:xx:xx:xx:xx
	 */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		printk(KERN_WARNING INGENIC_MAC_DRV_NAME ": no valid ethernet hw addr\n");
		return -EINVAL;
	}

	phy_write(lp->phydev, MII_BMCR, BMCR_RESET);
	while(phy_read(lp->phydev, MII_BMCR) & BMCR_RESET);
	phy_start(lp->phydev);

	if (synopGMAC_reset(gmacdev) < 0) {
		printk("func:%s, synopGMAC_reset failed\n", __func__);
		phy_stop(lp->phydev);
		return -1;
	}

	/* init MDC CLK */
	synopGMAC_set_mdc_clk_div(gmacdev,GmiiCsrClk4);
	gmacdev->ClockDivMdc = synopGMAC_get_mdc_clk_div(gmacdev);

	/* initial rx and tx list */
	retval = desc_list_init(lp);

	if (retval)
		return retval;

	setup_mac_addr(gmacdev, dev->dev_addr);
	ingenic_mac_configure(lp);
#ifdef DUMP_DEBUG
	ingenic_mac_dump_all_regs(gmacdev, __func__, __LINE__);
	ingenic_mac_phy_dump(lp);
#endif
	napi_enable(&lp->napi);

	/* we are ready, reset GMAC and enable interrupts */
	synopGMAC_enable_interrupt(gmacdev,DmaIntEnable);
	/* we only enable rx here */
	synopGMAC_enable_dma_rx(gmacdev);

	/* We can accept TX packets again */
	lp->netdev->trans_start = jiffies;
	netif_start_queue(dev);

	return 0;
}

/*
 * this makes the board clean up everything that it can
 * and not talk to the outside world.   Caused by
 * an 'ifconfig ethX down'
 */
static int ingenic_mac_close(struct net_device *dev)
{
	struct ingenic_mac_local *lp = netdev_priv(dev);
	synopGMACdevice *gmacdev = lp->gmacdev;
    int data = 0;

	synopGMAC_clear_interrupt(gmacdev);
	synopGMAC_disable_interrupt_all(gmacdev);
	ingenic_mac_disable(lp);

	netif_carrier_off(dev);

	phy_stop(lp->phydev);
	data = phy_read(lp->phydev, MII_BMCR);
	phy_write(lp->phydev, MII_BMCR, (data | BMCR_PDOWN));

	/* free the rx/tx buffers */
	desc_list_free(lp);
	return 0;
}

static void ingenic_mac_change_rx_flags(struct net_device *dev, int flags)
{
	struct ingenic_mac_local *lp = netdev_priv(dev);
	synopGMACdevice *gmacdev = lp->gmacdev;
	if (dev->flags & IFF_PROMISC) {
		/* Accept any kinds of packets */
		synopGMAC_promisc_enable(gmacdev);
		//	synopGMAC_frame_filter_disable(gmacdev);
		printk("%s: Enter promisc mode!\n",dev->name);
	}else{
		synopGMAC_promisc_disable(gmacdev);
	}
}

static struct net_device_stats *ingenic_mac_get_stats(struct net_device *netdev)
{
	struct ingenic_mac_local *lp = netdev_priv(netdev);

	/* only return the current stats */
	return &lp->net_stats;
}

static int ingenic_mac_change_mtu(struct net_device *netdev, int new_mtu) {
	printk("===>new_mtu = %d\n", new_mtu);
#if 1
	return eth_change_mtu(netdev, new_mtu);
#else
	netdev->mtu = new_mtu;
	return 0;
#endif
}

static int ingenic_mac_do_ioctl(struct net_device *netdev, struct ifreq *ifr, s32 cmd) {
	struct ingenic_mac_local *lp = netdev_priv(netdev);

	if (!netif_running(netdev)) {
		printk("error : it is not in netif_running\n");
		return -EINVAL;
	}

	if(!(lp->phydev->link))
		return 0;

	return phy_mii_ioctl(lp->phydev, ifr, cmd);
}

/*
 * Multicast filter and config multicast hash table
 */
#define MULTICAST_FILTER_LIMIT 64
static void ingenic_mac_multicast_hash(struct net_device *dev)
{
    struct netdev_hw_addr *ha;
    u32 mc_filter[2];
    u32 crc;

    mc_filter[1] = mc_filter[0] = 0;
    netdev_for_each_mc_addr(ha, dev) {
        /* make sure this is a multicast address -
           shouldn't this be a given if we have it here ? */
        if (!(*ha->addr & 1))
            continue;
            crc = ether_crc(ETH_ALEN, ha->addr);
            /* Take upper 6bits HASH value after bitwise reversal Ether CRC */
            set_bit(~crc >> 26, (unsigned long *)mc_filter);
    }
    /* TODO: set multicast hash filter here */
    synopGMAC_write_hash_table_high(g_gmacdev, mc_filter[1]);
    synopGMAC_write_hash_table_low(g_gmacdev, mc_filter[0]);

}
static void ingenic_set_multicast_list(struct net_device *dev)
{
    if (dev->flags & IFF_PROMISC) {
       /* Accept any kinds of packets */
       synopGMAC_promisc_enable(g_gmacdev);
       printk("%s: Enter promisc mode!\n",dev->name);
    }else  if ((dev->flags & IFF_ALLMULTI) || (dev->mc.count > MULTICAST_FILTER_LIMIT)) {
       /* Accept all multicast packets */
       synopGMAC_multicast_enable(g_gmacdev);

       /* TODO: accept broadcast and enable multicast here */
       printk("%s: Enter allmulticast mode!   %d \n",dev->name,dev->mc.count);
    }else if (dev->mc.count) {
       /* Update multicast hash table */
       ingenic_mac_multicast_hash(dev);

       /* TODO: enable multicast here */
       synopGMAC_promisc_disable(g_gmacdev);
       synopGMAC_multicast_disable(g_gmacdev);
       synopGMAC_multicast_hash_filter_enable(g_gmacdev);
    } else {
       /* FIXME: clear promisc or multicast mode */
       synopGMAC_promisc_disable(g_gmacdev);
       synopGMAC_multicast_disable(g_gmacdev);
       synopGMAC_multicast_hash_filter_disable(g_gmacdev);
    }
}

static const struct net_device_ops ingenic_mac_netdev_ops = {
	.ndo_open		= ingenic_mac_open,
	.ndo_stop		= ingenic_mac_close,
	.ndo_start_xmit		= ingenic_mac_hard_start_xmit,
	.ndo_change_rx_flags	= ingenic_mac_change_rx_flags,
	.ndo_get_stats		= ingenic_mac_get_stats,
	.ndo_set_mac_address	= ingenic_mac_set_mac_address,
	.ndo_tx_timeout		= ingenic_mac_tx_timeout,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= ingenic_mac_change_mtu,
	.ndo_do_ioctl		= ingenic_mac_do_ioctl,
    .ndo_set_rx_mode        = ingenic_set_multicast_list,
};


/* Read an off-chip register in a PHY through the MDC/MDIO port */
static int ingenic_mdiobus_read(struct mii_bus *bus, int phy_addr, int regnum)
{
	struct ingenic_mac_local *lp = bus->priv;
	synopGMACdevice *gmacdev = lp->gmacdev;
	u16 data = 0;
	s32 status;

	status = synopGMAC_read_phy_reg(gmacdev, phy_addr, regnum, &data);

	if (status)
		data = 0;
	return (int)data;
}

/* Write an off-chip register in a PHY through the MDC/MDIO port */
static int ingenic_mdiobus_write(struct mii_bus *bus, int phy_addr, int regnum,
		u16 value)
{
	struct ingenic_mac_local *lp = bus->priv;
	synopGMACdevice *gmacdev = lp->gmacdev;

	return synopGMAC_write_phy_reg(gmacdev, phy_addr, regnum, value);
}

static int ingenic_mdiobus_reset(struct mii_bus *bus)
{
	return 0;
}

static int ingenic_mdio_phy_read(struct net_device *dev, int phy_id, int location)
{
	struct ingenic_mac_local *lp = netdev_priv(dev);
	return ingenic_mdiobus_read(lp->mii_bus, phy_id, location);
}

static void ingenic_mdio_phy_write(struct net_device *dev, int phy_id, int location, int value)
{
	struct ingenic_mac_local *lp = netdev_priv(dev);

	ingenic_mdiobus_write(lp->mii_bus, phy_id, location, value);
}

static void ingenic_mac_interface_set(struct ingenic_mac_local *lp)
{
	struct platform_device *pdev = lp->pdev;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	unsigned int cpm_mphyc;
	unsigned int mode;
	unsigned int data;
	unsigned int val;
	int err;

	err = of_property_read_u32(np, "ingenic,mac-mode", &mode);
	if (err < 0)
		return;
	lp->interface = mode;

	err = of_property_read_u32(np, "ingenic,mode-reg", &cpm_mphyc);
	if (err < 0)
		return;

	if(mode==RMII || mode==RGMII)
		val = mode;
	else
		val = 0;

	data = *(volatile unsigned int *)cpm_mphyc;
	*(volatile unsigned int *)cpm_mphyc = (data & ~0x7) | val;
}
#if 0
/**
 * ingenic_mac_get_settings - Exported for Ethtool to query PHY setting
 * @ndev:	pointer of net DEVICE structure
 * @cmd:
 */
static int ingenic_mac_get_settings(struct net_device *ndev, struct ethtool_cmd *cmd)
{
	struct ingenic_mac_local *ingenic_local = netdev_priv(ndev);
	return mii_ethtool_gset(&ingenic_local->mii, cmd);
}

/**
 * ingenic_mac_set_settings - Exported for Ethtool to set PHY setting
 * @ndev:	pointer of net DEVICE structure
 * @cmd:
 */
static int ingenic_mac_set_settings(struct net_device *ndev, struct ethtool_cmd *cmd)
{
        struct ingenic_mac_local *ingenic_local = netdev_priv(ndev);
        return mii_ethtool_sset(&ingenic_local->mii, cmd);
}

/**
 * ingenic_mac_get_drvinfo - Exported for Ethtool to query the driver version
 * @ndev:	pointer of net DEVICE structure
 * @info:
 */
static void ingenic_mac_get_drvinfo(struct net_device *ndev, struct ethtool_drvinfo *info)
{
	/* Inherit standard device info */
	strncpy (info->driver, INGENIC_MAC_DRV_NAME, sizeof info->driver);
	strncpy (info->version, INGENIC_MAC_DRV_VERSION, sizeof info->version);
}

/**
 * ingenic_mac_nway_reset - Exported for Ethtool to restart PHY autonegotiation
 * @ndev:	pointer of net DEVICE structure
 */
static int ingenic_mac_nway_reset(struct net_device *ndev)
{
        struct ingenic_mac_local *ingenic_local = netdev_priv(ndev);
        return mii_nway_restart(&ingenic_local->mii);
}


static struct ethtool_ops ingenic_mac_ethtool_ops = {
	.get_settings	= ingenic_mac_get_settings,
	.set_settings	= ingenic_mac_set_settings,
	.get_drvinfo	= ingenic_mac_get_drvinfo,
	.nway_reset	= ingenic_mac_nway_reset,
	.get_link	= ethtool_op_get_link,
};

#endif
static ssize_t mdio_cmd_set(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos);
static int mdio_cmd_open(struct inode *inode, struct file *file);
static const struct file_operations mdio_cmd_fops ={
	.read = seq_read,
	.open = mdio_cmd_open,
	.llseek = seq_lseek,
	.release = single_release,
	.write = mdio_cmd_set,
};

static int ingenic_mac_probe(struct platform_device *pdev)
{
	synopGMACdevice *gmacdev;
	struct resource *r_mem = NULL;
	struct net_device *ndev = NULL;
	struct device_node *child = NULL;
	struct ingenic_mac_local *lp;
	const struct of_device_id *match;
	struct mii_bus *miibus;
	char clk_name[16];
	unsigned int mode;
	int rc = 0, i;
	struct proc_dir_entry *proc;
    unsigned int phy_clk_freq;

	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_mem) {
		dev_err(&pdev->dev, "no IO resource defined.\n");
		return -ENXIO;
	}

	gmacdev = devm_kzalloc(&pdev->dev, sizeof (synopGMACdevice), GFP_KERNEL);
	if(!gmacdev)
		return -ENOMEM;

	ndev = alloc_etherdev(sizeof(struct ingenic_mac_local));
	if (!ndev) {
		dev_err(&pdev->dev, "Cannot allocate net device!\n");
		return -ENOMEM;
	}

	pdev->id = of_alias_get_id(pdev->dev.of_node, "mac");
	SET_NETDEV_DEV(ndev, &pdev->dev);
	platform_set_drvdata(pdev, ndev);
	lp = netdev_priv(ndev);
	lp->netdev = ndev;
	lp->pdev = pdev;
	lp->gmacdev = gmacdev;
	lp->id = pdev->id;
	lp->interface = RMII;

	match = of_match_node(ingenic_mac_dt_match, pdev->dev.of_node);
	if (!match) {
		rc = -ENODEV;
		goto err_out_free_netdev;
	}

	lp->baseaddr = devm_ioremap_resource(&pdev->dev, r_mem);
	if (IS_ERR(lp->baseaddr)) {
		dev_err(&pdev->dev, "failed to map baseaddress.\n");
		rc = PTR_ERR(lp->baseaddr);
		goto err_out_free_netdev;
	}

	gmacdev->DmaBase = (u32)lp->baseaddr + DMABASE;
	gmacdev->MacBase = (u32)lp->baseaddr + MACBASE;

	rc = of_property_read_u32(pdev->dev.of_node, "ingenic,mac-mode", &mode);
	if (rc < 0) {
		dev_err(&pdev->dev, "Get mac-mode value failed\n");
		goto err_out_free_netdev;
	}
	lp->interface = mode;

	sprintf(clk_name, "gate_gmac");
	lp->clk_gate = devm_clk_get(&pdev->dev, clk_name);
	if (IS_ERR(lp->clk_gate)) {
		rc = PTR_ERR(lp->clk_gate);
		dev_err(&pdev->dev, "%s:can't get clk %s\n", __func__,clk_name);
		goto err_out_free_netdev;
	}
	sprintf(clk_name, "div_macphy");
	lp->clk_cgu = devm_clk_get(&pdev->dev, clk_name);
	if (IS_ERR(lp->clk_cgu)) {
		rc = PTR_ERR(lp->clk_cgu);
		dev_err(&pdev->dev, "%s:can't get clk %s\n", __func__,clk_name);
		goto err_out_clk_gate_put;
	}

	if ((rc = clk_prepare_enable(lp->clk_gate)) < 0) {
		dev_err(&pdev->dev, "Enable gmac gate clk failed\n");
		goto err_out_clk_cgu_disable;
	}
	if ((rc = clk_prepare_enable(lp->clk_cgu)) < 0) {
		dev_err(&pdev->dev, "Enable gmac cgu clk failed\n");
		goto err_out_clk_gate_disable;
	}

	rc = of_property_read_u32(pdev->dev.of_node, "ingenic,phy-clk-freq", &phy_clk_freq);
	if (rc < 0) {
		phy_clk_freq = 50000000;
	}

	if (clk_set_rate(lp->clk_cgu, phy_clk_freq) || (clk_prepare_enable(lp->clk_cgu))) {
		dev_err(&pdev->dev, "Set cgu_mac clk rate faild\n");
		rc = -ENODEV;
		goto err_out_clk_gate_disable;
	}

	ingenic_mac_interface_set(lp);

	if (ingenic_mac_phy_hwrst(pdev, true)) {
		rc = -ENODEV;
		goto err_out_clk_cgu_disable;
	}

	if (synopGMAC_reset(gmacdev)) {
		dev_err(&pdev->dev, "PROB:synopGMAC_reset timeout...\n");
		rc = -ETIMEDOUT;
		goto err_out_clk_cgu_disable;
	}

	synopGMAC_disable_interrupt_all(gmacdev);

	if (!!(child = of_get_child_by_name(pdev->dev.of_node, "mdio-gpio")) &&
			of_device_is_available(child)) {
		if (!(lp->mii_pdev = of_platform_device_create(child, NULL, &pdev->dev))) {
			rc = -ENODEV;
			goto err_out_clk_cgu_disable;
		}
		lp->use_mdio_goio = true;
		lp->mii_bus = platform_get_drvdata(lp->mii_pdev);
	} else {
		lp->use_mdio_goio = false;
		if (!(miibus = devm_mdiobus_alloc(&pdev->dev))) {
			rc = -ENOMEM;
			goto err_out_clk_cgu_disable;
		}
		miibus->priv = lp;
		miibus->read = ingenic_mdiobus_read;
		miibus->write = ingenic_mdiobus_write;
		miibus->reset = ingenic_mdiobus_reset;
		miibus->parent = &pdev->dev;
		miibus->name = "ingenic_mii_bus";
		snprintf(miibus->id, MII_BUS_ID_SIZE, "%d", lp->id);
		miibus->irq = devm_kzalloc(&pdev->dev, sizeof(int) * PHY_MAX_ADDR, GFP_ATOMIC);
		for (i = 0; i < PHY_MAX_ADDR; ++i)
			miibus->irq[i] = PHY_POLL;

		/* init MDC CLK */
		synopGMAC_set_mdc_clk_div(gmacdev, GmiiCsrClk4);

		rc = mdiobus_register(miibus);
		if (rc) {
			dev_err(&pdev->dev, "Cannot register MDIO bus!\n");
			goto err_out_clk_cgu_disable;
		}
		lp->mii_bus = miibus;
	}

	/*Lets read the version of ip in to device structure*/
	synopGMAC_read_version(gmacdev);

	/* configure MAC address */
	if (bootargs_ethaddr) {
		for (i=0; i<6; i++) {
			ndev->dev_addr[i] = ethaddr_hex[i];
		}
	} else {
		random_ether_addr(ndev->dev_addr);
	}
	setup_mac_addr(gmacdev, ndev->dev_addr);

	rc = mii_probe(ndev);
	if (rc) {
		dev_err(&pdev->dev, "MII Probe failed!\n");
		goto err_out_miibus_unregister;
	}

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(ndev);

	ndev->netdev_ops = &ingenic_mac_netdev_ops;
	ndev->watchdog_timeo = 2 * HZ;

	lp->mii.phy_id	= lp->phydev->addr;
	lp->mii.phy_id_mask  = 0x1f;
	lp->mii.reg_num_mask = 0x1f;
	lp->mii.dev	= ndev;
	lp->mii.mdio_read    = ingenic_mdio_phy_read;
	lp->mii.mdio_write   = ingenic_mdio_phy_write;
	lp->mii.supports_gmii	= mii_check_gmii_support(&lp->mii);

	init_timer(&lp->watchdog_timer);
	lp->watchdog_timer.data = (unsigned long)lp;
	lp->watchdog_timer.function = &ingenic_mac_watchdog;

	netif_napi_add(ndev, &lp->napi, ingenic_mac_clean, 32);

	spin_lock_init(&lp->link_lock);
	spin_lock_init(&lp->napi_poll_lock);

	INIT_WORK(&lp->reset_task, ingenic_mac_reset_task);

	/* register irq handler */
	rc = platform_get_irq(pdev, 0);
	if (rc < 0)
		goto err_out_miibus_unregister;

	rc = devm_request_irq(&pdev->dev, rc, ingenic_mac_interrupt, 0, "ingenic_mac", ndev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot request ingenic MAC IRQ!\n");
		rc = -EBUSY;
		goto err_out_miibus_unregister;
	}

	strcpy(ndev->name, "eth%d");
	rc = register_netdev(ndev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot register net device!\n");
		goto err_out_miibus_unregister;
	}

	dev_info(&pdev->dev, "%s, Version %s\n", INGENIC_MAC_DRV_DESC, INGENIC_MAC_DRV_VERSION);

	g_gmacdev = gmacdev;
	/* proc info */
	proc = jz_proc_mkdir("mdio");
	if (!proc) {
		printk("create mdio info failed!\n");
	}
	proc_create_data("cmd", S_IRUGO, proc, &mdio_cmd_fops, NULL);

	return 0;

err_out_miibus_unregister:
	if (lp->use_mdio_goio)
		platform_device_unregister(lp->mii_pdev);
	else
		mdiobus_unregister(lp->mii_bus);
err_out_clk_cgu_disable:
	clk_disable_unprepare(lp->clk_cgu);
err_out_clk_gate_disable:
	clk_disable_unprepare(lp->clk_gate);
err_out_clk_cgu_put:
		devm_clk_put(&pdev->dev, lp->clk_cgu);
err_out_clk_gate_put:
		devm_clk_put(&pdev->dev, lp->clk_gate);
err_out_free_netdev:
	platform_set_drvdata(pdev, NULL);
	free_netdev(ndev);
	return rc;
}

static int  ingenic_mac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ingenic_mac_local *lp = netdev_priv(ndev);

	unregister_netdev(ndev);

	if (lp->use_mdio_goio)
		platform_device_unregister(lp->mii_pdev);
	else
		mdiobus_unregister(lp->mii_bus);

	clk_disable_unprepare(lp->clk_cgu);

	clk_disable_unprepare(lp->clk_gate);

	platform_set_drvdata(pdev, NULL);

	free_netdev(ndev);

	return 0;
}

#ifdef CONFIG_PM
static int ingenic_mac_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct net_device *net_dev = platform_get_drvdata(pdev);
	struct ingenic_mac_local *lp = netdev_priv(net_dev);

	if (netif_running(net_dev))
		ingenic_mac_close(net_dev);

	clk_disable_unprepare(lp->clk_cgu);
#ifndef CONFIG_FPGA_TEST
	clk_disable_unprepare(lp->clk_gate);
#endif
	return 0;
}

static int ingenic_mac_resume(struct platform_device *pdev)
{
	struct net_device *net_dev = platform_get_drvdata(pdev);
	struct ingenic_mac_local *lp = netdev_priv(net_dev);
#ifndef CONFIG_FPGA_TEST
	clk_prepare_enable(lp->clk_gate);
#endif
	clk_prepare_enable(lp->clk_cgu);

	if(ingenic_mac_phy_hwrst(pdev, false) < 0)
		return -1;

	if (netif_running(net_dev))
		ingenic_mac_open(net_dev);

	return 0;
}
#else
#define ingenic_mac_suspend NULL
#define ingenic_mac_resume NULL
#endif	/* CONFIG_PM */

static const struct of_device_id ingenic_mac_dt_match[] = {
	{ .compatible = "ingenic,t31-mac", .data = NULL },
	{ .compatible = "ingenic,t40-mac", .data = NULL },
	{},
};
MODULE_DEVICE_TABLE(of, ingenic_mac_dt_match);

static struct platform_driver ingenic_mac_driver = {
	.driver = {
		.name = INGENIC_MAC_DRV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ingenic_mac_dt_match),
	},
	.probe = ingenic_mac_probe,
	.remove = ingenic_mac_remove,
	.resume = ingenic_mac_resume,
	.suspend = ingenic_mac_suspend,
};
module_platform_driver(ingenic_mac_driver)

/* cmd */
#define MDIO_CMD_BUF_SIZE 100
static uint8_t mdio_cmd_buf[100];
        static int mdio_cmd_show(struct seq_file *m, void *v)
{
    int len = strlen(mdio_cmd_buf) + 1;

    seq_printf(m, "%s\n", mdio_cmd_buf);

    return len;
}

static ssize_t mdio_cmd_set(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos)
{
	int ret = 0;
	char *buf = kzalloc((count+1), GFP_KERNEL);
	if(!buf)
		return -ENOMEM;
	if(copy_from_user(buf, buffer, count))
	{
		kfree(buf);
		return EFAULT;
	}
	if (!strncmp(buf, "r reg", sizeof("r reg")-1)) {
		uint32_t phybase;
		uint32_t regoffset;
		uint16_t data = 0;
		ret = sscanf(buf, "r reg:%i-%i", &phybase, &regoffset);
		if (2!=ret)
			return EFAULT;
		{
			ret = down_interruptible(&mutex_mdio);
			if (0 != ret) {
				printk("err(%s): ret = %d\n", __func__, ret);
			}
			ret = synopGMAC_read_phy_reg(g_gmacdev, phybase, regoffset, &data);
			up(&mutex_mdio);
		}
		if (ret)
			printk("##### err %s.%d\n", __func__, __LINE__);
		printk("mdio: reg read 0x%x-0x%x:0x%x\n", phybase, regoffset, data);
		sprintf(mdio_cmd_buf, "mdio: reg read 0x%x-0x%x:0x%x\n", phybase, regoffset, data);
	} else if (!strncmp(buf, "w reg", sizeof("w reg")-1)) {
		uint32_t phybase;
		uint32_t regoffset;
		uint32_t data = 0;
		ret = sscanf(buf, "w reg:%i-%i-%i", &phybase, &regoffset, &data);
		if (3!=ret)
			return EFAULT;
		printk("mdio: reg write 0x%x-0x%x-0x%x\n", phybase, regoffset, data);
		{

			ret = down_interruptible(&mutex_mdio);
			if (0 != ret) {
				printk("err(%s): ret = %d\n", __func__, ret);
			}
			ret = synopGMAC_write_phy_reg(g_gmacdev, phybase, regoffset, data);
			up(&mutex_mdio);
		}
		if (!ret)
			sprintf(mdio_cmd_buf, "%s\n", "ok");
		else
			sprintf(mdio_cmd_buf, "%s\n", "nok");
	} else {
		sprintf(mdio_cmd_buf, "null");
	}
	kfree(buf);
	return count;
}
static int mdio_cmd_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, mdio_cmd_show, PDE_DATA(inode),8192);
}
