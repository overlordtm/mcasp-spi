/*
 * Borrowed from davinci-mcasp.h
 *
 * ALSA SoC McASP Audio Layer for TI DAVINCI processor
 *
 * MCASP related definitions
 *
 * Author: Nirmal Pandey <n-pandey@ti.com>,
 *         Suresh Rajashekara <suresh.r@ti.com>
 *         Steve Chen <schen@.mvista.com>
 *
 * Copyright:   (C) 2009 MontaVista Software, Inc., <source@mvista.com>
 * Copyright:   (C) 2009  Texas Instruments, India
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DAVINCI_MCASP_H
#define DAVINCI_MCASP_H

/*
 * McASP register definitions
 */
#define DAVINCI_MCASP_REV_REG		0x00
#define DAVINCI_MCASP_PWRIDLESYSCONFIGT_REG	0x04

#define DAVINCI_MCASP_PFUNC_REG		0x10
#define DAVINCI_MCASP_PDIR_REG		0x14
#define DAVINCI_MCASP_PDOUT_REG		0x18
#define DAVINCI_MCASP_PDSET_REG		0x1c

#define DAVINCI_MCASP_PDCLR_REG		0x20

#define DAVINCI_MCASP_TLGC_REG		0x30
#define DAVINCI_MCASP_TLMR_REG		0x34

#define DAVINCI_MCASP_GBLCTL_REG	0x44
#define DAVINCI_MCASP_AMUTE_REG		0x48
#define DAVINCI_MCASP_DLBCTL_REG	0x4c

#define DAVINCI_MCASP_DITCTL_REG	0x50

#define DAVINCI_MCASP_RGBLCTL_REG	0x60
#define DAVINCI_MCASP_RMASK_REG		0x64
#define DAVINCI_MCASP_RFMT_REG		0x68
#define DAVINCI_MCASP_AFSRCTL_REG	0x6c

#define DAVINCI_MCASP_ACLKRCTL_REG	0x70
#define DAVINCI_MCASP_AHCLKRCTL_REG	0x74
#define DAVINCI_MCASP_RTDM_REG		0x78
#define DAVINCI_MCASP_RINTCTL_REG	0x7c

#define DAVINCI_MCASP_RSTAT_REG		0x80
#define DAVINCI_MCASP_RSLOT_REG		0x84
#define DAVINCI_MCASP_RCLKCHK_REG	0x88
#define DAVINCI_MCASP_REVTCTL_REG	0x8c

#define DAVINCI_MCASP_XGBLCTL_REG	0xa0
#define DAVINCI_MCASP_XMASK_REG		0xa4
#define DAVINCI_MCASP_XFMT_REG		0xa8
#define DAVINCI_MCASP_AFSXCTL_REG	0xac

#define DAVINCI_MCASP_ACLKXCTL_REG	0xb0
#define DAVINCI_MCASP_AHCLKXCTL_REG	0xb4
#define DAVINCI_MCASP_XTDM_REG		0xb8
#define DAVINCI_MCASP_XINTCTL_REG	0xbc

#define DAVINCI_MCASP_XSTAT_REG		0xc0
#define DAVINCI_MCASP_XSLOT_REG		0xc4
#define DAVINCI_MCASP_XCLKCHK_REG	0xc8
#define DAVINCI_MCASP_XEVTCTL_REG	0xcc

/* Left(even TDM Slot) Channel Status Register File */
#define DAVINCI_MCASP_DITCSRA_REG	0x100
/* Right(odd TDM slot) Channel Status Register File */
#define DAVINCI_MCASP_DITCSRB_REG	0x118
/* Left(even TDM slot) User Data Register File */
#define DAVINCI_MCASP_DITUDRA_REG	0x130
/* Right(odd TDM Slot) User Data Register File */
#define DAVINCI_MCASP_DITUDRB_REG	0x148

/* Serializer n Control Register */
#define DAVINCI_MCASP_SRCTL_BASE_REG	0x180
#define DAVINCI_MCASP_SRCTL_REG(n)	(DAVINCI_MCASP_SRCTL_BASE_REG + \
						(n << 2))

/* Transmit Buffer for Serializer n */
#define DAVINCI_MCASP_XBUF_REG(n)	(0x200 + (n << 2))
/* Receive Buffer for Serializer n */
#define DAVINCI_MCASP_RBUF_REG(n)	(0x280 + (n << 2))

/* McASP FIFO Registers */
#define DAVINCI_MCASP_V2_AFIFO_BASE	(0x1010)
#define DAVINCI_MCASP_V3_AFIFO_BASE	(0x1000)

/* FIFO register offsets from AFIFO base */
#define MCASP_WFIFOCTL_OFFSET		(0x0)
#define MCASP_WFIFOSTS_OFFSET		(0x4)
#define MCASP_RFIFOCTL_OFFSET		(0x8)
#define MCASP_RFIFOSTS_OFFSET		(0xc)

