
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
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/platform_data/davinci_asp.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/circ_buf.h>
#include <net/netlink.h>
#include <net/genetlink.h>


#include "mcasp.h"

#define AXRNTX		0
#define AXRNRX		1
#define TDM_SLOTS	8

#define MCASP_DEBUG
// #define MCASP_REG_DEBUG

#define MCASP_DEBUG_IRQTX
#define MCASP_DEBUG_IRQRX

#define REG_DUMP_FORCE(MCASP, REG) dev_info(MCASP->dev, #REG " is 0x%08X", mcasp_get_reg(MCASP, REG));

#ifdef MCASP_REG_DEBUG
	#define REG_DUMP(MCASP, REG) REG_DUMP_FORCE(MCASP, REG)
#else
	#define REG_DUMP(MCASP, REG) /* noop */
#endif // MCASP_REG_DEBUG

static const struct of_device_id mcasp_dt_ids[] = {
	{
		.compatible = "ti,am33xx-mcasp-serial",
	},
	{},
};
MODULE_DEVICE_TABLE(of, mcasp_dt_ids);

struct davinci_mcasp {
	void __iomem *base;
	struct device *dev;
	struct clk *clk;

	struct circ_buf tx_buf;
	struct circ_buf rx_buf;

	struct cdev cdev;

	int majorNum;

	u32 revision;
};

static int mcasp_start(struct davinci_mcasp *);
static int mcasp_start_tx(struct davinci_mcasp *);
static int mcasp_start_rx(struct davinci_mcasp *);
static int mcasp_stop(struct davinci_mcasp *);
static int mcasp_stop_tx(struct davinci_mcasp *);
static int mcasp_stop_rx(struct davinci_mcasp *);

static int mcasp_dev_open(struct inode *ino, struct file *filep) {
	struct davinci_mcasp *mcasp = container_of(ino->i_cdev, struct davinci_mcasp, cdev);
	filep->private_data = mcasp;

	mcasp_start(mcasp);

	return 0;
}

static int mcasp_dev_release(struct inode *ino, struct file *filep) {

	struct davinci_mcasp *mcasp = container_of(ino->i_cdev, struct davinci_mcasp, cdev);

	mcasp_stop(mcasp);

	return 0;
}

static ssize_t mcasp_dev_read(struct file *filep, char __user *buf, size_t length, loff_t *offset) {
	struct davinci_mcasp *mcasp = filep->private_data;
	u32 val =  0;
	ssize_t retval = 0;

	dev_info(mcasp->dev, "Read request on char dev. length:%zu offset:%lld head:%d tail:%d", length, *offset, mcasp->rx_buf.head, mcasp->rx_buf.tail);

	// consumer for rx buf
	if(CIRC_CNT(mcasp->rx_buf.head, mcasp->rx_buf.tail, MCASP_RX_BUF_SIZE) > 0) {
		val = mcasp->rx_buf.buf[mcasp->rx_buf.tail];
		mcasp->rx_buf.tail = (mcasp->rx_buf.tail + sizeof(u32)) & (MCASP_RX_BUF_SIZE - 1);
		dev_info(mcasp->dev, "Device read %x", val);

		if(copy_to_user(buf, &val, sizeof(u32))) {
			retval = -EFAULT;
		}

		retval = length;
	} else {
		dev_warn(mcasp->dev, "Cannot read, empty buffer head:%d, tail:%d", mcasp->rx_buf.head, mcasp->rx_buf.tail);
	}


	return retval;
}

