obj-m += homesw.o
tmc5160-objs := homesw.o

KDIR := /home/sunbeam/bbb/linux/
PWD  := $(shell pwd)

modules:
	make -C $(KDIR) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules

clean:
	make -C $(KDIR) M=$(PWD) clean

.PHONY: modules clean
