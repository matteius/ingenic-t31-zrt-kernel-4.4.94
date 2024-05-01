/*
 *  Based on drivers/serial/8250.c by Russell King.
 *
 *  Author:	Nicolas Pitre
 *  Created:	Feb 20, 2003
 *  Copyright:	(C) 2003 Monta Vista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Note 1: This driver is made separate from the already too overloaded
 * 8250.c because it needs some kirks of its own and that'll make it
 * easier to add DMA support.
 *
 * Note 2: I'm too sick of device allocation policies for serial ports.
 * If someone else wants to request an "official" allocation of major/minor
 * for this driver please be my guest.  And don't forget that new hardware
 * to come from Intel might have more than 3 or 4 of those UARTs.  Let's
 * hope for a better port registration and dynamic device allocation scheme
 * with the serial core maintainer satisfaction to appear soon.
 */


#if defined(CONFIG_SERIAL_INGENIC_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/serial_core.h>
#include <linux/circ_buf.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/bsearch.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/serial.h>
#include <uapi/linux/tty_flags.h>

#include "ingenic_uart.h"

#define ASYNC_CTS_FLOW		(1U << ASYNCB_CTS_FLOW) // still used here

#define PORT_NR 10
#define DMA_BUFFER 1024
#define COUNT_DMA_BUFFER 2048
static unsigned short quot1[3] = {0}; /* quot[0]:baud_div, quot[1]:umr, quot[2]:uacr */
struct uart_ingenic_port {
	struct uart_port        port;
	unsigned char           ier;
	unsigned char           lcr;
	unsigned char           mcr;/* just store bit status of MDCE and FCM in UART_MCR */
	unsigned char           old_mcr; /* store the old value of UART_MCR */
	unsigned int            lsr_break_flag;
	struct clk		*clk;
	char			name[16];

#ifdef CONFIG_DEBUG_FS
	struct dentry	*debugfs;
#endif
};
static inline void check_modem_status(struct uart_ingenic_port *up);
static unsigned short *serial47xx_get_divisor(struct uart_port *port, unsigned int baud);
static inline void serial_dl_write(struct uart_port *up, int value);

