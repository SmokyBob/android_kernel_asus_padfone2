/* Copyright (c) 2010-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/syscore_ops.h>
#include <linux/irqdomain.h>
#include <linux/of.h>

#include <asm/mach/irq.h>

#include <mach/msm_iomap.h>
#include <mach/gpiomux.h>
#include <mach/mpm.h>
//++Ledger
#include "linux/console.h"
int gpio_irq_cnt, gpio_resume_irq[]={};
#define NFCIRQ_GPIO     1

/******************************************************************************
 * Debug Definitions
 *****************************************************************************/

enum {
        GPIOMUX_DEBUG_SUSPEND = BIT(0),
};

int gpiomux_debug_mask = 0;
module_param_named(
        debug_mask, gpiomux_debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP
);

static struct gpiomux_setting gsbi[] = {
        /* Active state */
        {
                .func = GPIOMUX_FUNC_1,
                .drv = GPIOMUX_DRV_8MA,
                .pull = GPIOMUX_PULL_NONE,
        },
        {
                .func = GPIOMUX_FUNC_2,
                .drv = GPIOMUX_DRV_8MA,
                .pull = GPIOMUX_PULL_NONE,
        },
        //UART_TXD_DBG
        {
                .func = GPIOMUX_FUNC_GPIO,
                .drv = GPIOMUX_DRV_2MA,
                .pull = GPIOMUX_PULL_NONE,
                .dir = GPIOMUX_OUT_LOW,	
	},
        //UART_RXD_DBG
        {
                .func = GPIOMUX_FUNC_GPIO,
                .drv = GPIOMUX_DRV_2MA,
                .pull = GPIOMUX_PULL_DOWN,
                .dir = GPIOMUX_IN,
        },
};

// static struct gpiomux_setting gen[] = {
//         /* Active state */
//         {
//                 .func = GPIOMUX_FUNC_GPIO,
//                 .drv = GPIOMUX_DRV_2MA,
//                 .pull = GPIOMUX_PULL_NONE,
//                 .dir = GPIOMUX_IN,
//         },
//         /* Suspended state */
//         {
//                 .func = GPIOMUX_FUNC_GPIO,
//                 .drv = GPIOMUX_DRV_2MA,
//                 .pull = GPIOMUX_PULL_DOWN,
//                 .dir = GPIOMUX_IN,
//         },
//         /*gen output low-2*/
//         {
//                 .func = GPIOMUX_FUNC_GPIO,
//                 .drv = GPIOMUX_DRV_2MA,
//                 .pull = GPIOMUX_PULL_NONE,
//                 .dir = GPIOMUX_OUT_LOW, 
//         },
// 
// };

static struct msm_gpiomux_config gpiomux_configs[] = {
        {
                .gpio      = 82,	/* UART_TXD_DBG */
                .settings = {
                        [GPIOMUX_ACTIVE] = &gsbi[1],
                        [GPIOMUX_SUSPENDED] = &gsbi[2],
                },
        },
        {
                .gpio      = 83,        /* UART_RXD_DBG */
                .settings = {
                        [GPIOMUX_ACTIVE] = &gsbi[0],
                        [GPIOMUX_SUSPENDED] = &gsbi[3],
                },
        },
};

enum {
        UART_TXD_DBG  = 0,
        UART_RXD_DBG  = 1,
        REWRITE_NUM = 2,
};
//--Ledger

/* Bits of interest in the GPIO_IN_OUT register.
 */
enum {
	GPIO_IN_BIT  = 0,
	GPIO_OUT_BIT = 1
};

/* Bits of interest in the GPIO_INTR_STATUS register.
 */
enum {
	INTR_STATUS_BIT = 0,
};

/* Bits of interest in the GPIO_CFG register.
 */
enum {
	GPIO_OE_BIT = 9,
};

/* Bits of interest in the GPIO_INTR_CFG register.
 */
enum {
	INTR_ENABLE_BIT        = 0,
	INTR_POL_CTL_BIT       = 1,
	INTR_DECT_CTL_BIT      = 2,
	INTR_RAW_STATUS_EN_BIT = 3,
};

/* Codes of interest in GPIO_INTR_CFG_SU.
 */
enum {
	TARGET_PROC_SCORPION = 4,
	TARGET_PROC_NONE     = 7,
};

/*
 * There is no 'DC_POLARITY_LO' because the GIC is incapable
 * of asserting on falling edge or level-low conditions.  Even though
 * the registers allow for low-polarity inputs, the case can never arise.
 */
enum {
	DC_POLARITY_HI	= BIT(11),
	DC_IRQ_ENABLE	= BIT(3),
};

enum msm_tlmm_register {
	SDC4_HDRV_PULL_CTL = 0x20a0,
	SDC3_HDRV_PULL_CTL = 0x20a4,
	SDC1_HDRV_PULL_CTL = 0x20a0,
};

struct tlmm_field_cfg {
	enum msm_tlmm_register reg;
	u8                     off;
};

