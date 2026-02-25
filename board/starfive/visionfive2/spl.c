// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 * Author: Yanhong Wang<yanhong.wang@starfivetech.com>
 */

#include <asm/arch/eeprom.h>
#include <asm/arch/gpio.h>
#include <asm/arch/regs.h>
#include <asm/arch/spl.h>
#include <asm/io.h>
#include <dt-bindings/clock/starfive,jh7110-crg.h>
#include <fdt_support.h>
#include <linux/libfdt.h>
#include <log.h>
#include <spl.h>

DECLARE_GLOBAL_DATA_PTR;
#define JH7110_CLK_CPU_ROOT_OFFSET		0x0U
#define JH7110_CLK_CPU_ROOT_SHIFT		24
#define JH7110_CLK_CPU_ROOT_MASK		GENMASK(29, 24)

void spl_perform_board_fixups(struct spl_image_info *spl_image)
{
	/* Update the memory size which read from eeprom or DT */
	if (spl_image->fdt_addr)
		fdt_fixup_memory(spl_image->fdt_addr, 0x40000000, gd->ram_size);
}

static void jh7110_jtag_init(void)
{
	/* nTRST: GPIO36 */
	SYS_IOMUX_DOEN(36, HIGH);
	SYS_IOMUX_DIN(36, 4);
	/* TDI: GPIO61 */
	SYS_IOMUX_DOEN(61, HIGH);
	SYS_IOMUX_DIN(61, 19);
	/* TMS: GPIO63 */
	SYS_IOMUX_DOEN(63, HIGH);
	SYS_IOMUX_DIN(63, 20);
	/* TCK: GPIO60 */
	SYS_IOMUX_DOEN(60, HIGH);
	SYS_IOMUX_DIN(60, 29);
	/* TDO: GPIO44 */
	SYS_IOMUX_DOEN(44, 8);
	SYS_IOMUX_DOUT(44, 22);
}

int spl_board_init_f(void)
{
	int ret;

	jh7110_jtag_init();

	ret = spl_dram_init();
	if (ret) {
		debug("JH7110 DRAM init failed: %d\n", ret);
		return ret;
	}

	return 0;
}

u32 spl_boot_device(void)
{
	u32 mode;

	mode = in_le32(JH7110_BOOT_MODE_SELECT_REG)
				& JH7110_BOOT_MODE_SELECT_MASK;
	switch (mode) {
	case 0:
		return BOOT_DEVICE_SPI;

	case 1:
		return BOOT_DEVICE_MMC2;

	case 2:
		return BOOT_DEVICE_MMC1;

	case 3:
		return BOOT_DEVICE_UART;

	default:
		debug("Unsupported boot device 0x%x.\n", mode);
		return BOOT_DEVICE_NONE;
	}
}

#define SYS_CRG_BASE		0x13020000
#define CLK_UART1_APB_OFFSET	0x24c
#define CLK_UART1_CORE_OFFSET	0x250
#define CLK_RSTN_3_OFFSET	0x300

/* UART1 registers - reg-shift=2 means 4-byte spacing */
#define UART1_BASE		0x10010000
#define UART_RBR		(0x00 << 2)	/* Receive Buffer Register */
#define UART_THR		(0x00 << 2)	/* Transmit Holding Register */
#define UART_IER		(0x01 << 2)	/* Interrupt Enable Register */
#define UART_FCR		(0x02 << 2)	/* FIFO Control Register */
#define UART_LCR		(0x03 << 2)	/* Line Control Register */
#define UART_MCR		(0x04 << 2)	/* Modem Control Register */
#define UART_LSR		(0x05 << 2)	/* Line Status Register */
#define UART_DLL		(0x00 << 2)	/* Divisor Latch Low */
#define UART_DLM		(0x01 << 2)	/* Divisor Latch High */

#define UART_LCR_DLAB		0x80	/* Divisor Latch Access Bit */
#define UART_LCR_8N1		0x03	/* 8 data, 1 stop, no parity */
#define UART_LSR_TEMT		0x40	/* Transmitter empty */
#define UART_LSR_THRE		0x20	/* Transmit-hold-register empty */
#define UART_FCR_ENABLE		0x01	/* Enable FIFO */

/* Oscillator frequency for UART */
#define UART_CLOCK		24000000	/* 24 MHz oscillator */
#define UART_BAUDRATE		115200

static void uart1_early_puts(const char *s)
{
	volatile u8 *uart = (volatile u8 *)UART1_BASE;

	/* Wait for transmitter to be ready before sending */
	while (!(readb(uart + UART_LSR) & UART_LSR_THRE))
		;

	while (*s) {
		/* Wait for transmit holding register to be empty */
		while (!(readb(uart + UART_LSR) & UART_LSR_THRE))
			;

		/* Write character */
		writeb(*s++, uart + UART_THR);
	}

	/* Wait for transmitter to be completely empty before returning */
	while (!(readb(uart + UART_LSR) & UART_LSR_TEMT))
		;
}