static struct baudtoregs_t
{
	unsigned int baud;
	unsigned short div;
	unsigned int umr:5;
	unsigned int uacr:12;
} baudtoregs[] = {
	/*
	  The data is generated by a python,
	  the script is tools/tty/get_divisor.py
	 */
 #if (CONFIG_EXTAL_CLOCK == 24)
	{50,0x7530,0x10,0x0},
	{75,0x4e20,0x10,0x0},
	{110,0x3521,0x10,0x0},
	{134,0x2b9d,0x10,0x0},
	{150,0x2710,0x10,0x0},
	{200,0x1d4c,0x10,0x0},
	{300,0x1388,0x10,0x0},
	{600,0x9c4,0x10,0x0},
	{1200,0x4e2,0x10,0x0},
	{1800,0x340,0x10,0x0},
	{2400,0x271,0x10,0x0},
	{4800,0x138,0x10,0x0},
	{9600,0x9c,0x10,0x0},
	{19200,0x4e,0x10,0x0},
	{38400,0x27,0x10,0x0},
	{57600,0x1a,0x10,0x0},
	{115200,0xd,0x10,0x0},
	{230400,0x6,0x11,0x252},
	{460800,0x3,0x11,0x252},
	{500000,0x3,0x10,0x0},
	{576000,0x3,0xd,0xfef},
	{921600,0x2,0xd,0x0},
	{1000000,0x2,0xc,0x0},
	{1152000,0x1,0x14,0xefb},
	{1500000,0x1,0x10,0x0},
	{2000000,0x1,0xc,0x0},
	{2500000,0x1,0x9,0x6b5},
	{3000000,0x1,0x8,0x0},
	{3500000,0x1,0x6,0xbf7},
	{4000000,0x1,0x6,0x0},
#elif (CONFIG_EXTAL_CLOCK == 26)
	{50,0x7ef4,0x10,0x0},
	{75,0x546b,0x10,0x0},
	{110,0x398f,0x10,0x0},
	{134,0x2f40,0x10,0x0},
	{150,0x2a36,0x10,0x0},
	{200,0x1fbd,0x10,0x0},
	{300,0x151b,0x10,0x0},
	{600,0xa8e,0x10,0x0},
	{1200,0x547,0x10,0x0},
	{1800,0x385,0x10,0x0},
	{2400,0x2a4,0x10,0x0},
	{4800,0x152,0x10,0x0},
	{9600,0xa9,0x10,0x0},
	{19200,0x54,0x10,0x2},
	{38400,0x2a,0x10,0x2},
	{57600,0x1c,0x10,0x2},
	{115200,0xe,0x10,0x2},
	{230400,0x7,0x10,0x2},
	{460800,0x4,0xe,0x2},
	{500000,0x3,0x11,0x550},
	{576000,0x3,0xf,0x2},
	{921600,0x2,0xe,0x2},
	{1000000,0x2,0xd,0x0},
	{1152000,0x2,0xb,0x248},
	{1500000,0x1,0x11,0x550},
	{2000000,0x1,0xd,0x0},
	{2500000,0x1,0xa,0x2a0},
	{3000000,0x1,0x8,0x700},
	{3500000,0x1,0x7,0x2a0},
	{4000000,0x1,0x6,0x7c0},
#elif (CONFIG_EXTAL_CLOCK == 48)
	{50,0xea60,0x10,0x0},
	{75,0x9c40,0x10,0x0},
	{110,0x6a42,0x10,0x0},
	{134,0x573a,0x10,0x0},
	{150,0x4e20,0x10,0x0},
	{200,0x3a98,0x10,0x0},
	{300,0x2710,0x10,0x0},
	{600,0x1388,0x10,0x0},
	{1200,0x9c4,0x10,0x0},
	{1800,0x67f,0x10,0x0},
	{2400,0x4e2,0x10,0x0},
	{4800,0x271,0x10,0x0},
	{9600,0x138,0x10,0x0},
	{19200,0x9c,0x10,0x0},
	{38400,0x4e,0x10,0x0},
	{57600,0x34,0x10,0x0},
	{115200,0x1a,0x10,0x0},
	{230400,0xd,0x10,0x0},
	{460800,0x6,0x11,0x550},
	{500000,0x6,0x10,0x0},
	{576000,0x5,0x10,0x700},
	{921600,0x3,0x11,0x550},
	{1000000,0x3,0x10,0x0},
	{1152000,0x3,0xd,0x0},
	{1500000,0x2,0x10,0x0},
	{2000000,0x2,0xc,0x0},
	{2500000,0x1,0x13,0x84},
	{3000000,0x1,0x10,0x0},
	{3500000,0x1,0xd,0x600},
	{4000000,0x1,0xc,0x0},
#endif
};

/*
*Function:read register
*Parameter:struct uart_ingenic_port *up, int offset
*Return:unsigned int:Register address
*/
static inline unsigned int serial_in(struct uart_ingenic_port *up, int offset)
{
	offset <<= 2;
	return readl(up->port.membase + offset);
}

/*
*Function:write register
*Parameter:struct uart_ingenic_port *up, int offset,int value:write value
*Return:void
*/
static inline void serial_out(struct uart_ingenic_port *up, int offset, int value)
{
	offset <<= 2;
	writel(value, up->port.membase + offset);
}

/*
*Function: Enable Modem status interrupt
*Parameter:struct uart_ingenic_port
*Return:void
*/
static void serial_ingenic_enable_ms(struct uart_port *port)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;

	up->ier |= UART_IER_MSI;// Enable Modem status interrupt
	serial_out(up, UART_IER, up->ier);
}

/*
*Function:stop transmitting
*Parameter:struct uart_ingenic_port
*Return:void
*/
static void serial_ingenic_stop_tx(struct uart_port *port)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;

	if (up->ier & UART_IER_THRI) {
		up->ier &= ~UART_IER_THRI;// Disable the transmit data request interrupt
		serial_out(up, UART_IER, up->ier);
	}
}

/*
*Function:stop receiving
*Parameter:struct uart_ingenic_port *up
*Return:void
*/
static void serial_ingenic_stop_rx(struct uart_port *port)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;

	up->ier &= ~UART_IER_RLSI;
	up->port.read_status_mask &= ~UART_LSR_DR;
	serial_out(up, UART_IER, up->ier);
}

