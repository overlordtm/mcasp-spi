/*
 *  mcaspspi.c
 */

#include <linux/init.h>
#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/platform_data/davinci_asp.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/fs.h>


#include "mcasp.h"

#define MCASP_MAX_AFIFO_DEPTH	64
#define MCASP_DEBUG				1

static struct davinci_mcasp_pdata am33xx_mcasp_pdata = {
	.tx_dma_offset = 0,
	.rx_dma_offset = 0,
	.version = MCASP_VERSION_3,
};

static const struct of_device_id mcasp_dt_ids[] = {
	{
		.compatible = "ti,am33xx-mcasp-audio",
		.data = &am33xx_mcasp_pdata,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mcasp_dt_ids);


struct davinci_mcasp {
	void __iomem *base;
	struct device *dev;
	struct clk *clk;

	u32 revision;
};

/*
 * Register handling stuff
*/

static inline void mcasp_set_bits(struct davinci_mcasp *mcasp, u32 offset,
				  u32 val)
{
	void __iomem *reg = mcasp->base + offset;
	__raw_writel(__raw_readl(reg) | val, reg);
}

static inline void mcasp_clr_bits(struct davinci_mcasp *mcasp, u32 offset,
				  u32 val)
{
	void __iomem *reg = mcasp->base + offset;
	__raw_writel((__raw_readl(reg) & ~(val)), reg);
}

static inline void mcasp_mod_bits(struct davinci_mcasp *mcasp, u32 offset,
				  u32 val, u32 mask)
{
	void __iomem *reg = mcasp->base + offset;
	__raw_writel((__raw_readl(reg) & ~mask) | val, reg);
}

static inline void mcasp_set_reg(struct davinci_mcasp *mcasp, u32 offset,
				 u32 val)
{
	__raw_writel(val, mcasp->base + offset);
}

static inline u32 mcasp_get_reg(struct davinci_mcasp *mcasp, u32 offset)
{
	return (u32)__raw_readl(mcasp->base + offset);
}

static void mcasp_set_ctl_reg(struct davinci_mcasp *mcasp, u32 ctl_reg, u32 val)
{
	int i = 0;

	mcasp_set_bits(mcasp, ctl_reg, val);

	/* programming GBLCTL needs to read back from GBLCTL and verfiy */
	/* loop count is to avoid the lock-up */
	for (i = 0; i < 100000; i++) {
		if ((mcasp_get_reg(mcasp, ctl_reg) & val) == val)
			break;
	}

	if (i == 100000 && ((mcasp_get_reg(mcasp, ctl_reg) & val) != val))
		dev_err(mcasp->dev, "GBLCTL write error\n");
}

/*
 * end of register stuff
 */


// static ssize_t mcasp_show_revision(struct device *dev, struct device_attribute *attr, char *buf) {
// 	dev_info(dev, "mcasp_show_revision %x %x", dev, buf);
// 	// struct davinci_mcasp *mcasp = dev_get_drvdata(dev);

// 	// return sprintf(buf, "0x%X", mcasp->revision);
// 	return 0;
// }
// static DEVICE_ATTR(revision, 0444, mcasp_show_revision, NULL);

static irqreturn_t mcasp_tx_irq_handler(int irq, void *data)
{
	struct davinci_mcasp *mcasp = (struct davinci_mcasp *)data;
	u32 handled_mask = 0;
	u32 stat;

	mcasp_set_reg(mcasp, DAVINCI_MCASP_XBUF_REG(0), 0xA5);

	stat = mcasp_get_reg(mcasp, DAVINCI_MCASP_XSTAT_REG);

	if (stat & XUNDRN) {
		dev_warn(mcasp->dev, "Transmit buffer underflow");
		handled_mask |= XUNDRN;
	}

	if (stat & XRDATA) {
		dev_warn(mcasp->dev, "XDATA bit not cleared in XSTAT");
		handled_mask |= XRDATA;
	}

	/* Ack the handled event only */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XSTAT_REG, handled_mask);

	return IRQ_RETVAL(handled_mask);
}

static irqreturn_t mcasp_rx_irq_handler(int irq, void *data)
{
	struct davinci_mcasp *mcasp = (struct davinci_mcasp *)data;
	u32 handled_mask = 0;
	u32 stat;
	u32 val;

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_RBUF_REG(0));

	dev_info(mcasp->dev, "Received 0x%X", val);

	stat = mcasp_get_reg(mcasp, DAVINCI_MCASP_RSTAT_REG);

	if (stat & ROVRN) {
		dev_warn(mcasp->dev, "Receive buffer overflow");
		handled_mask |= ROVRN;
	}

	if (stat & XRDATA) {
		dev_warn(mcasp->dev, "RDATA bit not cleared in RSTAT");
		handled_mask |= XRDATA;
	}

	/* Ack the handled event only */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RSTAT_REG, handled_mask);

	return IRQ_RETVAL(handled_mask);
}