static const struct tlmm_field_cfg tlmm_hdrv_cfgs[] = {
	{SDC4_HDRV_PULL_CTL, 6}, /* TLMM_HDRV_SDC4_CLK  */
	{SDC4_HDRV_PULL_CTL, 3}, /* TLMM_HDRV_SDC4_CMD  */
	{SDC4_HDRV_PULL_CTL, 0}, /* TLMM_HDRV_SDC4_DATA */
	{SDC3_HDRV_PULL_CTL, 6}, /* TLMM_HDRV_SDC3_CLK  */
	{SDC3_HDRV_PULL_CTL, 3}, /* TLMM_HDRV_SDC3_CMD  */
	{SDC3_HDRV_PULL_CTL, 0}, /* TLMM_HDRV_SDC3_DATA */
	{SDC1_HDRV_PULL_CTL, 6}, /* TLMM_HDRV_SDC1_CLK  */
	{SDC1_HDRV_PULL_CTL, 3}, /* TLMM_HDRV_SDC1_CMD  */
	{SDC1_HDRV_PULL_CTL, 0}, /* TLMM_HDRV_SDC1_DATA */
};

static const struct tlmm_field_cfg tlmm_pull_cfgs[] = {
	{SDC4_HDRV_PULL_CTL, 11}, /* TLMM_PULL_SDC4_CMD  */
	{SDC4_HDRV_PULL_CTL, 9},  /* TLMM_PULL_SDC4_DATA */
	{SDC3_HDRV_PULL_CTL, 14}, /* TLMM_PULL_SDC3_CLK  */
	{SDC3_HDRV_PULL_CTL, 11}, /* TLMM_PULL_SDC3_CMD  */
	{SDC3_HDRV_PULL_CTL, 9},  /* TLMM_PULL_SDC3_DATA */
	{SDC1_HDRV_PULL_CTL, 13}, /* TLMM_PULL_SDC1_CLK  */
	{SDC1_HDRV_PULL_CTL, 11}, /* TLMM_PULL_SDC1_CMD  */
	{SDC1_HDRV_PULL_CTL, 9},  /* TLMM_PULL_SDC1_DATA */
};

/*
 * Supported arch specific irq extension.
 * Default make them NULL.
 */
struct irq_chip msm_gpio_irq_extn = {
	.irq_eoi	= NULL,
	.irq_mask	= NULL,
	.irq_unmask	= NULL,
	.irq_retrigger	= NULL,
	.irq_set_type	= NULL,
	.irq_set_wake	= NULL,
	.irq_disable	= NULL,
};

/*
 * When a GPIO triggers, two separate decisions are made, controlled
 * by two separate flags.
 *
 * - First, INTR_RAW_STATUS_EN controls whether or not the GPIO_INTR_STATUS
 * register for that GPIO will be updated to reflect the triggering of that
 * gpio.  If this bit is 0, this register will not be updated.
 * - Second, INTR_ENABLE controls whether an interrupt is triggered.
 *
 * If INTR_ENABLE is set and INTR_RAW_STATUS_EN is NOT set, an interrupt
 * can be triggered but the status register will not reflect it.
 */
#define INTR_RAW_STATUS_EN BIT(INTR_RAW_STATUS_EN_BIT)
#define INTR_ENABLE        BIT(INTR_ENABLE_BIT)
#define INTR_DECT_CTL_EDGE BIT(INTR_DECT_CTL_BIT)
#define INTR_POL_CTL_HI    BIT(INTR_POL_CTL_BIT)

#define GPIO_INTR_CFG_SU(gpio)    (MSM_TLMM_BASE + 0x0400 + (0x04 * (gpio)))
#define DIR_CONN_INTR_CFG_SU(irq) (MSM_TLMM_BASE + 0x0700 + (0x04 * (irq)))
#define GPIO_CONFIG(gpio)         (MSM_TLMM_BASE + 0x1000 + (0x10 * (gpio)))
#define GPIO_IN_OUT(gpio)         (MSM_TLMM_BASE + 0x1004 + (0x10 * (gpio)))
#define GPIO_INTR_CFG(gpio)       (MSM_TLMM_BASE + 0x1008 + (0x10 * (gpio)))
#define GPIO_INTR_STATUS(gpio)    (MSM_TLMM_BASE + 0x100c + (0x10 * (gpio)))

/**
 * struct msm_gpio_dev: the MSM8660 SoC GPIO device structure
 *
 * @enabled_irqs: a bitmap used to optimize the summary-irq handler.  By
 * keeping track of which gpios are unmasked as irq sources, we avoid
 * having to do __raw_readl calls on hundreds of iomapped registers each time
 * the summary interrupt fires in order to locate the active interrupts.
 *
 * @wake_irqs: a bitmap for tracking which interrupt lines are enabled
 * as wakeup sources.  When the device is suspended, interrupts which are
 * not wakeup sources are disabled.
 *
 * @dual_edge_irqs: a bitmap used to track which irqs are configured
 * as dual-edge, as this is not supported by the hardware and requires
 * some special handling in the driver.
 */