/*
*Function:receive char
*Parameter:unsigned long data,unsigned int status
*Return:void
*/
static inline void receive_chars(unsigned long data)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)data;
	struct tty_struct *tty = up->port.state->port.tty;
	unsigned int ch, flag;
	int max_count = 256;
	unsigned int status= serial_in(up, UART_LSR);
	while ((status & UART_LSR_DR) && (max_count-- > 0))
		//ready to receive data and max_count>0
	{
		ch = serial_in(up, UART_RX);//read RX_Register
		flag = TTY_NORMAL;// TTY_NORMAL=0
		up->port.icount.rx++;

#ifdef CONFIG_SERIAL_INGENIC_MAGIC_SYSRQ
		/* Test SYSRQ */
		if((status == 0xe1) && ch == 0x00) {
			if (uart_handle_break(&up->port))
				goto ignore_char;
		}
#endif
		/*  Break interrupt error | prrity error | Frame error | overun error */
		if (unlikely(status & (UART_LSR_BI | UART_LSR_PE |
						UART_LSR_FE | UART_LSR_OE))) {
			if (status & UART_LSR_BI) {
				status &= ~(UART_LSR_FE | UART_LSR_PE);
				up->port.icount.brk++;
				/*
				 * We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
				if (uart_handle_break(&up->port))
					goto ignore_char;
			} else if (status & UART_LSR_PE)
				up->port.icount.parity++;
			else if (status & UART_LSR_FE)
				up->port.icount.frame++;
			if (status & UART_LSR_OE)
				up->port.icount.overrun++;
			/*
			 * Mask off conditions which should be ignored.
			 */
			status &= up->port.read_status_mask;

#ifdef CONFIG_SERIAL_INGENIC_CONSOLE
			if (up->port.line == up->port.cons->index) {
				/* Recover the break flag from console xmit */
				status |= up->lsr_break_flag;
				up->lsr_break_flag = 0;
			}
#endif
#ifdef CONFIG_SERIAL_INGENIC_MAGIC_SYSRQ
			if (status == 0xe1) {
				printk("handling break ....!\n");
#else
				if (status & UART_LSR_BI) {
#endif
				flag = TTY_BREAK;
			} else if (status & UART_LSR_PE)
				flag = TTY_PARITY;
			else if (status & UART_LSR_FE)
				flag = TTY_FRAME;
		}
#ifdef CONFIG_SERIAL_INGENIC_MAGIC_SYSRQ
		if (uart_handle_sysrq_char(&up->port, ch))
			goto ignore_char;
#endif
		uart_insert_char(&up->port, status, UART_LSR_OE, ch, flag);

ignore_char:
		status = serial_in(up, UART_LSR);
	}
	tty_flip_buffer_push(tty->port);
}
/* transmit one char*/
static void transmit_chars(struct uart_ingenic_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	int count;
	if (up->port.x_char) {
		serial_out(up, UART_TX, up->port.x_char);//transmit char=port.x_char
		up->port.icount.tx++;
		up->port.x_char = 0;//transmit finish x_char=0
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port)) {//xmit is empty or stop tx
		serial_ingenic_stop_tx(&up->port);
		return;
	}

	/* try to tx char until xmit is empty or count=0*/
	count = up->port.fifosize / 2;
	do {
		serial_out(up, UART_TX, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	/*if circ_chars is less than WARKUP_CHARS,then warkup 	*/
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)//get the renainder of circ_chars
		uart_write_wakeup(&up->port);

	if (uart_circ_empty(xmit))
		serial_ingenic_stop_tx(&up->port);
}

static void serial_ingenic_start_tx(struct uart_port *port)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;

	if (!(up->ier & UART_IER_THRI)) {
		up->ier |= UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
}

static inline void check_modem_status(struct uart_ingenic_port *up)
{
	int status;
	status = serial_in(up, UART_MSR);

	if ((status & UART_MSR_ANY_DELTA) == 0)
		return;

	if (status & UART_MSR_DCTS)
		uart_handle_cts_change(&up->port, status & UART_MSR_CTS);

	wake_up_interruptible(&up->port.state->port.delta_msr_wait);
}
/*
 * This handles the interrupt from one port.
 */
static inline irqreturn_t serial_ingenic_irq(int irq, void *dev_id)
{
	struct uart_ingenic_port *up = dev_id;
	unsigned int iir, lsr;
	iir = serial_in(up, UART_IIR);
	lsr = serial_in(up, UART_LSR);
	if (iir & UART_IIR_NO_INT)
		return IRQ_NONE;

	if (lsr & UART_LSR_DR)
		receive_chars((unsigned long)up);
	check_modem_status(up);
	if (lsr & UART_LSR_THRE)
		transmit_chars(up);
	return IRQ_HANDLED;
}

static unsigned int serial_ingenic_tx_empty(struct uart_port *port)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&up->port.lock, flags);
	ret = serial_in(up, UART_LSR) & UART_LSR_TEMT ? TIOCSER_TEMT : 0;
	spin_unlock_irqrestore(&up->port.lock, flags);

	return ret;
}

