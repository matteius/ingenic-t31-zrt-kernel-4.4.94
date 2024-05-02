#ifndef _EARLY_PRINTK_H
#define _EARLY_PRINTK_H

#include <linux/types.h>
#include <linux/serial_reg.h>
#include <asm/io.h>  // For memory access

// Define base addresses and offsets for UART ports
#define UART0_IOBASE    0x10030000
#define UART1_IOBASE    0x10031000
#define UART2_IOBASE    0x10032000
#define UART_OFF        0x1000

#define OFF_TDR         0x00
#define OFF_LCR         0x0C
#define OFF_LSR         0x14

#define LSR_TDRQ        (1 << 5)
#define LSR_TEMT        (1 << 6)

// Define the default UART base
static volatile u8 *uart_base = (volatile u8 *) CKSEG1ADDR(UART0_IOBASE);

// Typedef for the putchar function pointer
typedef void (*putchar_f_t)(char);

// Inline function to perform UART initialization check
static inline void check_uart(void) {
    int i = 0;
    for(i = 0; i < 10; i++) {
        if (readb(uart_base + OFF_LCR))
            break;
        uart_base += UART_OFF;
    }
    if (i == 10) {
        uart_base = NULL; // No UART found
    }
}

// Inline function to output a character
static inline void putchar(char ch) {
    if (!uart_base) return;

    // Wait for FIFO to be ready to accept data
    while (!(readb(uart_base + OFF_LSR) & (LSR_TDRQ | LSR_TEMT)))
        ;
    writeb(ch, uart_base + OFF_TDR);
}

// Function to print a string very early in boot
static inline void very_early_printk(char *str) {
    check_uart();  // Ensure UART is checked/set up
    while (*str) {
        putchar(*str++);
    }
}

// Function to print a string with a prefix very early in boot
static inline void super_early_printk(char *str) {
    very_early_printk("** early-debug-info: ");
    very_early_printk(str);
    very_early_printk("\r\n");
}

#endif // _EARLY_PRINTK_H