/*
 * DAVINCI_PWRIDLESYSCONFIGT_REG - Power Down and Emulation Management Register Bits
 */
#define MCASP_NOIDLE	BIT(0)
#define MCASP_SMARTIDLE	BIT(1)

/*
 * DAVINCI_MCASP_PFUNC_REG - Pin Function / GPIO Enable Register Bits
 */
#define PFUNC_AXR(n)	(1<<n)
#define PFUNC_AMUTE		BIT(25)
#define PFUNC_ACLKX		BIT(26)
#define PFUNC_AHCLKX	BIT(27)
#define PFUNC_AFSX		BIT(28)
#define PFUNC_ACLKR		BIT(29)
#define PFUNC_AHCLKR	BIT(30)
#define PFUNC_AFSR		BIT(31)

/*
 * DAVINCI_MCASP_PDIR_REG - Pin Direction Register Bits
 */
#define PDIR_AXR(n)		(1<<n)
#define PDIR_AMUTE		BIT(25)
#define PDIR_ACLKX		BIT(26)
#define PDIR_AHCLKX		BIT(27)
#define PDIR_AFSX		BIT(28)
#define PDIR_ACLKR		BIT(29)
#define PDIR_AHCLKR		BIT(30)
#define PDIR_AFSR		BIT(31)

/*
 * DAVINCI_MCASP_DITCTL_REG - Transmit DIT Control Register Bits
 */
#define DITEN	BIT(0)	/* Transmit DIT mode enable/disable */
#define VA		BIT(2)
#define VB		BIT(3)

/*
 * DAVINCI_MCASP_XFMT_REG - Transmit Bitstream Format Register Bits
 */
#define XROT(val)		(val)
#define XROT_MAKS		XROT(7)
#define XBUSEL			BIT(3)
#define XSSZ(val)		(val<<4)
#define XSSZ_MASK		XSSZ(0xF)
#define XPBIT(val)		(val<<8)
#define XPBIT_MASK		XPBIT(0x1F)
#define XPAD(val)		(val<<13)
#define XPAD_MASK		XPAD(3)
#define XRVRS			BIT(15)
#define XDATDLY(val)	(val<<16)
#define XDATDLY_MASK	XDATDLY(3)

/*
 * DAVINCI_MCASP_RFMT_REG - Receive Bitstream Format Register Bits
 */
#define RROT(val)		(val)
#define RROT_MASK		RROT(0x7)
#define RBUSEL			BIT(3)
#define RSSZ(val)		(val<<4)
#define RSSZ_MASK		RSSZ(0xF)
#define RPBIT(val)		(val<<8)
#define RPBIT_MASK		RPBIT(0x1F)
#define RPAD(val)		(val<<13)
#define RPAD_MASK		RPAD(3)
#define RRVRS			BIT(15)
#define RDATDLY(val)	(val<<16)
#define RDATDLY_MASK	RDATDLY(3)

/*
 * DAVINCI_MCASP_AFSXCTL_REG -  Transmit Frame Control Register Bits
 */
#define FSXP		BIT(0)
#define FSXM		BIT(1)
#define FXWID		BIT(4)
#define XMOD(val)	(val<<7)
#define XMOD_MASK	XMOD(0x1FF)

/*
 * DAVINCI_MCASP_AFSRCTL_REG - Receive Frame Control Register Bits
 */
#define FSRP		BIT(0)
#define FSRM		BIT(1)
#define FRWID		BIT(4)
#define RMOD(val)	(val<<7)
#define RMOD_MASK	RMOD(0x1FF)

/*
 * DAVINCI_MCASP_ACLKXCTL_REG - Transmit Clock Control Register Bits
 */
#define CLKXDIV(val)	(val)
#define CLKXM		BIT(5)
#define ASYNC		BIT(6)
#define CLKXP		BIT(7)
#define CLKXDIV_MASK	0x1f

/*
 * DAVINCI_MCASP_ACLKRCTL_REG Receive Clock Control Register Bits
 */
#define CLKRDIV(val)	(val)
#define CLKRM			BIT(5)
#define CLKRP			BIT(7)
#define CLKRDIV_MASK	0x1f

/*
 * DAVINCI_MCASP_AHCLKXCTL_REG - High Frequency Transmit Clock Control
 *     Register Bits
 */
#define HCLKXDIV(val)	(val)
#define HCLKXP		BIT(14)
#define HCLKXM		BIT(15)
#define HCLKXDIV_MASK	0xfff

/*
 * DAVINCI_MCASP_AHCLKRCTL_REG - High Frequency Receive Clock Control
 *     Register Bits
 */
#define HCLKRDIV(val)	(val)
#define HCLKRP		BIT(14)
#define HCLKRM		BIT(15)
#define HCLKRDIV_MASK	0xfff