/*get modem control*/
static unsigned int serial_ingenic_get_mctrl(struct uart_port *port)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;
	unsigned char status;
	unsigned int ret;

	status = serial_in(up, UART_MSR);

	ret = 0;
	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;
	return ret;
}

static void serial_ingenic_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;
	unsigned char mcr = 0;
	int is_auto_rts = up->mcr & UART_MCR_FCM;
	int is_mctrl_enabled = up->mcr & UART_MCR_MDCE;

	if (!is_mctrl_enabled) {
		mcr = up->mcr;
		up->old_mcr = mcr;

		serial_out(up, UART_MCR, mcr);
		return;
	}

	mcr |= UART_MCR_MDCE;

	/*
	 * If bit ITIOCM_RTS is cleared, force RTS to high whether auto RTS
	 * is enabled or not, which is: in UMCR, MDCE bit set to 1, bit FCM
	 * set to 0, and bit RTS set to 0.
	 */
	if (mctrl & TIOCM_RTS) {
		if (is_auto_rts)
			mcr |= UART_MCR_FCM;
		else
			mcr |= UART_MCR_RTS;
	}
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	if (mcr == up->old_mcr)
		return;

	up->old_mcr = mcr;
	serial_out(up, UART_MCR, mcr);
}

static void serial_ingenic_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	if (break_state == -1)
		up->lcr |= UART_LCR_SBC;
	else
		up->lcr &= ~UART_LCR_SBC;
	serial_out(up, UART_LCR, up->lcr);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int serial_ingenic_startup(struct uart_port *port)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;
	unsigned long flags;
	int retval;

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(port->irq, serial_ingenic_irq, 0, up->name, up);
	if (retval)
		return retval;

	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reenabled in set_termios())
	 */
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO);
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO
		   | UART_FCR_CLEAR_RCVR
		   | UART_FCR_CLEAR_XMIT
		   | UART_FCR_UME);
	serial_out(up, UART_FCR, 0);

	/*
	 * Clear the interrupt registers.
	 */
	(void) serial_in(up, UART_LSR);
	(void) serial_in(up, UART_RX);
	(void) serial_in(up, UART_IIR);
	(void) serial_in(up, UART_MSR);

	/*
	 * Now, initialize the UART
	 */
	serial_out(up, UART_LCR, UART_LCR_WLEN8);
	serial_out(up, UART_ISR, 0);

	spin_lock_irqsave(&up->port.lock, flags);
	spin_unlock_irqrestore(&up->port.lock, flags);

	/*
	 * Finally, enable interrupts.  Note: Modem status interrupts
	 * are set via set_termos(), which will be occurring imminently
	 * anyway, so we don't enable them here.
	 */
	up->ier = UART_IER_RLSI | UART_IER_RDI | UART_IER_RTOIE;
	serial_out(up, UART_IER, up->ier);

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void) serial_in(up, UART_LSR);
	(void) serial_in(up, UART_RX);
	(void) serial_in(up, UART_IIR);
	(void) serial_in(up, UART_MSR);

	return 0;
}

static void serial_ingenic_shutdown(struct uart_port *port)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;
	unsigned long flags;
	int tries;

	/*
	 * Disable interrupts from this port
	 */
	spin_lock_irqsave(&port->lock, flags);
	up->ier = 0;
	serial_out(up, UART_IER, 0);

	spin_unlock_irqrestore(&port->lock, flags);

	/*
	 * Wait the RX FIFO to be read.
	 */
	for (tries = 3; (serial_in(up, UART_RCR) != 0) && tries; tries--) {
		printk(KERN_WARNING "%s: waiting RX FIFO to be read\n", up->name);
		msleep(10);
	}
	if (!tries)
		printk(KERN_ERR "%s: RX FIFO hasn't been read before clearing\n",
				up->name);

	/*
	 * Disable break condition and FIFOs
	 */
	serial_out(up, UART_LCR, serial_in(up, UART_LCR) & ~UART_LCR_SBC);
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO
		   | UART_FCR_CLEAR_RCVR
		   | UART_FCR_CLEAR_XMIT
		   | UART_FCR_UME);

	free_irq(port->irq, up);
}

static void init_hw_stopped_status(struct uart_port *uport)
{
	struct tty_port tport = uport->state->port;
	struct tty_struct *tty = tport.tty;

	if (tport.flags & ASYNC_CTS_FLOW) {
		unsigned int mctrl;
		spin_lock_irq(&uport->lock);
		if (!((mctrl = uport->ops->get_mctrl(uport)) & TIOCM_CTS))
			tty->hw_stopped = 1;
		else
			tty->hw_stopped = 0;
		spin_unlock_irq(&uport->lock);
	}
}