static ssize_t mcasp_dev_write(struct file *filep, const char *buf, size_t length, loff_t *offset) {
	struct davinci_mcasp *mcasp = filep->private_data;
	ssize_t retval = 1;
	u32 val;

	dev_info(mcasp->dev, "Write request on char dev. length:%zu offset:%lld head:%d tail:%d", length, *offset, mcasp->tx_buf.head, mcasp->tx_buf.tail);

	// producer for tx buff
	if(CIRC_SPACE(mcasp->tx_buf.head, mcasp->tx_buf.tail, MCASP_TX_BUF_SIZE) > 0) {

		if(copy_from_user(&val, buf, sizeof(u32))) {
			retval = -EFAULT;
		}

		mcasp->tx_buf.buf[mcasp->tx_buf.head] = val;
		mcasp->tx_buf.head = (mcasp->tx_buf.head + sizeof(u32)) & (MCASP_TX_BUF_SIZE - 1);
		dev_info(mcasp->dev, "Wrote to tx_buf val:%x head:%d tail:%d", val, mcasp->tx_buf.head, mcasp->tx_buf.tail);
		retval = length;
	} else {
		dev_warn(mcasp->dev, "Cannot write, buffer full head:%d, tail:%d", mcasp->tx_buf.head, mcasp->tx_buf.tail);
	}

	return retval;
}

/*
 * Create a set of file operations for our proc files.
 */
static struct file_operations mcasp_file_ops = {
	.owner   = THIS_MODULE,
	.open    = mcasp_dev_open,
	.write   = mcasp_dev_write,
	.read    = mcasp_dev_read,
	.release = mcasp_dev_release,
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

static irqreturn_t mcasp_tx_irq_handler(int irq, void *data)
{
	struct davinci_mcasp *mcasp = (struct davinci_mcasp *)data;
	u32 handled_mask = 0;
	u32 stat;
	u32 val = 0xABCD1234;

	stat = mcasp_get_reg(mcasp, DAVINCI_MCASP_XSTAT_REG);

	// consumer for tx buf
	if (stat & XDATA) {
		// if(CIRC_CNT(mcasp->tx_buf.head, mcasp->tx_buf.tail, MCASP_TX_BUF_SIZE) > 0) {
		// 	val = mcasp->tx_buf.buf[mcasp->tx_buf.tail];
		// 	mcasp->tx_buf.tail = (mcasp->tx_buf.tail + sizeof(u32)) & (MCASP_TX_BUF_SIZE - 1);
		// }
		mcasp_set_reg(mcasp, DAVINCI_MCASP_XBUF_REG(AXRNTX), val);
		handled_mask |= XDATA;
	}

	if (stat & XUNDRN) {
		dev_err_ratelimited(mcasp->dev, "Transmit buffer underflow XUNDRN");
		dev_info(mcasp->dev, "Transmit buffer underflow XUNDRN");
		mcasp_stop_tx(mcasp);
		handled_mask |= XUNDRN;
	}

	if (stat & XRERR) {
		handled_mask |= XRERR;
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

	stat = mcasp_get_reg(mcasp, DAVINCI_MCASP_RSTAT_REG);

	// producer for rx buf
	if (stat & RDATA) {
		val = mcasp_get_reg(mcasp, DAVINCI_MCASP_RBUF_REG(AXRNRX));
		// if(CIRC_SPACE(mcasp->rx_buf.head, mcasp->rx_buf.tail, MCASP_TX_BUF_SIZE) > 0) {
		// 	mcasp->rx_buf.buf[mcasp->rx_buf.head] = val;
		// 	mcasp->rx_buf.head = (mcasp->rx_buf.head + sizeof(u32)) & (MCASP_TX_BUF_SIZE - 1);
		// } else {
		// 	dev_alert(mcasp->dev, "rx_buf overflow");
		// }
		handled_mask |= RDATA;
	}

	if (stat & ROVRN) {
		dev_err_ratelimited(mcasp->dev, "Receive buffer overflow ROVRN");
		dev_info(mcasp->dev, "Receive buffer overflow ROVRN");
		mcasp_stop_rx(mcasp);
		handled_mask |= ROVRN;
	}

	if (stat & XRERR) {
		handled_mask |= XRERR;
	}

	/* Ack the handled event only */
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RSTAT_REG, handled_mask);

	return IRQ_RETVAL(handled_mask);
}

static void mcasp_rx_init(struct davinci_mcasp *mcasp) {

	// mask bits
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RMASK_REG, 0xFF);
	REG_DUMP(mcasp, DAVINCI_MCASP_RMASK_REG);