/*
 * DAVINCI_MCASP_SRCTL_BASE_REG -  Serializer Control Register Bits
 */
#define MODE(val)	(val)
#define DISMOD_3STATE	(0x0)
#define DISMOD_LOW	(0x2 << 2)
#define DISMOD_HIGH	(0x3 << 2)
#define DISMOD_MASK	DISMOD_HIGH
#define TXSTATE		BIT(4)
#define RXSTATE		BIT(5)
#define SRMOD_MASK	3
#define SRMOD_INACTIVE	0
#define SRMOD_TX		1
#define SRMOD_RX		2
/*
 * DAVINCI_MCASP_DLBCTL_REG - Loop Back Control Register Bits
 */
#define DLBEN		BIT(0)
#define DLBORD		BIT(1)
#define DLBMODE(val)	(val<<2)
#define DLBMODE_MASK 0xC

/*
 * DAVINCI_MCASP_XSLOT_REG - Transmit TDM Slot Register configuration
 */
#define XTDMS(n)	(1<<n)

/*
 * DAVINCI_MCASP_RSLOT_REG - Receive TDM Slot Register configuration
 */
#define RTDMS(n)	(1<<n)

/*
 * DAVINCI_MCASP_GBLCTL_REG -  Global Control Register Bits
 */
#define RCLKRST		BIT(0)	/* Receiver Clock Divider Reset */
#define RHCLKRST	BIT(1)	/* Receiver High Frequency Clock Divider */
#define RSRCLR		BIT(2)	/* Receiver Serializer Clear */
#define RSMRST		BIT(3)	/* Receiver State Machine Reset */
#define RFSRST		BIT(4)	/* Frame Sync Generator Reset */
#define XCLKRST		BIT(8)	/* Transmitter Clock Divider Reset */
#define XHCLKRST	BIT(9)	/* Transmitter High Frequency Clock Divider*/
#define XSRCLR		BIT(10)	/* Transmit Serializer Clear */
#define XSMRST		BIT(11)	/* Transmitter State Machine Reset */
#define XFSRST		BIT(12)	/* Frame Sync Generator Reset */

/*
 * DAVINCI_MCASP_XSTAT_REG - Transmitter Status Register Bits
 * DAVINCI_MCASP_RSTAT_REG - Receiver Status Register Bits
 */
#define XRERR		BIT(8) /* Transmit/Receive error */
#define XRDATA		BIT(5) /* Transmit/Receive data ready */

/*
 * DAVINCI_MCASP_AMUTE_REG -  Mute Control Register Bits
 */
#define MUTENA(val)	(val)
#define MUTEINPOL	BIT(2)
#define MUTEINENA	BIT(3)
#define MUTEIN		BIT(4)
#define MUTER		BIT(5)
#define MUTEX		BIT(6)
#define MUTEFSR		BIT(7)
#define MUTEFSX		BIT(8)
#define MUTEBADCLKR	BIT(9)
#define MUTEBADCLKX	BIT(10)
#define MUTERXDMAERR	BIT(11)
#define MUTETXDMAERR	BIT(12)

/*
 * DAVINCI_MCASP_REVTCTL_REG - Receiver DMA Event Control Register bits
 */
#define RXDATADMADIS	BIT(0)

/*
 * DAVINCI_MCASP_XEVTCTL_REG - Transmitter DMA Event Control Register bits
 */
#define TXDATADMADIS	BIT(0)

/*
 * DAVINCI_MCASP_RINTCTL_REG - Receiver Interrupt Control Register Bits
 */
#define RSTAFRM		BIT(7)
#define RDATA		BIT(5)
#define RLAST		BIT(4)
#define RDMAERR		BIT(3)
#define RCKFAIL		BIT(2)
#define RSYNCERR	BIT(1)
#define ROVRN		BIT(0)

/*
 * DAVINCI_MCASP_XINTCTL_REG - Transmitter Interrupt Control Register Bits
 */
#define XSTAFRM		BIT(7)
#define XDATA		BIT(5)
#define XLAST		BIT(4)
#define XDMAERR		BIT(3)
#define XCKFAIL		BIT(2)
#define XSYNCERR	BIT(1)
#define XUNDRN		BIT(0)

/*
 * DAVINCI_MCASP_W[R]FIFOCTL - Write/Read FIFO Control Register bits
 */
#define FIFO_ENABLE	BIT(16)
#define NUMEVT_MASK	(0xFF << 8)
#define NUMEVT(x)	(((x) & 0xFF) << 8)
#define NUMDMA_MASK	(0xFF)

/* clock divider IDs */
#define MCASP_CLKDIV_AUXCLK		0 /* HCLK divider from AUXCLK */
#define MCASP_CLKDIV_BCLK		1 /* BCLK divider from HCLK */
#define MCASP_CLKDIV_BCLK_FS_RATIO	2 /* to set BCLK FS ration */

#endif	/* DAVINCI_MCASP_H */