static void serial_ingenic_set_termios(struct uart_port *port, struct ktermios *termios,struct ktermios *old)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;
	unsigned char cval=0;
	unsigned long flags;
	unsigned int baud;
	unsigned short *quot1;

	switch (termios->c_cflag & CSIZE) {
		case CS5:
			cval = UART_LCR_WLEN5;
			break;
		case CS6:
			cval = UART_LCR_WLEN6;
			break;
		case CS7:
			cval = UART_LCR_WLEN7;
			break;
		default:
		case CS8:
			cval = UART_LCR_WLEN8;
			break;
	}

	if (termios->c_cflag & CSTOPB){
		cval |= UART_LCR_STOP;
	  }
	if (termios->c_cflag & PARENB){
		cval |= UART_LCR_PARITY;
	  }
	if (!(termios->c_cflag & PARODD)){
		cval |= UART_LCR_EPAR;
	  }
	serial_out(up, UART_LCR, cval);//write cval to UART_LCR
	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk);//get BaudRate
	quot1 = serial47xx_get_divisor(port, baud);
	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);
	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);
	up->port.read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (termios->c_iflag & INPCK)
		up->port.read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= UART_LSR_BI;

	/*
	 * Characters to ignore
	 */
	up->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		up->port.ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		up->port.ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			up->port.ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		up->port.ignore_status_mask |= UART_LSR_DR;

	/*
	 * CTS flow control flag and modem status interrupts
	 */
	up->ier &= ~UART_IER_MSI;
	/*
	 *enable modem status interrupts and enable modem function and control by hardware
	 */
	if (UART_ENABLE_MS(&up->port, termios->c_cflag)) {
		up->ier |= UART_IER_MSI;
		serial_out(up, UART_IER, up->ier);
		up->mcr |= UART_MCR_MDCE | UART_MCR_FCM;
	}
	/*
	 *disable modem status interrupts and clear MCR bits of modem function and control by hardware
	 */
	else {
		up->ier &= ~UART_IER_MSI;
		serial_out(up, UART_IER, up->ier);
		up->mcr &= ~(UART_MCR_MDCE | UART_MCR_FCM);
	}

	serial_dl_write(port, quot1[0]);
	serial_out(up,UART_UMR, quot1[1]);//UART send or receive one bit takes quot1[1] cycles
	serial_out(up,UART_UACR, quot1[2]);

	up->lcr = cval;					/* Save LCR */
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO | UART_FCR_R_TRIG_11 | UART_FCR_UME);

	serial_ingenic_set_mctrl(port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);

	init_hw_stopped_status(port);
}

static inline void serial_dl_write(struct uart_port *up, int value)
{
	struct uart_ingenic_port *port = (struct uart_ingenic_port*)up;
	int lcr = serial_in(port,UART_LCR);
	serial_out(port,UART_LCR, UART_LCR_DLAB);
	serial_out(port,UART_DLL, value & 0xff);
	serial_out(port,UART_DLM, (value >> 8 )& 0xff);
	serial_out(port,UART_LCR, lcr);
}


static int uart_setting_baud(const void *key,const void *elt)
{
	unsigned long *d = (unsigned long*)key;
	struct baudtoregs_t *b = (struct baudtoregs_t *)elt;
	if(*d > b->baud)
		return 1;
	else if(*d < b->baud)
		return -1;
	return 0;
}

static struct baudtoregs_t *search_divisor(unsigned int baud)
{
	struct baudtoregs_t *b = NULL;

	if(baud <= baudtoregs[ARRAY_SIZE(baudtoregs) - 1].baud)
		b = (struct baudtoregs_t *)bsearch((const void*)&baud,(const void*)baudtoregs,ARRAY_SIZE(baudtoregs),
						       sizeof(struct baudtoregs_t),uart_setting_baud);

	return b;
}