struct msm_gpio_dev {
	struct gpio_chip gpio_chip;
	DECLARE_BITMAP(enabled_irqs, NR_MSM_GPIOS);
	DECLARE_BITMAP(wake_irqs, NR_MSM_GPIOS);
	DECLARE_BITMAP(dual_edge_irqs, NR_MSM_GPIOS);
	struct irq_domain domain;
};

static DEFINE_SPINLOCK(tlmm_lock);

static inline struct msm_gpio_dev *to_msm_gpio_dev(struct gpio_chip *chip)
{
	return container_of(chip, struct msm_gpio_dev, gpio_chip);
}

static inline void set_gpio_bits(unsigned n, void __iomem *reg)
{
	__raw_writel(__raw_readl(reg) | n, reg);
}

static inline void clr_gpio_bits(unsigned n, void __iomem *reg)
{
	__raw_writel(__raw_readl(reg) & ~n, reg);
}

static int msm_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	int rc;
	rc = __raw_readl(GPIO_IN_OUT(offset)) & BIT(GPIO_IN_BIT);
	mb();
	return rc;
}

static void msm_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	__raw_writel(val ? BIT(GPIO_OUT_BIT) : 0, GPIO_IN_OUT(offset));
	mb();
}

static int msm_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	clr_gpio_bits(BIT(GPIO_OE_BIT), GPIO_CONFIG(offset));
	mb();
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
	return 0;
}

static int msm_gpio_direction_output(struct gpio_chip *chip,
				unsigned offset,
				int val)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	msm_gpio_set(chip, offset, val);
	set_gpio_bits(BIT(GPIO_OE_BIT), GPIO_CONFIG(offset));
	mb();
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
	return 0;
}

#ifdef CONFIG_OF
static int msm_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct msm_gpio_dev *g_dev = to_msm_gpio_dev(chip);
	struct irq_domain *domain = &g_dev->domain;
	return domain->irq_base + (offset - chip->base);
}

static inline int msm_irq_to_gpio(struct gpio_chip *chip, unsigned irq)
{
	struct msm_gpio_dev *g_dev = to_msm_gpio_dev(chip);
	struct irq_domain *domain = &g_dev->domain;
	return irq - domain->irq_base;
}
#else
static int msm_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return MSM_GPIO_TO_INT(offset - chip->base);
}

static inline int msm_irq_to_gpio(struct gpio_chip *chip, unsigned irq)
{
	return irq - MSM_GPIO_TO_INT(chip->base);
}
#endif

static int msm_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return msm_gpiomux_get(chip->base + offset);
}

static void msm_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	msm_gpiomux_put(chip->base + offset);
}

static struct msm_gpio_dev msm_gpio = {
	.gpio_chip = {
		.label		  = "msmgpio",
		.base             = 0,
		.ngpio            = NR_MSM_GPIOS,
		.direction_input  = msm_gpio_direction_input,
		.direction_output = msm_gpio_direction_output,
		.get              = msm_gpio_get,
		.set              = msm_gpio_set,
		.to_irq           = msm_gpio_to_irq,
		.request          = msm_gpio_request,
		.free             = msm_gpio_free,
	},
};

static void switch_mpm_config(struct irq_data *d, unsigned val)
{
	/* switch the configuration in the mpm as well */
	if (!msm_gpio_irq_extn.irq_set_type)
		return;

	if (val)
		msm_gpio_irq_extn.irq_set_type(d, IRQF_TRIGGER_FALLING);
	else
		msm_gpio_irq_extn.irq_set_type(d, IRQF_TRIGGER_RISING);
}

/* For dual-edge interrupts in software, since the hardware has no
 * such support:
 *
 * At appropriate moments, this function may be called to flip the polarity
 * settings of both-edge irq lines to try and catch the next edge.
 *
 * The attempt is considered successful if:
 * - the status bit goes high, indicating that an edge was caught, or
 * - the input value of the gpio doesn't change during the attempt.
 * If the value changes twice during the process, that would cause the first
 * test to fail but would force the second, as two opposite
 * transitions would cause a detection no matter the polarity setting.
 *
 * The do-loop tries to sledge-hammer closed the timing hole between
 * the initial value-read and the polarity-write - if the line value changes
 * during that window, an interrupt is lost, the new polarity setting is
 * incorrect, and the first success test will fail, causing a retry.
 *
 * Algorithm comes from Google's msmgpio driver, see mach-msm/gpio.c.
 */
