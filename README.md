## mcasp-spi

Attemp to create general purpose driver for McASP on beaglebone black.


## Custom device tree

Maybe I just dont know better, but I had to modift device tree a bit :)

Modified device tree is `am335x-boneblack-mcasp0.dts`.

I do not know how to compile it manually, so just copy it to `arch/arm/boot/dts` in kernel sources and run

```
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- am335x-boneblack-mcasp0.dtb
```

Then copy the am335x-boneblack-mcasp0.dtb to `/boot/dtbs` on device and modify `dtb` variable in `uEnv.txt`


## Compiling kernel module

Not much to say here, standard prcedure

```
make KERNEL=/path/to/kernel/sources
```

## McASP init procedure (from AM335x reference manual)

1. Reset McASP to default values by setting GBLCTL = 0.

2. Configure all McASP registers except GBLCTL in the following order:

    A. Power Idle SYSCONFIG: PWRIDLESYSCONFIG.

    B. Receive registers: RMASK, RFMT, AFSRCTL, ACLKRCTL, AHCLKRCTL, RTDM, RINTCTL,
    RCLKCHK. If external clocks AHCLKR and/or ACLKR are used, they must be running already for
    proper synchronization of the GBLCTL register.

    C. Transmit registers: XMASK, XFMT, AFSXCTL, ACLKXCTL, AHCLKXCTL, XTDM, XINTCTL,
    XCLKCHK. If external clocks AHCLKX and/or ACLKX are used, they must be running already for
    proper synchronization of the GBLCTL register.

    D. Serializer registers: SRCTL[n].

    E. Global registers: Registers PFUNC, PDIR, DITCTL, DLBCTL, AMUTE. Note that PDIR should only
    be programmed after the clocks and frames are set up in the steps above. This is because the
    moment a clock pin is configured as an output in PDIR, the clock pin starts toggling at the rate
    defined in the corresponding clock control register. Therefore you must ensure that the clock control
    register is configured appropriately before you set the pin to be an output. A similar argument
    applies to the frame sync pins. Also note that the reset state for the transmit high-frequency clock
    divide register (HCLKXDIV) is divide-by-1, and the divide-by-1 clocks are not gated by the transmit
    high-frequency clock divider reset enable (XHCLKRST).

    F. DIT registers: For DIT mode operation, set up registers DITCSRA[n], DITCSRB[n], DITUDRA[n],
    and DITUDRB[n].

3. Start the respective high-frequency serial clocks AHCLKX and/or AHCLKR. This step is necessary
    even if external high-frequency serial clocks are used:

    A. Take the respective internal high-frequency serial clock divider(s) out of reset by setting the
    RHCLKRST bit for the receiver and/or the XHCLKRST bit for the transmitter in GBLCTL. All other
    bits in GBLCTL should be held at 0.

    B. Read back from GBLCTL to ensure the bit(s) to which you wrote are successfully latched in
    GBLCTL before you proceed.

4.  Start the respective serial clocks ACLKX and/or ACLKR. This step can be skipped if external serial
    clocks are used and they are running:

    A. Take the respective internal serial clock divider(s) out of reset by setting the RCLKRST bit for the
    receiver and/or the XCLKRST bit for the transmitter in GBLCTL. All other bits in GBLCTL should
    be left at the previous state.

    B. Read back from GBLCTL to ensure the bit(s) to which you wrote are successfully latched in
    GBLCTL before you proceed.

5. Setup data acquisition as required:

    A. If DMA is used to service the McASP, set up data acquisition as desired and start the DMA in this
    step, before the McASP is taken out of reset.

    B. If CPU interrupt is used to service the McASP, enable the transmit and/ or receive interrupt as
    required.

    C. If CPU polling is used to service the McASP, no action is required in this step.

6. Activate serializers.

    A. Before starting, clear the respective transmitter and receiver status registers by writing
    XSTAT = FFFFh and RSTAT = FFFFh.

    B. Take the respective serializers out of reset by setting the RSRCLR bit for the receiver and/or the
    XSRCLR bit for the transmitter in GBLCTL. All other bits in GBLCTL should be left at the previous
    state.

    C. Read back from GBLCTL to ensure the bit(s) to which you wrote are successfully latched in
    GBLCTL before you proceed.

7. Verify that all transmit buffers are serviced. Skip this step if the transmitter is not used. Also, skip this
    step if time slot 0 is selected as inactive (special cases, see Figure 22-21, second waveform). As soon
    as the transmit serializer is taken out of reset, XDATA in the XSTAT register is set, indicating that
    XBUF is empty and ready to be serviced. The XDATA status causes an DMA event AXEVT to be
    generated, and can cause an interrupt AXINT to be generated if it is enabled in the XINTCTL register.

    A. If DMA is used to service the McASP, the DMA automatically services the McASP upon receiving
    AXEVT. Before proceeding in this step, you should verify that the XDATA bit in the XSTAT is
    cleared to 0, indicating that all transmit buffers are already serviced by the DMA.

    B. If CPU interrupt is used to service the McASP, interrupt service routine is entered upon the AXINT
    interrupt. The interrupt service routine should service the XBUF registers. Before proceeding in this
    step, you should verify that the XDATA bit in XSTAT is cleared to 0, indicating that all transmit
    buffers are already serviced by the CPU.

    C. If CPU polling is used to service the McASP, the XBUF registers should be written to in this step

8. Release state machines from reset.

    A. Take the respective state machine(s) out of reset by setting the RSMRST bit for the receiver and/or
    the XSMRST bit for the transmitter in GBLCTL. All other bits in GBLCTL should be left at the
    previous state.

    B. Read back from GBLCTL to ensure the bit(s) to which you wrote are successfully latched in
    GBLCTL before you proceed.

9. Release frame sync generators from reset. Note that it is necessary to release the internal frame sync
    generators from reset, even if an external frame sync is being used, because the frame sync error
    detection logic is built into the frame sync generator.

    A. Take the respective frame sync generator(s) out of reset by setting the RFRST bit for the receiver,
    and/or the XFRST bit for the transmitter in GBLCTL. All other bits in GBLCTL should be left at the
    previous state.

    B. Read back from GBLCTL to ensure the bit(s) to which you wrote are successfully latched in
    GBLCTL before you proceed.

10. Upon the first frame sync signal, McASP transfers begin. The McASP synchronizes to an edge on the
    frame sync pin, not the level on the frame sync pin. This makes it easy to release the state machine
    and frame sync generators from reset.

    A. For example, if you configure the McASP for a rising edge transmit frame sync, then you do not
    need to wait for a low level on the frame sync pin before releasing the McASP transmitter state
    machine and frame sync generators from reset.