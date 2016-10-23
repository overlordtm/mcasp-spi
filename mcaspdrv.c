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

#define AXRNTX		0
#define AXRNRX		1
#define TDM_SLOTS	8

#define MCASP_DEBUG

#define MCASP_DEBUG_IRQTX
#define MCASP_DEBUG_IRQRX

#define REG_DUMP(MCASP, REG) \
	dev_info(MCASP->dev, #REG " is 0x%08X", mcasp_get_reg(MCASP, REG));

static const struct of_device_id mcasp_dt_ids[] = {
	{
		.compatible = "ti,am33xx-mcasp-audio",
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
		if ((mcasp_get_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG) & val) == val)
			break;
	}

	if (i == 100000 && ((mcasp_get_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG) & val) != val))
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

	// pm_runtime_get_sync(mcasp->dev);

	stat = mcasp_get_reg(mcasp, DAVINCI_MCASP_XSTAT_REG);

	if (stat & XDATA) {
		dev_warn(mcasp->dev, "Sent data");
		mcasp_set_reg(mcasp, DAVINCI_MCASP_XBUF_REG(AXRNTX), 0xFFFFFFFF);
		handled_mask |= XDATA;
	}

	if (stat & XUNDRN) {
		dev_warn(mcasp->dev, "Transmit buffer underflow");
		handled_mask |= XUNDRN;
	}

	// if (stat & XSYNCERR) {
	// 	dev_warn(mcasp->dev, "Transmit frame sync error");
	// 	handled_mask |= XSYNCERR;
	// }

	// if (stat & XCKFAIL) {
	// 	dev_warn(mcasp->dev, "Transmit clock failure");
	// 	handled_mask |= XCKFAIL;
	// }

	// if (stat & XDMAERR) {
	// 	dev_warn(mcasp->dev, "Transmit DMA failure");
	// 	handled_mask |= XDMAERR;
	// }


	// if (stat & XLAST) {
	// 	dev_warn(mcasp->dev, "Transmit last slot");
	// 	handled_mask |= XLAST;
	// }

	// if (stat & XSTAFRM) {
	// 	dev_warn(mcasp->dev, "Transmit start of frame");
	// 	handled_mask |= XSTAFRM;
	// }

	if (stat & XRERR) {
		handled_mask |= XRERR;
	}

	/* Ack the handled event only */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XSTAT_REG, handled_mask);

	// pm_runtime_put(mcasp->dev);

	return IRQ_RETVAL(handled_mask);
}

static irqreturn_t mcasp_rx_irq_handler(int irq, void *data)
{
	struct davinci_mcasp *mcasp = (struct davinci_mcasp *)data;
	u32 handled_mask = 0;
	u32 stat;
	u32 val;

	// pm_runtime_get_sync(mcasp->dev);

	stat = mcasp_get_reg(mcasp, DAVINCI_MCASP_RSTAT_REG);

	if (stat & RDATA) {
		dev_warn(mcasp->dev, "Receive data");
		val = mcasp_get_reg(mcasp, DAVINCI_MCASP_RBUF_REG(AXRNRX));
		handled_mask |= RDATA;
	}

	if (stat & ROVRN) {
		dev_warn(mcasp->dev, "Receive buffer overflow");
		handled_mask |= ROVRN;
	}

	// if (stat & RSYNCERR) {
	// 	dev_warn(mcasp->dev, "Receive frame sync error");
	// 	handled_mask |= RSYNCERR;
	// }

	// if (stat & RCKFAIL) {
	// 	dev_warn(mcasp->dev, "Receive clock failure");
	// 	handled_mask |= RCKFAIL;
	// }

	// if (stat & RDMAERR) {
	// 	dev_warn(mcasp->dev, "Receive DMA error");
	// 	handled_mask |= RDMAERR;
	// }


	// if (stat & RLAST) {
	// 	dev_warn(mcasp->dev, "Receive last slot");
	// 	handled_mask |= RLAST;
	// }

	// if (stat & RSTAFRM) {
	// 	dev_warn(mcasp->dev, "Received start of frame");
	// 	handled_mask |= RSTAFRM;
	// }

	if (stat & XRERR) {
		handled_mask |= XRERR;
	}

	/* Ack the handled event only */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RSTAT_REG, handled_mask);

	// pm_runtime_put(mcasp->dev);

	return IRQ_RETVAL(handled_mask);
}