static unsigned short *get_divisor(struct uart_port *port, unsigned int baud)
{
	int err, sum, i, j;
	int a[12], b[12];
	unsigned short div, umr, uacr;
	unsigned short umr_best, div_best, uacr_best;
	unsigned long long t0, t1, t2, t3;

	sum = 0;
	umr_best = div_best = uacr_best = 0;
	div = 1;

	if ((port->uartclk % (16 * baud)) == 0) {
		quot1[0] = port->uartclk / (16 * baud);
		quot1[1] = 16;
		quot1[2] = 0;
		return quot1;
	}

	while (1) {
		umr = port->uartclk / (baud * div);
		if (umr > 32) {
			div++;
			continue;
		}
		if (umr < 4) {
			break;
		}
		for (i = 0; i < 12; i++) {
			a[i] = umr;
			b[i] = 0;
			sum = 0;
			for (j = 0; j <= i; j++) {
				sum += a[j];
			}

			/* the precision could be 1/2^(36) due to the value of t0 */
			t0 = 0x1000000000LL;
			t1 = (i + 1) * t0;
			t2 = (sum * div) * t0;
			t3 = div * t0;
			do_div(t1, baud);
			do_div(t2, port->uartclk);
			do_div(t3, (2 * port->uartclk));
			err = t1 - t2 - t3;

			if (err > 0) {
				a[i] += 1;
				b[i] = 1;
			}
		}

		uacr = 0;
		for (i = 0; i < 12; i++) {
			if (b[i] == 1) {
				uacr |= 1 << i;
			}
		}
		if (div_best ==0){
			div_best = div;
			umr_best = umr;
			uacr_best = uacr;
		}

		/* the best value of umr should be near 16, and the value of uacr should better be smaller */
		if (abs(umr - 16) < abs(umr_best - 16) || (abs(umr - 16) == abs(umr_best - 16) && uacr_best > uacr))
		{
			div_best = div;
			umr_best = umr;
			uacr_best = uacr;
		}
		div++;
	}

	quot1[0] = div_best;
	quot1[1] = umr_best;
	quot1[2] = uacr_best;

	return quot1;
}
static unsigned short *serial47xx_get_divisor(struct uart_port *port, unsigned int baud)
{
	struct baudtoregs_t *bt;

	bt = search_divisor(baud);
	if(bt) {
		quot1[0] = bt->div;
		quot1[1] = bt->umr;
		quot1[2] = bt->uacr;
		return quot1;
	}

	return get_divisor(port, baud);
}

static void serial_ingenic_release_port(struct uart_port *port)
{
}

static int serial_ingenic_request_port(struct uart_port *port)
{
	return 0;
}

static void serial_ingenic_config_port(struct uart_port *port, int flags)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;
	up->port.type = PORT_8250;
}

static int serial_ingenic_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* we don't want the core code to modify any port params */
	return -EINVAL;
}

static const char *serial_ingenic_type(struct uart_port *port)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;
	return up->name;
}
static struct uart_ingenic_port *serial_ingenic_ports[PORT_NR];
static struct uart_driver serial_ingenic_reg;

#ifdef CONFIG_SERIAL_INGENIC_CONSOLE

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

/*
 *	Wait for transmitter & holding register to empty
 */
static inline void wait_for_xmitr(struct uart_ingenic_port *up)
{
	unsigned int status, tmout = 10000;

	/* Wait up to 10ms for the character(s) to be sent. */
	do {
		status = serial_in(up, UART_LSR);

		if (status & UART_LSR_BI)
			up->lsr_break_flag = UART_LSR_BI;

		if (--tmout == 0)
			break;
		udelay(1);
	} while ((status & BOTH_EMPTY) != BOTH_EMPTY);

	/* Wait up to 1s for flow control if necessary */
	if (up->port.flags & UPF_CONS_FLOW) {
		tmout = 1000000;
		while (--tmout &&
				((serial_in(up, UART_MSR) & UART_MSR_CTS) == 0))//no CTS
			udelay(1);
	}
}

static void serial_ingenic_console_putchar(struct uart_port *port, int ch)
{
	struct uart_ingenic_port *up = (struct uart_ingenic_port *)port;

	wait_for_xmitr(up);
	serial_out(up, UART_TX, ch);
}

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void serial_ingenic_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_ingenic_port *up = serial_ingenic_ports[co->index];
	unsigned int ier;

	/*
	 *	First save the IER then disable the interrupts
	 */
	spin_lock(&up->port.lock);
	ier = up->ier;
	up->ier &= ~UART_IER_THRI;
	serial_out(up, UART_IER, up->ier);

	uart_console_write(&up->port, s, count, serial_ingenic_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(up);
	up->ier = ier;
	serial_out(up, UART_IER, ier);
	spin_unlock(&up->port.lock);
}