	// format bits
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RBUSEL | RRVRS);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RROT(0), RROT_MASK);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RSSZ(0x7), RSSZ_MASK);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RPAD(0), RPAD_MASK);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_RFMT_REG, RDATDLY(0x1), RDATDLY_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_RFMT_REG);

	// frame sync
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, FSRP | FSRM);
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, FRWID);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AFSRCTL_REG, RMOD(TDM_SLOTS), RMOD_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_AFSRCTL_REG);

	// bit clock setup
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, CLKRM | CLKRP);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_ACLKRCTL_REG, CLKRDIV(2), CLKRDIV_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_ACLKRCTL_REG);

	// high clock
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, HCLKRM | HCLKRP);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG, HCLKRDIV(7), HCLKRDIV_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_AHCLKRCTL_REG);

	// ROVRN interrupt eanble
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RINTCTL_REG, ROVRN);
	REG_DUMP(mcasp, DAVINCI_MCASP_RINTCTL_REG);

	// clock check
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RCLKCHK_REG, 0x00FF0003);
	REG_DUMP(mcasp, DAVINCI_MCASP_RCLKCHK_REG);

	// set TDM
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RTDM_REG, 0x8);
	REG_DUMP(mcasp, DAVINCI_MCASP_RTDM_REG);

	return;
}

static void mcasp_tx_init(struct davinci_mcasp *mcasp) {

	// mask
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XMASK_REG, 0xFF);
	REG_DUMP(mcasp, DAVINCI_MCASP_XMASK_REG);

	// format
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XBUSEL | XRVRS);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XROT(0), XROT_MAKS);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XSSZ(0x7), XSSZ_MASK);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XPAD(0), XPAD_MASK);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_XFMT_REG, XDATDLY(0x1), XDATDLY_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_XFMT_REG);

	// frame sync
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, FSXP | FSXM);
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, FXWID);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AFSXCTL_REG, XMOD(TDM_SLOTS), XMOD_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_AFSXCTL_REG);

	// clock internal
	mcasp_set_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, CLKXM | CLKXP);
	// sync clock, TX provides RX clock
	mcasp_clr_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, ASYNC);
	// clock
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_ACLKXCTL_REG, CLKXDIV(2), CLKXDIV_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_ACLKXCTL_REG);

	// high clock
	mcasp_set_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, HCLKXM | HCLKXP);
	mcasp_mod_bits(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG, HCLKXDIV(7), HCLKXDIV_MASK);
	REG_DUMP(mcasp, DAVINCI_MCASP_AHCLKXCTL_REG);

	// XUNDRN interrupt eanble
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XINTCTL_REG, XUNDRN);
	REG_DUMP(mcasp,DAVINCI_MCASP_XINTCTL_REG);

	// set clock check
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XCLKCHK_REG, 0x00FF0003);
	REG_DUMP(mcasp, DAVINCI_MCASP_XCLKCHK_REG);

	// set TDM
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XTDM_REG, 0x8);
	REG_DUMP(mcasp, DAVINCI_MCASP_XTDM_REG);

	return;
}

static int mcasp_hw_init(struct davinci_mcasp *mcasp) {
	pm_runtime_get_sync(mcasp->dev);

	mcasp->revision = mcasp_get_reg(mcasp, DAVINCI_MCASP_REV_REG);
	REG_DUMP(mcasp, DAVINCI_MCASP_REV_REG);

	dev_info(mcasp->dev, "Starting intialization.");
	mcasp_set_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, 0x0);
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

	// clear receive status register
	dev_info(mcasp->dev, "Clearing RSTAT register");
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RSTAT_REG, 0xFFFF);
	REG_DUMP(mcasp, DAVINCI_MCASP_RSTAT_REG);

	// clear transmit status register
	dev_info(mcasp->dev, "Clearing XSTAT register");
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XSTAT_REG, 0xFFFF);
	REG_DUMP(mcasp, DAVINCI_MCASP_XSTAT_REG);

	dev_info(mcasp->dev, "Initalization finished");
	REG_DUMP(mcasp, DAVINCI_MCASP_GBLCTL_REG);
	REG_DUMP(mcasp, DAVINCI_MCASP_RSTAT_REG);
	REG_DUMP(mcasp, DAVINCI_MCASP_XSTAT_REG);

	pm_runtime_put(mcasp->dev);

	return 0;
}

