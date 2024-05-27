/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2021 by nick shen <xianghui.shen@ingneic.com>
 */
#ifndef __ASM_MACH_INGENIC_PDMA_H__
#define __ASM_MACH_INGENIC_PDMA_H__

#include <dt-bindings/dma/ingenic-pdma.h>
#define INGENIC_DMA_REQ_AUTO 0xff
#define INGENIC_DMA_CHAN_CNT 24
unsigned int pdma_maps[INGENIC_DMA_CHAN_CNT] = {
	INGENIC_DMA_REQ_AUTO,
	INGENIC_DMA_REQ_AUTO,
	INGENIC_DMA_REQ_AUTO,
	INGENIC_DMA_REQ_AIC1_TX,
	INGENIC_DMA_REQ_AIC_LOOP_RX,
	INGENIC_DMA_REQ_AIC_TX,
	INGENIC_DMA_REQ_AIC_F_RX,
	INGENIC_DMA_REQ_AUTO_TX,
	INGENIC_DMA_REQ_UART2_TX,
	INGENIC_DMA_REQ_UART2_RX,
	INGENIC_DMA_REQ_UART1_TX,
	INGENIC_DMA_REQ_UART1_RX,
	INGENIC_DMA_REQ_UART0_TX,
	INGENIC_DMA_REQ_UART0_RX,
	INGENIC_DMA_REQ_SSI0_TX,
	INGENIC_DMA_REQ_SSI0_RX,
	INGENIC_DMA_REQ_SSI1_TX,
	INGENIC_DMA_REQ_SSI1_RX,
	INGENIC_DMA_REQ_I2C0_TX,
	INGENIC_DMA_REQ_I2C0_RX,
	INGENIC_DMA_REQ_I2C1_TX,
	INGENIC_DMA_REQ_I2C1_RX,
	INGENIC_DMA_REQ_DES_TX,
	INGENIC_DMA_REQ_DES_RX,
};
#endif