static int __init serial_ingenic_console_setup(struct console *co, char *options)
{
	struct uart_ingenic_port *up;
	int baud = 57600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index == -1 || co->index >= serial_ingenic_reg.nr)
		co->index = 0;
	up = serial_ingenic_ports[co->index];
	if (!up)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&up->port, co, baud, parity, bits, flow);
}

static struct console serial_ingenic_console = {
	.name		= "ttyS",
	.write		= serial_ingenic_console_write,
	.device		= uart_console_device,
	.setup		= serial_ingenic_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &serial_ingenic_reg,
};

#define INGENIC_CONSOLE	&serial_ingenic_console
#else
#define INGENIC_CONSOLE	NULL
#endif

struct uart_ops serial_ingenic_pops = {
	.tx_empty	= serial_ingenic_tx_empty,//TX Buffer empty
	.set_mctrl	= serial_ingenic_set_mctrl,//set Modem control
	.get_mctrl	= serial_ingenic_get_mctrl,//get Modem control
	.stop_tx	= serial_ingenic_stop_tx,//stop TX
	.start_tx	= serial_ingenic_start_tx,//start tx
	.stop_rx	= serial_ingenic_stop_rx,//stop rx
	.enable_ms	= serial_ingenic_enable_ms,//modem status enable
	.break_ctl	= serial_ingenic_break_ctl,//
	.startup	= serial_ingenic_startup,//start endport
	.shutdown	= serial_ingenic_shutdown,//shutdown endport
	.set_termios	= serial_ingenic_set_termios,//change para of endport
	.type		= serial_ingenic_type,//
	.release_port	= serial_ingenic_release_port,//release port I/O
	.request_port	= serial_ingenic_request_port,//
	.config_port	= serial_ingenic_config_port,//
	.verify_port	= serial_ingenic_verify_port,//
};

static struct uart_driver serial_ingenic_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= "INGENIC serial",
	.dev_name	= "ttyS",
	.major		= TTY_MAJOR,
	.minor		= 64,
	.nr		= PORT_NR,
	.cons		= INGENIC_CONSOLE,
};

#ifdef CONFIG_PM
static int serial_ingenic_suspend(struct device *dev)
{
	struct uart_ingenic_port *up = dev_get_drvdata(dev);

	if (up)
		uart_suspend_port(&serial_ingenic_reg, &up->port);

	return 0;
}

static int serial_ingenic_resume(struct device *dev)
{
	struct uart_ingenic_port *up = dev_get_drvdata(dev);

	if (up)
		uart_resume_port(&serial_ingenic_reg, &up->port);

	return 0;
}

static const struct dev_pm_ops serial_ingenic_pm_ops = {
	.suspend	= serial_ingenic_suspend,
	.resume		= serial_ingenic_resume,
};
#endif

#ifdef CONFIG_DEBUG_FS


static ssize_t port_show_regs(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct uart_ingenic_port *up = file->private_data;
	char *buf;
	int len = 0;
	unsigned int lcr;
	unsigned long flags;
	ssize_t ret;

#define REGS_BUFSIZE	1024
	buf = kzalloc(REGS_BUFSIZE, GFP_KERNEL);
	if (!buf)
		return 0;

	spin_lock_irqsave(&up->port.lock, flags);

	lcr = serial_in(up, UART_LCR);
	serial_out(up, UART_LCR, lcr | UART_LCR_DLAB);
	len += snprintf(buf + len, REGS_BUFSIZE - len, "%s:\n", up->name);
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UDLLR: 0x%02x\n", serial_in(up, UART_DLL));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UDLHR: 0x%02x\n", serial_in(up, UART_DLM));
	serial_out(up, UART_LCR, lcr & (~UART_LCR_DLAB));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UIER: 0x%02x\n", serial_in(up, UART_IER));
	serial_out(up, UART_LCR, lcr);
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UIIR: 0x%02x\n", serial_in(up, UART_IIR));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UFCR: 0x%02x\n", serial_in(up, UART_FCR));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "ULCR: 0x%02x\n", lcr);
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UMCR: 0x%02x\n", serial_in(up, UART_MCR));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "ULSR: 0x%02x\n", serial_in(up, UART_LSR));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UMSR: 0x%02x\n", serial_in(up, UART_MSR));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "USPR: 0x%02x\n", serial_in(up, UART_SCR));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UISR: 0x%02x\n", serial_in(up, UART_ISR));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UMR: 0x%02x\n", serial_in(up, UART_UMR));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UACR: 0x%03x\n", serial_in(up, UART_UACR));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "URCR: 0x%03x\n", serial_in(up, UART_RCR));
	len += snprintf(buf + len, REGS_BUFSIZE - len, "UTCR: 0x%03x\n", serial_in(up, UART_TCR));

	spin_unlock_irqrestore(&up->port.lock, flags);

	ret =  simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