static void mcasp_rx_init(struct davinci_mcasp *mcasp) {

	// mask bits
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RMASK_REG, 0xFF);
	REG_DUMP(mcasp, DAVINCI_MCASP_RMASK_REG);

	// format bits
	// right rotation is 0
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RROT(0), RROT_MASK);
	// reads from CFG bus
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RBUSEL);
	// receive slot size is 16 bits
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RSSZ(0x7), RSSZ_MASK);
	// pad extra bits not belonging to word with 0
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RPAD(0), RPAD_MASK);
	// bit stream is MSB first
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RRVRS);
	// receive bit delay is 1 bit
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RDATDLY(0x1), RDATDLY_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_RFMT_REG);

	// frame sync
	// Receive frame sync polarity select bit.
	// 0 = A rising edge on receive frame sync (AFSR) indicates the beginning of a frame.
	// 1 = A falling edge on receive frame sync (AFSR) indicates the beginning of a frame.
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, FSRP);
	// internaly generated frame sync
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, FSRM);
	// frame sync width is 1 bit
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, FRWID);
	// set number of tdm slots
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, RMOD(TDM_SLOTS), RMOD_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_AFSRCTL_REG);

	// bit clock setup
	// clock divide rate (actualy no effect since ACLKXCTL.ASYNC=0)
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, CLKRDIV(2), CLKRDIV_MASK);
	// internal clock source=1
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, CLKRM);
	// receive bitstream clock polarity
	// 0 = Falling edge. Receiver samples data on the falling edge of the serial clock, so the external transmitter
	// driving this receiver must shift data out on the rising edge of the serial clock.
	// 1 = Rising edge. Receiver samples data on the rising edge of the serial clock, so the external transmitter
	// driving this receiver must shift data out on the falling edge of the serial clock.
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, CLKRP);
	REG_DUMP(mcasp, DAVINCI_MCASP_ACLKRCTL_REG);

	// high clock
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, HCLKRM);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, HCLKRP);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, HCLKRDIV(7), HCLKRDIV_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG);

	// ROVRN interrupt eanble
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RINTCTL_REG, ROVRN);
	REG_DUMP(mcasp, DAVINCI_MCASP_RINTCTL_REG);

	// clock check
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RCLKCHK_REG, 0x00FF0003);
	REG_DUMP(mcasp, DAVINCI_MCASP_RCLKCHK_REG);

	// set TDM
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RTDM_REG, 0x2);
	REG_DUMP(mcasp, DAVINCI_MCASP_RTDM_REG);


	return;
}

static void mcasp_tx_init(struct davinci_mcasp *mcasp) {

	// mask
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XMASK_REG, 0xFF);
	REG_DUMP(mcasp, DAVINCI_MCASP_XMASK_REG);

	// format
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XROT(0), XROT_MAKS);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XBUSEL);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XSSZ(0x7), XSSZ_MASK);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XPAD(0), XPAD_MASK);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XRVRS);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XDATDLY(0x1), XDATDLY_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_XFMT_REG);

	// frame sync
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, FSXP);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, FSXM);
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, FXWID);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, XMOD(TDM_SLOTS), XMOD_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_AFSXCTL_REG);

	// clock
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, CLKXDIV(2), CLKXDIV_MASK);
	// clock internal
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, CLKXM);
	// sync clock, TX provides RX clock
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ASYNC);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, CLKXP);
	REG_DUMP(mcasp, DAVINCI_MCASP_ACLKXCTL_REG);

	// high clock
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, HCLKXM);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, HCLKXP);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, HCLKXDIV(7), HCLKXDIV_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG);

	// XUNDRN interrupt eanble
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XINTCTL_REG, XUNDRN);
	REG_DUMP(mcasp,DAVINCI_MCASP_XINTCTL_REG);

	// set clock check
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XCLKCHK_REG, 0x00FF0003);
	REG_DUMP(mcasp, DAVINCI_MCASP_XCLKCHK_REG);

	// set TDM
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XTDM_REG, 0x2);
	REG_DUMP(mcasp, DAVINCI_MCASP_XTDM_REG);

	return;
}