static void msm_gpio_update_dual_edge_pos(struct irq_data *d, unsigned gpio)
{
	int loop_limit = 100;
	unsigned val, val2, intstat;

	do {
		val = __raw_readl(GPIO_IN_OUT(gpio)) & BIT(GPIO_IN_BIT);
		if (val)
			clr_gpio_bits(INTR_POL_CTL_HI, GPIO_INTR_CFG(gpio));
		else
			set_gpio_bits(INTR_POL_CTL_HI, GPIO_INTR_CFG(gpio));
		val2 = __raw_readl(GPIO_IN_OUT(gpio)) & BIT(GPIO_IN_BIT);
		intstat = __raw_readl(GPIO_INTR_STATUS(gpio)) &
					BIT(INTR_STATUS_BIT);
		if (intstat || val == val2) {
			switch_mpm_config(d, val);
			return;
		}
	} while (loop_limit-- > 0);
	pr_err("%s: dual-edge irq failed to stabilize, "
	       "interrupts dropped. %#08x != %#08x\n",
	       __func__, val, val2);
}

static void msm_gpio_irq_ack(struct irq_data *d)
{
	int gpio = msm_irq_to_gpio(&msm_gpio.gpio_chip, d->irq);

	__raw_writel(BIT(INTR_STATUS_BIT), GPIO_INTR_STATUS(gpio));
	if (test_bit(gpio, msm_gpio.dual_edge_irqs))
		msm_gpio_update_dual_edge_pos(d, gpio);
	mb();
}

static void __msm_gpio_irq_mask(unsigned int gpio)
{
        if (gpio != NFCIRQ_GPIO)
	        __raw_writel(TARGET_PROC_NONE, GPIO_INTR_CFG_SU(gpio)); //1032,Ledger
	clr_gpio_bits(INTR_ENABLE, GPIO_INTR_CFG(gpio));
}

static void msm_gpio_irq_mask(struct irq_data *d)
{
	int gpio = msm_irq_to_gpio(&msm_gpio.gpio_chip, d->irq);
	unsigned long irq_flags;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	__msm_gpio_irq_mask(gpio);
	__clear_bit(gpio, msm_gpio.enabled_irqs);
	mb();
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);

	if (msm_gpio_irq_extn.irq_mask)
		msm_gpio_irq_extn.irq_mask(d);

}

static void __msm_gpio_irq_unmask(unsigned int gpio)
{
        if (gpio == NFCIRQ_GPIO)
	        __raw_writel(BIT(INTR_STATUS_BIT), GPIO_INTR_STATUS(gpio));     //1035,Ledger
	set_gpio_bits(INTR_ENABLE, GPIO_INTR_CFG(gpio));
        if (gpio != NFCIRQ_GPIO)
	        __raw_writel(TARGET_PROC_SCORPION, GPIO_INTR_CFG_SU(gpio));     //1032,Ledger
}

static void msm_gpio_irq_unmask(struct irq_data *d)
{
	int gpio = msm_irq_to_gpio(&msm_gpio.gpio_chip, d->irq);
	unsigned long irq_flags;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	__set_bit(gpio, msm_gpio.enabled_irqs);
	__msm_gpio_irq_unmask(gpio);
	mb();
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);

	if (msm_gpio_irq_extn.irq_mask)
		msm_gpio_irq_extn.irq_unmask(d);
}

static void msm_gpio_irq_disable(struct irq_data *d)
{
	if (msm_gpio_irq_extn.irq_disable)
		msm_gpio_irq_extn.irq_disable(d);
}