static int mcasp_sw_init(struct davinci_mcasp *mcasp) {
	unsigned long tx_page, rx_page;
	int retval, err = 0;
	dev_t chrdev = 0;

	tx_page = get_zeroed_page(GFP_KERNEL);
	if (!tx_page) {
		retval =  -ENOMEM;
		goto err;
	}

	rx_page = get_zeroed_page(GFP_KERNEL);
	if (!rx_page) {
		retval =  -ENOMEM;
		goto err;
	}

	if(mcasp->tx_buf.buf) {
		free_page(tx_page);
	} else {
		mcasp->tx_buf.buf = (unsigned char *) tx_page;
	}

	if(mcasp->rx_buf.buf) {
		free_page(rx_page);
	} else {
		mcasp->rx_buf.buf = (unsigned char *) rx_page;
	}

	mcasp->tx_buf.head = mcasp->tx_buf.tail = 0;
	mcasp->rx_buf.head = mcasp->rx_buf.tail = 0;

	// alloc_chrdev_region — register a range of char device numbers
	err = alloc_chrdev_region(&chrdev, 0, 1, MCASP_DEVICE_NAME);
	if (err < 0) {
		dev_alert(mcasp->dev, "failed to register a majon number");
		return err;
	}

	mcasp->majorNum = MAJOR(chrdev);

	cdev_init(&mcasp->cdev, &mcasp_file_ops);
	mcasp->cdev.owner = THIS_MODULE;
	mcasp->cdev.ops = &mcasp_file_ops;

	err = cdev_add(&mcasp->cdev, chrdev, 1);
	if(err) {
		dev_alert(mcasp->dev, "cdev_add failed %d", err);
		return err;
	}

	dev_info(mcasp->dev, "registred device %d", mcasp->majorNum);
	dev_info(mcasp->dev, "mknod /dev/mcasp c %d 0", mcasp->majorNum);

	return retval;

// err2:
	// class_destroy(mcasp->class);

err:
	unregister_chrdev(mcasp->majorNum, MCASP_DEVICE_NAME);

	return retval;
}

static int mcasp_start_tx(struct davinci_mcasp *mcasp) {
	int cnt;

	mcasp->tx_buf.head = mcasp->tx_buf.tail = 0;

	pm_runtime_get_sync(mcasp->dev);

	dev_info(mcasp->dev, "Starting high freq TX clock");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XHCLKRST);

	dev_info(mcasp->dev, "Starting serial TX clock");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XCLKRST);

	dev_info(mcasp->dev, "Starting TX serializers");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XSRCLR);

	cnt = 0;
	while ((mcasp_get_reg(mcasp, DAVINCI_MCASP_XSTAT_REG) & XRDATA) && (cnt < 100000))
		cnt++;

	dev_info(mcasp->dev, "Resetting TX state machine");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XSMRST);

	dev_info(mcasp->dev, "Starting TX frame sync");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, XFSRST);

	dev_info(mcasp->dev, "Enabling XDATA interrupt");
	mcasp_set_bits(mcasp, DAVINCI_MCASP_XINTCTL_REG, XDATA);

	REG_DUMP_FORCE(mcasp, DAVINCI_MCASP_XSTAT_REG);

	pm_runtime_put(mcasp->dev);
	return 0;
}

