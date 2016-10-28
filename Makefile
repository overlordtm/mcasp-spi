obj-m += mcaspdrv.o

UNAME := $(shell uname -a)

KERNEL ?= ../linux

ifneq (,$(findstring beaglebone, $(UNAME)))
	KERNEL := /usr/src/linux-headers-$(shell uname -r)
endif

export ARCH ?= arm
export CROSS_COMPILE=arm-linux-gnueabihf-

all: clean build try

build:
	$(MAKE) -C $(KERNEL) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL) M=$(PWD) clean

transfer:
	scp Makefile mcasp.h mcaspdrv.c am335x-boneblack-mcasp0.dts root@192.168.7.2:~/mcasp

try: rmmod insmod

rmmod:
	rmmod mcaspdrv.ko || true

insmod:
	insmod mcaspdrv.ko

lsmod:
	lsmod | grep mcasp