static int msm_gpio_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	int gpio = msm_irq_to_gpio(&msm_gpio.gpio_chip, d->irq);
	unsigned long irq_flags;
	uint32_t bits;

	spin_lock_irqsave(&tlmm_lock, irq_flags);

	/* RAW_STATUS_EN is left on for all gpio irqs. Due to the
	 * internal circuitry of TLMM, toggling the RAW_STATUS
	 * could cause the INTR_STATUS to be set for EDGE interrupts.
	 */
        if (gpio == NFCIRQ_GPIO)
	        set_gpio_bits(INTR_RAW_STATUS_EN, GPIO_INTR_CFG(gpio));         //1035,Ledger
	bits = __raw_readl(GPIO_INTR_CFG(gpio));
        if (gpio == NFCIRQ_GPIO)
	        __raw_writel(TARGET_PROC_SCORPION, GPIO_INTR_CFG_SU(gpio));     //1035,Ledger

	if (flow_type & IRQ_TYPE_EDGE_BOTH) {
		bits |= INTR_DECT_CTL_EDGE;
		__irq_set_handler_locked(d->irq, handle_edge_irq);
		if ((flow_type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
			__set_bit(gpio, msm_gpio.dual_edge_irqs);
		else
			__clear_bit(gpio, msm_gpio.dual_edge_irqs);
	} else {
		bits &= ~INTR_DECT_CTL_EDGE;
		__irq_set_handler_locked(d->irq, handle_level_irq);
		__clear_bit(gpio, msm_gpio.dual_edge_irqs);
	}

	if (flow_type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_LEVEL_HIGH))
		bits |= INTR_POL_CTL_HI;
	else
		bits &= ~INTR_POL_CTL_HI;

	__raw_writel(bits, GPIO_INTR_CFG(gpio));
	/* RAW_STATUS_EN is left on for all gpio irqs. Due to the
	 * internal circuitry of TLMM, toggling the RAW_STATUS
	 * could cause the INTR_STATUS to be set for EDGE interrupts.
	 */
        if (gpio != NFCIRQ_GPIO)
	        set_gpio_bits(INTR_RAW_STATUS_EN, GPIO_INTR_CFG(gpio));         //1032,Ledger

	/* Sometimes it might take a little while to update
	* the interrupt status after the RAW_STATUS is enabled
	* We clear the interrupt status before enabling the
	* interrupt in the unmask call-back
	*/
	udelay(5);

	/* Clear the interrupt status to clear out any spurious
	 * irq as a result of the above operation
	 */
        if (gpio !=NFCIRQ_GPIO)
	        __raw_writel(BIT(INTR_STATUS_BIT), GPIO_INTR_STATUS(gpio));     //1032,Ledger

	if ((flow_type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
		msm_gpio_update_dual_edge_pos(d, gpio);

	mb();
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);

	if (msm_gpio_irq_extn.irq_set_type && !((flow_type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)) //ASUS_BSP HANS
		msm_gpio_irq_extn.irq_set_type(d, flow_type);

	return 0;
}

/*
 * When the summary IRQ is raised, any number of GPIO lines may be high.
 * It is the job of the summary handler to find all those GPIO lines
 * which have been set as summary IRQ lines and which are triggered,
 * and to call their interrupt handlers.
 */
static irqreturn_t msm_summary_irq_handler(int irq, void *data)
{
	unsigned long i;
	struct irq_desc *desc = irq_to_desc(irq);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	for (i = find_first_bit(msm_gpio.enabled_irqs, NR_MSM_GPIOS);
	     i < NR_MSM_GPIOS;
	     i = find_next_bit(msm_gpio.enabled_irqs, NR_MSM_GPIOS, i + 1)) {
		if (__raw_readl(GPIO_INTR_STATUS(i)) & BIT(INTR_STATUS_BIT))
			generic_handle_irq(msm_gpio_to_irq(&msm_gpio.gpio_chip,
							   i));
	}

	chained_irq_exit(chip, desc);
	return IRQ_HANDLED;
}

static int msm_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	int gpio = msm_irq_to_gpio(&msm_gpio.gpio_chip, d->irq);

	if (on) {
		if (bitmap_empty(msm_gpio.wake_irqs, NR_MSM_GPIOS))
			irq_set_irq_wake(TLMM_MSM_SUMMARY_IRQ, 1);
		set_bit(gpio, msm_gpio.wake_irqs);
	} else {
		clear_bit(gpio, msm_gpio.wake_irqs);
		if (bitmap_empty(msm_gpio.wake_irqs, NR_MSM_GPIOS))
			irq_set_irq_wake(TLMM_MSM_SUMMARY_IRQ, 0);
	}

	if (msm_gpio_irq_extn.irq_set_wake)
		msm_gpio_irq_extn.irq_set_wake(d, on);

	return 0;
}

static struct irq_chip msm_gpio_irq_chip = {
	.name		= "msmgpio",
	.irq_mask	= msm_gpio_irq_mask,
	.irq_unmask	= msm_gpio_irq_unmask,
	.irq_ack	= msm_gpio_irq_ack,
	.irq_set_type	= msm_gpio_irq_set_type,
	.irq_set_wake	= msm_gpio_irq_set_wake,
	.irq_disable	= msm_gpio_irq_disable,
};

/*
 * This lock class tells lockdep that GPIO irqs are in a different
 * category than their parents, so it won't report false recursion.
 */
static struct lock_class_key msm_gpio_lock_class;

static int __devinit msm_gpio_probe(void)
{
	int i, irq, ret;

	spin_lock_init(&tlmm_lock);
	bitmap_zero(msm_gpio.enabled_irqs, NR_MSM_GPIOS);
	bitmap_zero(msm_gpio.wake_irqs, NR_MSM_GPIOS);
	bitmap_zero(msm_gpio.dual_edge_irqs, NR_MSM_GPIOS);
	ret = gpiochip_add(&msm_gpio.gpio_chip);
	if (ret < 0)
		return ret;

	for (i = 0; i < msm_gpio.gpio_chip.ngpio; ++i) {
		irq = msm_gpio_to_irq(&msm_gpio.gpio_chip, i);
		irq_set_lockdep_class(irq, &msm_gpio_lock_class);
		irq_set_chip_and_handler(irq, &msm_gpio_irq_chip,
					 handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	ret = request_irq(TLMM_MSM_SUMMARY_IRQ, msm_summary_irq_handler,
			IRQF_TRIGGER_HIGH, "msmgpio", NULL);
	if (ret) {
		pr_err("Request_irq failed for TLMM_MSM_SUMMARY_IRQ - %d\n",
				ret);
		return ret;
	}
	return 0;
}

static int __devexit msm_gpio_remove(void)
{
	int ret = gpiochip_remove(&msm_gpio.gpio_chip);

	if (ret < 0)
		return ret;

	irq_set_handler(TLMM_MSM_SUMMARY_IRQ, NULL);

	return 0;
}

#ifdef CONFIG_PM
static void msm_gpio_suspend_reconfig(void)
{
        int gpio_n,write_n;

        if (gpiomux_debug_mask & GPIOMUX_DEBUG_SUSPEND)
                printk("++%s\n",__func__);

        if (gpiomux_debug_mask & GPIOMUX_DEBUG_SUSPEND)
                for(gpio_n=0;gpio_n<ARRAY_SIZE(gpiomux_configs);gpio_n++)
                {
                        printk("GPIO%d CFG:%x, IO:%x \r\n" ,gpiomux_configs[gpio_n].gpio,__raw_readl(GPIO_CONFIG(gpiomux_configs[gpio_n].gpio)),__raw_readl(GPIO_IN_OUT(gpiomux_configs[gpio_n].gpio))); //Ledger
                }

        for (write_n=1;write_n<REWRITE_NUM;write_n++) {
                if ( msm_gpiomux_write_pm(gpiomux_configs[write_n].gpio, gpiomux_configs[write_n].settings[GPIOMUX_SUSPENDED]))
                        pr_err("%s: write gpio%d configs failure\n", __func__,gpiomux_configs[write_n].gpio);
        }

//         if (g_A60K_hwID > A66_HW_ID_ER2)
//                 if ( msm_gpiomux_write_pm(gpiomux_configs[HS_PATH_EN].gpio, gpiomux_configs[HS_PATH_EN].settings[GPIOMUX_SUSPENDED]))
//                         pr_err("%s: write gpio%d configs failure\n", __func__,gpiomux_configs[HS_PATH_EN].gpio);

        if (console_suspend_enabled) {
                if ( msm_gpiomux_write_pm(gpiomux_configs[UART_TXD_DBG].gpio,gpiomux_configs[UART_TXD_DBG].settings[GPIOMUX_SUSPENDED]))
                pr_err("%s: write UART_TXD_DBG failure\n", __func__);
        }

        if (gpiomux_debug_mask & GPIOMUX_DEBUG_SUSPEND)
                printk("--%s\n",__func__);

}
static void msm_gpio_resume_reconfig(void)
{
        int gpio_n,write_n;

        if (gpiomux_debug_mask & GPIOMUX_DEBUG_SUSPEND)
                printk("++%s\n",__func__);

        if (console_suspend_enabled) {
                if ( msm_gpiomux_write_pm(gpiomux_configs[UART_TXD_DBG].gpio,gpiomux_configs[UART_TXD_DBG].settings[GPIOMUX_ACTIVE]))
                pr_err("%s: write UART_TXD_DBG failure\n", __func__);
        }

        for (write_n=1;write_n<REWRITE_NUM;write_n++) {
                if ( msm_gpiomux_write_pm(gpiomux_configs[write_n].gpio, gpiomux_configs[write_n].settings[GPIOMUX_ACTIVE]))
                        pr_err("%s: write gpio%d configs failure\n", __func__,gpiomux_configs[write_n].gpio);
        }

        if (gpiomux_debug_mask & GPIOMUX_DEBUG_SUSPEND)
                for(gpio_n=0;gpio_n<ARRAY_SIZE(gpiomux_configs);gpio_n++)
                {
                        printk("GPIO%d CFG:%x, IO:%x \r\n" ,gpiomux_configs[gpio_n].gpio,__raw_readl(GPIO_CONFIG(gpiomux_configs[gpio_n].gpio)),__raw_readl(GPIO_IN_OUT(gpiomux_configs[gpio_n].gpio))); //Ledger
                }

        if (gpiomux_debug_mask & GPIOMUX_DEBUG_SUSPEND)
                printk("--%s\n",__func__);
}

static int msm_gpio_suspend(void)
{
	unsigned long irq_flags;
	unsigned long i;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	for_each_set_bit(i, msm_gpio.enabled_irqs, NR_MSM_GPIOS)
			__msm_gpio_irq_mask(i);

	for_each_set_bit(i, msm_gpio.wake_irqs, NR_MSM_GPIOS)
		__msm_gpio_irq_unmask(i);

	msm_gpio_suspend_reconfig();//Ledger

	mb();
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
	return 0;
}

extern int msm_show_resume_irq_mask;

void msm_gpio_show_resume_irq(void)
{
	unsigned long irq_flags;
	int i, irq, intstat;

        gpio_irq_cnt=0;

	if (!msm_show_resume_irq_mask)
		return;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	for_each_set_bit(i, msm_gpio.wake_irqs, NR_MSM_GPIOS) {
		intstat = __raw_readl(GPIO_INTR_STATUS(i)) &
					BIT(INTR_STATUS_BIT);
		if (intstat) {
			irq = msm_gpio_to_irq(&msm_gpio.gpio_chip, i);
			pr_warning("[PM]GPIO triggered: %d\n", irq-NR_MSM_IRQS);
//++Ledger
                        gpio_resume_irq[gpio_irq_cnt]=irq-NR_MSM_IRQS;
                        gpio_irq_cnt++;
//--Ledger
		}
	}
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
}

static void msm_gpio_resume(void)
{
	unsigned long irq_flags;
	unsigned long i;

	msm_gpio_resume_reconfig();	//Ledger
	msm_gpio_show_resume_irq();

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	for_each_set_bit(i, msm_gpio.wake_irqs, NR_MSM_GPIOS)
		__msm_gpio_irq_mask(i);

	for_each_set_bit(i, msm_gpio.enabled_irqs, NR_MSM_GPIOS)
		__msm_gpio_irq_unmask(i);
	mb();
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
}
#else
#define msm_gpio_suspend NULL
#define msm_gpio_resume NULL
#endif

static struct syscore_ops msm_gpio_syscore_ops = {
	.suspend = msm_gpio_suspend,
	.resume = msm_gpio_resume,
};

static int __init msm_gpio_init(void)
{
	msm_gpio_probe();
	register_syscore_ops(&msm_gpio_syscore_ops);
	return 0;
}

static void __exit msm_gpio_exit(void)
{
	unregister_syscore_ops(&msm_gpio_syscore_ops);
	msm_gpio_remove();
}

postcore_initcall(msm_gpio_init);
module_exit(msm_gpio_exit);

static void msm_tlmm_set_field(const struct tlmm_field_cfg *configs,
			       unsigned id, unsigned width, unsigned val)
{
	unsigned long irqflags;
	u32 mask = (1 << width) - 1;
	u32 __iomem *reg = MSM_TLMM_BASE + configs[id].reg;
	u32 reg_val;

	spin_lock_irqsave(&tlmm_lock, irqflags);
	reg_val = __raw_readl(reg);
	reg_val &= ~(mask << configs[id].off);
	reg_val |= (val & mask) << configs[id].off;
	__raw_writel(reg_val, reg);
	mb();
	spin_unlock_irqrestore(&tlmm_lock, irqflags);
}

void msm_tlmm_set_hdrive(enum msm_tlmm_hdrive_tgt tgt, int drv_str)
{
	msm_tlmm_set_field(tlmm_hdrv_cfgs, tgt, 3, drv_str);
}
EXPORT_SYMBOL(msm_tlmm_set_hdrive);

void msm_tlmm_set_pull(enum msm_tlmm_pull_tgt tgt, int pull)
{
	msm_tlmm_set_field(tlmm_pull_cfgs, tgt, 2, pull);
}
EXPORT_SYMBOL(msm_tlmm_set_pull);

int gpio_tlmm_config(unsigned config, unsigned disable)
{
	uint32_t flags;
	unsigned gpio = GPIO_PIN(config);

	if (gpio > NR_MSM_GPIOS)
		return -EINVAL;

	flags = ((GPIO_DIR(config) << 9) & (0x1 << 9)) |
		((GPIO_DRVSTR(config) << 6) & (0x7 << 6)) |
		((GPIO_FUNC(config) << 2) & (0xf << 2)) |
		((GPIO_PULL(config) & 0x3));
	__raw_writel(flags, GPIO_CONFIG(gpio));
	mb();

	return 0;
}
EXPORT_SYMBOL(gpio_tlmm_config);

int msm_gpio_install_direct_irq(unsigned gpio, unsigned irq,
					unsigned int input_polarity)
{
	unsigned long irq_flags;
	uint32_t bits;

	if (gpio >= NR_MSM_GPIOS || irq >= NR_TLMM_MSM_DIR_CONN_IRQ)
		return -EINVAL;

	spin_lock_irqsave(&tlmm_lock, irq_flags);

	__raw_writel(__raw_readl(GPIO_CONFIG(gpio)) | BIT(GPIO_OE_BIT),
		GPIO_CONFIG(gpio));
	__raw_writel(__raw_readl(GPIO_INTR_CFG(gpio)) &
		~(INTR_RAW_STATUS_EN | INTR_ENABLE),
		GPIO_INTR_CFG(gpio));
	__raw_writel(DC_IRQ_ENABLE | TARGET_PROC_NONE,
		GPIO_INTR_CFG_SU(gpio));

	bits = TARGET_PROC_SCORPION | (gpio << 3);
	if (input_polarity)
		bits |= DC_POLARITY_HI;
	__raw_writel(bits, DIR_CONN_INTR_CFG_SU(irq));

	mb();
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);

	return 0;
}
EXPORT_SYMBOL(msm_gpio_install_direct_irq);

#ifdef CONFIG_OF
static int msm_gpio_domain_dt_translate(struct irq_domain *d,
					struct device_node *controller,
					const u32 *intspec,
					unsigned int intsize,
					unsigned long *out_hwirq,
					unsigned int *out_type)
{
	if (d->of_node != controller)
		return -EINVAL;
	if (intsize != 2)
		return -EINVAL;

	/* hwirq value */
	*out_hwirq = intspec[0];

	/* irq flags */
	*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;
	return 0;
}

static struct irq_domain_ops msm_gpio_irq_domain_ops = {
	.dt_translate = msm_gpio_domain_dt_translate,
};

int __init msm_gpio_of_init(struct device_node *node,
			    struct device_node *parent)
{
	struct irq_domain *domain = &msm_gpio.domain;

	domain->irq_base = irq_domain_find_free_range(0, NR_MSM_GPIOS);
	domain->nr_irq = NR_MSM_GPIOS;
	domain->of_node = of_node_get(node);
	domain->priv = &msm_gpio;
	domain->ops = &msm_gpio_irq_domain_ops;
	irq_domain_add(domain);
	pr_debug("%s: irq_base = %u\n", __func__, domain->irq_base);

	return 0;
}
#endif
//+++ ASUS_BSP miniporting : Simpson
int msm_gpios_request_enable(const struct msm_gpio *table, int size)
{
	int rc = msm_gpios_request(table, size);
	if (rc)
		return rc;
	rc = msm_gpios_enable(table, size);
	if (rc)
		msm_gpios_free(table, size);
	return rc;
}
EXPORT_SYMBOL(msm_gpios_request_enable);

int msm_gpios_request(const struct msm_gpio *table, int size)
{
	int rc;
	int i;
	const struct msm_gpio *g;
	for (i = 0; i < size; i++) {
		g = table + i;
		rc = gpio_request(GPIO_PIN(g->gpio_cfg), g->label);
		if (rc) {
			pr_err("gpio_request(%d) <%s> failed: %d\n",
			       GPIO_PIN(g->gpio_cfg), g->label ?: "?", rc);
			goto err;
		}
	}
	return 0;
err:
	msm_gpios_free(table, i);
	return rc;
}
EXPORT_SYMBOL(msm_gpios_request);

void msm_gpios_free(const struct msm_gpio *table, int size)
{
	int i;
	const struct msm_gpio *g;
	for (i = size-1; i >= 0; i--) {
		g = table + i;
		gpio_free(GPIO_PIN(g->gpio_cfg));
	}
}
EXPORT_SYMBOL(msm_gpios_free);

int msm_gpios_enable(const struct msm_gpio *table, int size)
{
	int rc;
	int i;
	const struct msm_gpio *g;
	for (i = 0; i < size; i++) {
		g = table + i;
		rc = gpio_tlmm_config(g->gpio_cfg, GPIO_CFG_ENABLE);
		if (rc) {
			pr_err("gpio_tlmm_config(0x%08x, GPIO_CFG_ENABLE)"
			       " <%s> failed: %d\n",
			       g->gpio_cfg, g->label ?: "?", rc);
			pr_err("pin %d func %d dir %d pull %d drvstr %d\n",
			       GPIO_PIN(g->gpio_cfg), GPIO_FUNC(g->gpio_cfg),
			       GPIO_DIR(g->gpio_cfg), GPIO_PULL(g->gpio_cfg),
			       GPIO_DRVSTR(g->gpio_cfg));
			goto err;
		}
	}
	return 0;
err:
	msm_gpios_disable(table, i);
	return rc;
}
EXPORT_SYMBOL(msm_gpios_enable);

int msm_gpios_disable(const struct msm_gpio *table, int size)
{
	int rc = 0;
	int i;
	const struct msm_gpio *g;
	for (i = size-1; i >= 0; i--) {
		int tmp;
		g = table + i;
		tmp = gpio_tlmm_config(g->gpio_cfg, GPIO_CFG_DISABLE);
		if (tmp) {
			pr_err("gpio_tlmm_config(0x%08x, GPIO_CFG_DISABLE)"
			       " <%s> failed: %d\n",
			       g->gpio_cfg, g->label ?: "?", rc);
			pr_err("pin %d func %d dir %d pull %d drvstr %d\n",
			       GPIO_PIN(g->gpio_cfg), GPIO_FUNC(g->gpio_cfg),
			       GPIO_DIR(g->gpio_cfg), GPIO_PULL(g->gpio_cfg),
			       GPIO_DRVSTR(g->gpio_cfg));
			if (!rc)
				rc = tmp;
		}
	}

	return rc;
}
EXPORT_SYMBOL(msm_gpios_disable);
//--- ASUS_BSP miniporting : Simpson

MODULE_AUTHOR("Gregory Bean <gbean@codeaurora.org>");
MODULE_DESCRIPTION("Driver for Qualcomm MSM TLMMv2 SoC GPIOs");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("sysdev:msmgpio");