#undef REGS_BUFSIZE
	return ret;
}

static const struct file_operations port_regs_ops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= port_show_regs,
	.llseek		= default_llseek,
};
#endif

static const struct of_device_id serial_ingenic_of_match[] = {
	{.compatible = "ingenic,8250-uart", .data = NULL},
};

MODULE_DEVICE_TABLE(of, serial_ingenic_of_match);

static int serial_ingenic_probe(struct platform_device *pdev)
{
	struct uart_ingenic_port *up;
	struct resource *mmres;
	int irq;
	char clk_name[20];
	unsigned long uartclk;

	mmres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!mmres || irq < 0)
		return -ENODEV;

	up = devm_kzalloc(&pdev->dev, sizeof(struct uart_ingenic_port), GFP_KERNEL);
	if (!up)
		return -ENOMEM;

	pdev->id = of_alias_get_id(pdev->dev.of_node, "uart");

	sprintf(up->name,"uart%d",pdev->id);

	sprintf(clk_name, "gate_uart%d",pdev->id);
	up->clk = devm_clk_get(&pdev->dev, clk_name);
	if (IS_ERR_OR_NULL(up->clk)) {
		printk(KERN_WARNING "%s: Failed to get uart clk. Using Default clk rate(24MHz)\n", up->name);
		uartclk = 24000000;
		up->clk = NULL;
	} else {
		uartclk = clk_get_rate(up->clk);
	}

	up->port.type = PORT_8250;
	up->port.iotype = UPIO_MEM;
	up->port.mapbase = mmres->start;
	up->port.mapsize = resource_size(mmres);
	up->port.irq = irq;
	up->port.fifosize = 64;
	up->port.ops = &serial_ingenic_pops;
	up->port.line = pdev->id;
	up->port.dev = &pdev->dev;
	up->port.flags = UPF_IOREMAP | UPF_BOOT_AUTOCONF;
	up->port.uartclk = uartclk;
	up->port.membase = devm_ioremap_resource(&pdev->dev, mmres);
	if (!up->port.membase)
		return -ENOMEM;

	if(up->clk)
		clk_prepare_enable(up->clk);

	serial_ingenic_ports[pdev->id] = up;
	uart_add_one_port(&serial_ingenic_reg, &up->port);

	platform_set_drvdata(pdev, up);

#ifdef CONFIG_DEBUG_FS
	{
		char name[20];
		snprintf(name, sizeof(name), "uart%d_regs", up->port.line);
		up->debugfs = debugfs_create_file(name, S_IFREG | S_IRUGO,
					NULL, up, &port_regs_ops);
	}
#endif
	return 0;
}

static int serial_ingenic_remove(struct platform_device *pdev)
{
	struct uart_ingenic_port *up = platform_get_drvdata(pdev);

#ifdef CONFIG_DEBUG_FS
	if (up->debugfs)
		debugfs_remove(up->debugfs);
#endif
	platform_set_drvdata(pdev, NULL);

	uart_remove_one_port(&serial_ingenic_reg, &up->port);

	if(up->clk)
		clk_disable_unprepare(up->clk);

	return 0;
}

static struct platform_driver serial_ingenic_driver = {
	.probe          = serial_ingenic_probe,
	.remove         = serial_ingenic_remove,

	.driver		= {
		.name	= "ingenic-uart",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(serial_ingenic_of_match),
#ifdef CONFIG_PM
		.pm	= &serial_ingenic_pm_ops,
#endif
	},
};

int __init serial_ingenic_init(void)
{
	int ret;
	ret = uart_register_driver(&serial_ingenic_reg);
	if (ret != 0)
		return ret;

	ret = platform_driver_register(&serial_ingenic_driver);
	if (ret != 0)
		uart_unregister_driver(&serial_ingenic_reg);

	return ret;
}

void __exit serial_ingenic_exit(void)
{
	platform_driver_unregister(&serial_ingenic_driver);
	uart_unregister_driver(&serial_ingenic_reg);
}

#ifdef CONFIG_EARLY_INIT_RUN
rootfs_initcall(serial_ingenic_init);

#else
module_init(serial_ingenic_init);

#endif

module_exit(serial_ingenic_exit);

MODULE_DESCRIPTION("Ingenic Compatible UART driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ingenic-uart");