static int mcasp_start_rx(struct davinci_mcasp *mcasp) {

	mcasp->rx_buf.head = mcasp->rx_buf.tail = 0;

	pm_runtime_get_sync(mcasp->dev);

	dev_info(mcasp->dev, "Starting high freq RX clock");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RHCLKRST);

	dev_info(mcasp->dev, "Starting serial RX clock");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RCLKRST);

	dev_info(mcasp->dev, "Starting RX serializers");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RSRCLR);

	dev_info(mcasp->dev, "Resetting RX state machine");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RSMRST);

	dev_info(mcasp->dev, "Starting RX frame sync");
	mcasp_set_ctl_reg(mcasp, DAVINCI_MCASP_GBLCTL_REG, RFSRST);

	dev_info(mcasp->dev, "Enabling RDATA interrupt");
	mcasp_set_bits(mcasp, DAVINCI_MCASP_RINTCTL_REG, RDATA);

	REG_DUMP_FORCE(mcasp, DAVINCI_MCASP_RSTAT_REG);

	pm_runtime_put(mcasp->dev);
	return 0;
}

static int mcasp_start(struct davinci_mcasp *mcasp) {

	dev_info(mcasp->dev, "Starting McASP");
	mcasp_start_rx(mcasp);
	mcasp_start_tx(mcasp);

	return 0;
}

static int mcasp_stop_tx(struct davinci_mcasp *mcasp) {
	pm_runtime_get_sync(mcasp->dev);

	dev_info(mcasp->dev, "Stopping McASP TX unit");
	REG_DUMP_FORCE(mcasp, DAVINCI_MCASP_XSTAT_REG);

	mcasp_set_reg(mcasp, DAVINCI_MCASP_XGBLCTL_REG, 0x0);
	mcasp_set_reg(mcasp, DAVINCI_MCASP_XSTAT_REG, 0xFFFF);
	REG_DUMP_FORCE(mcasp, DAVINCI_MCASP_XSTAT_REG);

	pm_runtime_put(mcasp->dev);

	return 0;
}

static int mcasp_stop_rx(struct davinci_mcasp *mcasp) {
	pm_runtime_get_sync(mcasp->dev);

	dev_info(mcasp->dev, "Stopping McASP RX unit");
	REG_DUMP_FORCE(mcasp, DAVINCI_MCASP_RSTAT_REG);

	mcasp_set_reg(mcasp, DAVINCI_MCASP_RGBLCTL_REG, 0x0);
	mcasp_set_reg(mcasp, DAVINCI_MCASP_RSTAT_REG, 0xFFFF);
	REG_DUMP_FORCE(mcasp, DAVINCI_MCASP_RSTAT_REG);

	pm_runtime_put(mcasp->dev);

	return 0;
}

static int mcasp_stop(struct davinci_mcasp *mcasp) {

	dev_info(mcasp->dev, "Stopping McASP");
	mcasp_stop_tx(mcasp);
	mcasp_stop_rx(mcasp);

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
		ret = devm_request_threaded_irq(&pdev->dev, irq, mcasp_tx_irq_handler, NULL, IRQF_ONESHOT | IRQF_SHARED, irq_name, mcasp);

		if (ret) {
			dev_err(&pdev->dev, "TX IRQ request failed\n");
			goto err;
		}
	}

	irq = platform_get_irq_byname(pdev, "rx");
	if (irq >= 0) {
		irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_rx", dev_name(&pdev->dev));
		ret = devm_request_threaded_irq(&pdev->dev, irq, mcasp_rx_irq_handler, NULL, IRQF_ONESHOT | IRQF_SHARED, irq_name, mcasp);

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

	mcasp_sw_init(mcasp);
	mcasp_hw_init(mcasp);
	mcasp_start(mcasp);

	return 0;

err:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int mcaspspi_remove(struct platform_device *pdev)
{
	struct davinci_mcasp *mcasp = dev_get_drvdata(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	if (mcasp->tx_buf.buf)
		free_page((long unsigned int) mcasp->tx_buf.buf);

	if (mcasp->rx_buf.buf)
		free_page((long unsigned int) mcasp->rx_buf.buf);

	cdev_del(&mcasp->cdev);

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