static int mcasp_hw_init(struct davinci_mcasp *mcasp) {
	u32 cnt;

	pm_runtime_get_sync(mcasp->dev);
	mcasp->revision = mcasp_get_reg(mcasp, DAVINCI_MCASP_REV_REG);
	REG_DUMP(mcasp, DAVINCI_MCASP_REV_REG);

	dev_info(mcasp->dev, "Starting intialization.");
	mcasp_set_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, 0x0);

	cnt = 0;
	while(mcasp_get_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG) != 0 && cnt < 100000)
		cnt++;

	REG_DUMP(mcasp, DAVINCI_MCASP_GBLCTL_REG);

	// power configuration
	mcasp_set_reg(mcasp, DAVINCI_MCASP_PWRIDLESYSCONFIGT_REG, MCASP_SMARTIDLE);
	REG_DUMP(mcasp, DAVINCI_MCASP_PWRIDLESYSCONFIGT_REG);

	mcasp_tx_init(mcasp);
	mcasp_rx_init(mcasp);

	// setup TX serializer
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_SRCTL_REG(AXRNTX), SRMOD_TX, SRMOD_MASK);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_SRCTL_REG(AXRNTX), DISMOD_LOW, DISMOD_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_SRCTL_REG(AXRNTX));

	// setup RX serializer
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_SRCTL_REG(AXRNRX), SRMOD_RX, SRMOD_MASK);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_SRCTL_REG(AXRNRX), DISMOD_LOW, DISMOD_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_SRCTL_REG(AXRNRX));

	// set all pins as McASP
	mcasp_set_reg(mcasp, DAVINCI_MCASP_PFUNC_REG, 0x00000000);
	REG_DUMP(mcasp, DAVINCI_MCASP_PFUNC_REG);

	// setup pin directions
	// set -> output
	// clr -> input
	mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, PDIR_AXR(AXRNTX));
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_PDIR_REG, PDIR_AXR(AXRNRX));
	mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, PDIR_ACLKX);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, PDIR_AHCLKX);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, PDIR_AFSX);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, PDIR_ACLKR);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, PDIR_AHCLKR);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_PDIR_REG, PDIR_AFSR);
	REG_DUMP(mcasp, DAVINCI_MCASP_PDIR_REG);

#ifdef MCASP_DEBUG
	dev_info(mcasp->dev, "Device loopback enabled");
	// setup loopback
	mcasp_set_bits(mcasp, DAVINCI_MCASP_DLBCTL_REG, DLBEN);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_DLBCTL_REG, DLBORD);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_DLBCTL_REG, DLBMODE(1), DLBMODE_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_DLBCTL_REG);
#endif

	// start high requency clocks
	dev_info(mcasp->dev, "Starting high freq clocks by setting RHCLKRST and XHCLKRST bits in GBLCTL");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XHCLKRST);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RHCLKRST);
	REG_DUMP(mcasp, DAVINCI_MCASP_GBLCTL_REG);

	// start serial clocks
	dev_info(mcasp->dev, "Starting serial clocks by setting RCLKRST and XCLKRST bits in GBLCTL");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XCLKRST);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RCLKRST);
	REG_DUMP(mcasp, DAVINCI_MCASP_GBLCTL_REG);

	// enable send and receive interupts
	dev_info(mcasp->dev, "Enabling RDATA and XDATA interrupts");
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XINTCTL_REG, XDATA);
	REG_DUMP(mcasp, DAVINCI_MCASP_XINTCTL_REG);
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RINTCTL_REG, RDATA);
	REG_DUMP(mcasp, DAVINCI_MCASP_RINTCTL_REG);

	// clear receive status register
	dev_info(mcasp->dev, "Clearing RSTAT register");
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RSTAT_REG, 0xFFFF);
	REG_DUMP(mcasp, DAVINCI_MCASP_RSTAT_REG);

	// clear transmit status register
	dev_info(mcasp->dev, "Clearing XSTAT register");
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XSTAT_REG, 0xFFFF);
	REG_DUMP(mcasp, DAVINCI_MCASP_XSTAT_REG);

	// take serializers out of reset
	dev_info(mcasp->dev, "Taking serializers out of reset");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XSRCLR);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RSRCLR);
	REG_DUMP(mcasp, DAVINCI_MCASP_GBLCTL_REG);

	mcasp_set_reg(mcasp, DAVINCI_MCASP_XSTAT_REG, 0xFFFF);
	REG_DUMP(mcasp, DAVINCI_MCASP_XSTAT_REG);
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RSTAT_REG, 0xFFFF);
	REG_DUMP(mcasp, DAVINCI_MCASP_RSTAT_REG);



	// reset state machines
	dev_info(mcasp->dev, "Reseting state machines for RX and TX");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XSMRST);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RSMRST);
	REG_DUMP(mcasp, DAVINCI_MCASP_GBLCTL_REG);

	// release frame sync
	dev_info(mcasp->dev, "release frame sync generators");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RFSRST);
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XFSRST);
	REG_DUMP(mcasp, DAVINCI_MCASP_GBLCTL_REG);

	dev_info(mcasp->dev, "Initalization finished");
	REG_DUMP(mcasp, DAVINCI_MCASP_GBLCTL_REG);
	REG_DUMP(mcasp, DAVINCI_MCASP_RSTAT_REG);
	REG_DUMP(mcasp, DAVINCI_MCASP_XSTAT_REG);

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
	int clock_rate;

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

	clock_rate = clk_get_rate(mcasp->clk);
	dev_info(mcasp->dev, "Functional clock rate is %d Hz", clock_rate);

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