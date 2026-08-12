obj-m += tmc5160.o
tmc5160-objs := tmc5160_cdev.o tmc5160_motion.o tmc5160_hw.o 

KDIR := /home/sunbeam/bbb/linux/
PWD  := $(shell pwd)

modules:
	make -C $(KDIR) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules

clean:
	make -C $(KDIR) M=$(PWD) clean

.PHONY: modules clean