static void uart1_hw_init(void)
{
	volatile u8 *uart = (volatile u8 *)UART1_BASE;
	u32 div;
	volatile int i;

	/* Calculate divisor for 115200 baud */
	div = UART_CLOCK / (16 * UART_BAUDRATE);

	/* Disable interrupts first */
	writeb(0x00, uart + UART_IER);

	/* Set DLAB to access divisor latches */
	writeb(UART_LCR_DLAB, uart + UART_LCR);

	/* Set divisor */
	writeb(div & 0xff, uart + UART_DLL);
	writeb((div >> 8) & 0xff, uart + UART_DLM);

	/* Clear DLAB and set 8N1 mode */
	writeb(UART_LCR_8N1, uart + UART_LCR);

	/* Enable FIFO and reset TX/RX FIFO */
	writeb(UART_FCR_ENABLE | 0x06, uart + UART_FCR);

	/* Set Modem Control Register */
	writeb(0x00, uart + UART_MCR);

	/* Small delay to let UART stabilize */
	for (i = 0; i < 1000; i++)
		__asm__ volatile ("nop");
}

void uart1_init(void)
{
	/* Enable UART1 clock */
	setbits_le32(SYS_CRG_BASE + CLK_UART1_APB_OFFSET, BIT(31));
	setbits_le32(SYS_CRG_BASE + CLK_UART1_CORE_OFFSET, BIT(31));
	clrsetbits_le32(SYS_CRG_BASE + CLK_RSTN_3_OFFSET, BIT(21) | BIT(22), 0);

	/*uart1 tx*/
	SYS_IOMUX_DOEN(43, LOW);
	SYS_IOMUX_DOUT(43, 0x44);
	SYS_IOMUX_SET_DS(43, 3);
	/*uart1 rx*/
	SYS_IOMUX_DOEN(42, HIGH);
	SYS_IOMUX_DIN(42, 55);

	/* Initialize UART hardware */
	uart1_hw_init();
}

static void uart1_test(void)
{
	uart1_early_puts("\r\n");
	uart1_early_puts("UART1 SPL Test Start\r\n");
	uart1_early_puts("Test Complete\r\n");
}

void board_init_f(ulong dummy)
{
	uart1_init();

	/* Test UART1 before DM initialization */
	uart1_test();

	int ret;

	ret = spl_early_init();
	if (ret)
		panic("spl_early_init() failed: %d\n", ret);

	riscv_cpu_setup();
	preloader_console_init();

	/* Set the parent clock of cpu_root clock to pll0,
	 * it must be initialized here
	 */
	clrsetbits_le32(JH7110_SYS_CRG + JH7110_CLK_CPU_ROOT_OFFSET,
			JH7110_CLK_CPU_ROOT_MASK,
			BIT(JH7110_CLK_CPU_ROOT_SHIFT));

	/* Set USB overcurrent overflow pin disable */
	SYS_IOMUX_DIN_DISABLED(2);

	ret = spl_board_init_f();
	if (ret) {
		debug("spl_board_init_f init failed: %d\n", ret);
		return;
	}
}

#if CONFIG_IS_ENABLED(LOAD_FIT)
int board_fit_config_name_match(const char *name)
{
	if (!strcmp(name, "starfive/jh7110-deepcomputing-fml13v01") &&
		    !strncmp(get_product_id_from_eeprom(), "FML13V01", 8)) {
		return 0;
	} else if (!strcmp(name, "starfive/jh7110-milkv-mars") &&
		    !strncmp(get_product_id_from_eeprom(), "MARS", 4)) {
		return 0;
	} else if (!strcmp(name, "starfive/jh7110-milkv-marscm-emmc") &&
		    !strncmp(get_product_id_from_eeprom(), "MARC", 4) &&
		    get_mmc_size_from_eeprom()) {
		return 0;
	} else if (!strcmp(name, "starfive/jh7110-milkv-marscm-lite") &&
		    !strncmp(get_product_id_from_eeprom(), "MARC", 4) &&
		    !get_mmc_size_from_eeprom()) {
		return 0;
	} else if (!strcmp(name, "starfive/jh7110-pine64-star64") &&
		    !strncmp(get_product_id_from_eeprom(), "STAR64", 6)) {
		return 0;
	} else if (!strcmp(name, "starfive/jh7110-starfive-visionfive-2-v1.2a") &&
		    !strncmp(get_product_id_from_eeprom(), "VF7110A", 7)) {
		return 0;
	} else if (!strcmp(name, "starfive/jh7110-starfive-visionfive-2-v1.3b") &&
		    !strncmp(get_product_id_from_eeprom(), "VF7110B", 7)) {
		return 0;
	}

	return -EINVAL;
}
#endif