static void mcasp_rx_init(struct davinci_mcasp *mcasp) {
	u32 val = 0;

	// mask bits
	val = (1UL << 16) - 1;
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RMASK_REG, val);
	dev_info(mcasp->dev, "Setting RMASK to 0x%X", val);

	// format bits
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RFMT_REG, FSRDLY(0x1), FSRDLY(0x3));
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RXORD);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RXSSZ(0x7), RXSSZ(0xF));
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RXSEL);

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_RFMT_REG);
	dev_info(mcasp->dev, "Setting RFMT to 0x%X", val);

	// frame sync
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, FSRP);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, FSRM);
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, FRWID);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, RMOD(0x8), RMOD(0x1FF));

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_AFSRCTL_REG);
	dev_info(mcasp->dev, "Setting AFSRCTL to 0x%X", val);

	// clock
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, CLKRP);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, CLKRM);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, CLKRDIV(23), CLKRDIV_MASK);

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_ACLKRCTL_REG);
	dev_info(mcasp->dev, "Setting ACLKRCTL to 0x%X", val);

	// high clock
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, HCLKRM);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, HCLKRP);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, HCLKRDIV(2), HCLKRDIV_MASK);

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG);
	dev_info(mcasp->dev, "Setting AHCLKRCTL to 0x%X", val);

	// ROVRN interrupt eanble
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RINTCTL_REG, ROVRN);

	// setup serializer on pin ???
	// TODO
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_SRCTL_REG(1), SRMOD_TX, SRMOD_MASK);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_SRCTL_REG(1), DISMOD_HIGH, DISMOD_MASK);

	return;
}

static void mcasp_tx_init(struct davinci_mcasp *mcasp) {
	u32 val = 0;

	// mask
	val = (1UL << 16) - 1;
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XMASK_REG, val);
	dev_info(mcasp->dev, "Setting XMASK to 0x%X", val);

	// format
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_XFMT_REG, FSRDLY(0x1), FSRDLY(0x3));
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XFMT_REG, RXORD);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_XFMT_REG, RXSSZ(0x7), RXSSZ(0xF));
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XFMT_REG, RXSEL);

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_XFMT_REG);
	dev_info(mcasp->dev, "Setting XFMT to 0x%X", val);

	// frame sync
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, FSXP);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, FSXM);
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, FXWID);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, XMOD(0x8), XMOD(0x1FF));

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_AFSXCTL_REG);
	dev_info(mcasp->dev, "Setting AFSXCTL to 0x%X", val);

	// clock
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, CLKXP); // falling edge polarity
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ASYNC); // sync freqs
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, CLKXM); // internal clock source
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, CLKXDIV(23), CLKXDIV_MASK);

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_ACLKXCTL_REG);
	dev_info(mcasp->dev, "Setting ACLKXCTL to 0x%X", val);

	// high clock
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, HCLKXM);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, HCLKXP);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, HCLKXDIV(2), HCLKXDIV_MASK);

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG);
	dev_info(mcasp->dev, "Setting AHCLKXCTL to 0x%X", val);

	// XUNDRN interrupt eanble
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XINTCTL_REG, XUNDRN);

	// setup serializer on pin ???
	// TODO
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_SRCTL_REG(0), SRMOD_TX, SRMOD_MASK);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_SRCTL_REG(0), DISMOD_HIGH, DISMOD_MASK);

	return;
}

static int mcasp_hw_init(struct davinci_mcasp *mcasp) {
	u32 val = 0;
	u32 cnt;

	pm_runtime_get_sync(mcasp->dev);
	mcasp->revision = mcasp_get_reg(mcasp, DAVINCI_MCASP_REV_REG);

	dev_info(mcasp->dev, "Starting intialization. Device revision is 0x%X", mcasp->revision);
	mcasp_set_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, 0x0);

	cnt = 0;
	while(mcasp_get_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG) != 0 && cnt < 100000)
		cnt++;

	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG);
	dev_info(mcasp->dev, "GBLCTL set to 0x%X", val);

	// poser configuration
	mcasp_set_reg(mcasp, DAVINCI_MCASP_PWRIDLESYSCONFIGT_REG, MCASP_SMARTIDLE);
	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_PWRIDLESYSCONFIGT_REG);
	dev_info(mcasp->dev, "Setting PWREMUMGT to 0x%X", val);

	mcasp_rx_init(mcasp);
	mcasp_tx_init(mcasp);

	// set all pins as McASP
	mcasp_set_reg(mcasp, DAVINCI_MCASP_PFUNC_REG, 0x00000000);

	// setup pin directions
	// set -> output
	// clr -> input
	mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, AXR(0));
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG, AXR(1));

#ifdef MCASP_DEBUG
	dev_info(mcasp->dev, "Device loopback enabled");
	// setup loopback
	mcasp_set_bits(mcasp, DAVINCI_MCASP_DLBCTL_REG, DLBEN);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_DLBCTL_REG, DLBORD);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_DLBCTL_REG, DLBMODE(1), DLBMODE_MASK);
