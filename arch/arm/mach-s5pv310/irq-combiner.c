/* linux/arch/arm/mach-s5pv310/irq-combiner.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Based on arch/arm/common/gic.c
 *
 * IRQ COMBINER support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>

#include <asm/mach/irq.h>

#define COMBINER_ENABLE_SET	0x0
#define COMBINER_ENABLE_CLEAR	0x4
#define COMBINER_INT_STATUS	0xC

static DEFINE_SPINLOCK(irq_controller_lock);

struct combiner_chip_data {
	unsigned int irq_offset;
	unsigned int irq_mask;
	void __iomem *base;
};

static struct combiner_chip_data combiner_data[MAX_COMBINER_NR];

static inline void __iomem *combiner_base(unsigned int irq)
{
	struct combiner_chip_data *combiner_data = get_irq_chip_data(irq);
	return combiner_data->base;
}

static void combiner_mask_irq(unsigned int irq)
{
	u32 mask = 1 << (irq % 32);

	spin_lock(&irq_controller_lock);
	__raw_writel(mask, combiner_base(irq) + COMBINER_ENABLE_CLEAR);
	spin_unlock(&irq_controller_lock);
}

static void combiner_unmask_irq(unsigned int irq)
{
	u32 mask = 1 << (irq % 32);

	spin_lock(&irq_controller_lock);
	__raw_writel(mask, combiner_base(irq) + COMBINER_ENABLE_SET);
	spin_unlock(&irq_controller_lock);
}

static void combiner_handle_cascade_irq(unsigned int irq, struct irq_desc *desc)
{
	struct combiner_chip_data *chip_data = get_irq_data(irq);
	struct irq_chip *chip = get_irq_chip(irq);
	unsigned int cascade_irq, combiner_irq;
	unsigned long status;

	/* primary controller ack'ing */
	chip->ack(irq);

	spin_lock(&irq_controller_lock);
	status = __raw_readl(chip_data->base + COMBINER_INT_STATUS);
	spin_unlock(&irq_controller_lock);
	status &= chip_data->irq_mask;

	if (status == 0)
		goto out;

	combiner_irq = __ffs(status);

	cascade_irq = combiner_irq + (chip_data->irq_offset & ~31);
	if (unlikely(cascade_irq >= NR_IRQS))
		do_bad_IRQ(cascade_irq, desc);
	else
		generic_handle_irq(cascade_irq);

 out:
	/* primary controller unmasking */
	chip->unmask(irq);
}

#ifdef CONFIG_SMP
static int combiner_set_cpu(unsigned int irq, const struct cpumask *mask_val)
{
	struct irq_chip *chip = NULL;
	unsigned long gic_irq;

	gic_irq = ((irq - COMBINER_IRQ(0,0) ) >> 3 ) + IRQ_SPI(0);

	chip = get_irq_chip(gic_irq);

	return chip->set_affinity(gic_irq, mask_val);
}
#endif

static struct irq_chip combiner_chip = {
	.name		= "COMBINER",
	.mask		= combiner_mask_irq,
	.unmask		= combiner_unmask_irq,
	.disable	= combiner_mask_irq,
#ifdef CONFIG_SMP
	.set_affinity 	= combiner_set_cpu,
#endif
};


void __init combiner_cascade_irq(unsigned int combiner_nr, unsigned int irq)
{
	if (combiner_nr >= MAX_COMBINER_NR)
		BUG();
	if (set_irq_data(irq, &combiner_data[combiner_nr]) != 0)
		BUG();
	set_irq_chained_handler(irq, combiner_handle_cascade_irq);
}

void __init combiner_init(unsigned int combiner_nr, void __iomem *base,
			  unsigned int irq_start)
{
	unsigned int i;

	if (combiner_nr >= MAX_COMBINER_NR)
		BUG();

	combiner_data[combiner_nr].base = base;
	combiner_data[combiner_nr].irq_offset = irq_start;
	combiner_data[combiner_nr].irq_mask = 0xff << ((combiner_nr % 4) << 3);

	/* Disable all interrupts */
	__raw_writel(combiner_data[combiner_nr].irq_mask,
		     base + COMBINER_ENABLE_CLEAR);

	/* Setup the Linux IRQ subsystem */

	for (i = irq_start; i < combiner_data[combiner_nr].irq_offset
				+ MAX_IRQ_IN_COMBINER; i++) {
		set_irq_chip(i, &combiner_chip);
		set_irq_chip_data(i, &combiner_data[combiner_nr]);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}
}
