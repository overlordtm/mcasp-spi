obj-m += mcaspdrv.o

UNAME := $(shell uname -a)

KERNEL ?= ../linux

ifneq (,$(findstring beaglebone, $(UNAME)))
	KERNEL := /usr/src/linux-headers-$(shell uname -r)
endif

export ARCH ?= arm
export CROSS_COMPILE=arm-linux-gnueabihf-

all:
	$(MAKE) -C $(KERNEL) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL) M=$(PWD) clean

transfer:
	scp Makefile mcasp.h mcaspdrv.c am335x-boneblack-mcasp0.dts debian@192.168.7.2:~/mcasp

try: rmmod insmod lsmod

rmmod:
	rmmod mcaspdrv.ko

insmod:
	insmod mcaspdrv.ko

lsmod:
	lsmod | grep mcasp