#endif

	// start the clocks
	dev_info(mcasp->dev, "Starting high clocks by setting RHCLKRST and XHCLKRST bits in GBLCTL");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RHCLKRST);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XHCLKRST);

	// start clocks #2
	dev_info(mcasp->dev, "Starting clocks by setting RHCLKRST and XHCLKRST bits in GBLCTL");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RCLKRST);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XCLKRST);

	// clear receive status register
	dev_info(mcasp->dev, "Clearing RSTAT register");
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RSTAT_REG, 0xFFFF);
	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_RSTAT_REG);
	dev_info(mcasp->dev, "RSTAT=0x%X", val);
	cnt = 0;
	while((mcasp_get_reg(mcasp, DAVINCI_MCASP_RSTAT_REG) & XRDATA) && cnt < 100000)
		cnt++;
	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_RSTAT_REG);
	dev_info(mcasp->dev, "RSTAT=0x%X", val);

	// clear transmit status register
	dev_info(mcasp->dev, "Clearing XSTAT register");
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XSTAT_REG, 0xFFFF);
	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_XSTAT_REG);
	dev_info(mcasp->dev, "XSTAT=0x%X", val);
	cnt = 0;
	while((mcasp_get_reg(mcasp, DAVINCI_MCASP_XSTAT_REG) & XRDATA) && cnt < 100000)
		cnt++;
	val = mcasp_get_reg(mcasp, DAVINCI_MCASP_XSTAT_REG);
	dev_info(mcasp->dev, "XSTAT=0x%X", val);

	// take serializers out of reset
	dev_info(mcasp->dev, "Taking serializers out of reset");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RSRCLR);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XSRCLR);

	// reset state machines
	dev_info(mcasp->dev, "Reseting state machines for RX and TX");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RSMRST);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XSMRST);

	// release frame sync
	dev_info(mcasp->dev, "release frame sync generators");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RFSRST);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XFSRST);

	// enable send and receive interupts
	dev_info(mcasp->dev, "Enabling RDATA and XDATA interrupts");
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RINTCTL_REG, RDATA);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XINTCTL_REG, XDATA);

	pm_runtime_put(mcasp->dev);

	return 0;
}


static int mcaspspi_probe(struct platform_device *pdev)
{
 	struct resource *mem;
	struct davinci_mcasp *mcasp;
	char *irq_name;
	int irq;
	int ret;

	dev_info(&pdev->dev, "mcaspspi_probe %s", *&pdev->name);

	if (!pdev->dev.platform_data && !pdev->dev.of_node) {
		dev_err(&pdev->dev, "No platform data supplied\n");
		return -EINVAL;
	}

	mcasp = devm_kzalloc(&pdev->dev, sizeof(struct davinci_mcasp), GFP_KERNEL);
	if (!mcasp) {
		return -ENOMEM;
	}

	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mpu");
	if (!mem) {
		dev_warn(mcasp->dev, "\"mpu\" mem resource not found, using index 0\n");
		mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!mem) {
			dev_err(&pdev->dev, "no mem resource?\n");
			return -ENODEV;
		}
	}

	dev_info(&pdev->dev, "Memory area: Start: %lx,  End:%lx Size:%d\n", (unsigned long)mem->start, (unsigned long)mem->end, resource_size(mem));

	mcasp->dev = &pdev->dev;

	mcasp->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(mcasp->base)) {
		return PTR_ERR(mcasp->base);
	}

	pm_runtime_enable(&pdev->dev);

	irq = platform_get_irq_byname(pdev, "tx");
	if (irq >= 0) {
		irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_tx", dev_name(&pdev->dev));
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL, mcasp_tx_irq_handler, IRQF_ONESHOT | IRQF_SHARED, irq_name, mcasp);

		if (ret) {
			dev_err(&pdev->dev, "TX IRQ request failed\n");
			goto err;
		}
	}

	irq = platform_get_irq_byname(pdev, "rx");
	if (irq >= 0) {
		irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_rx", dev_name(&pdev->dev));
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL, mcasp_rx_irq_handler, IRQF_ONESHOT | IRQF_SHARED, irq_name, mcasp);

		if (ret) {
			dev_err(&pdev->dev, "RX IRQ request failed\n");
			goto err;
		}
	}

	dev_set_drvdata(&pdev->dev, mcasp);

	mcasp->clk = devm_clk_get(&pdev->dev, NULL);
	clk_prepare_enable(mcasp->clk);
	if (IS_ERR(mcasp->clk)) {
		dev_err(&pdev->dev, "clock error");
		ret = PTR_ERR(mcasp->clk);
		goto err;
	}

	mcasp_hw_init(mcasp);

	return 0;

err:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int mcaspspi_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

// static struct attribute *dev_attrs[] = {
// 	&dev_attr_revision.attr,
// 	NULL,
// };

// static struct attribute_group dev_attr_group = {
// 	.attrs = dev_attrs
// };

// static const struct attribute_group *dev_attr_groups[] = {
// 	&dev_attr_group,
// 	NULL,
// };

static struct platform_driver mcasp_driver = {
	.probe          = mcaspspi_probe,
	.remove         = mcaspspi_remove,
	.driver = {
			.name  = "mcasp-spi",
			.of_match_table = mcasp_dt_ids,
			// .groups = dev_attr_groups,
	},
};

module_platform_driver(mcasp_driver)

MODULE_AUTHOR("Andraz Vrhovec");
MODULE_LICENSE("GPL");