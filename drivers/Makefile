KERNELDIR := /home/lyp/Codes/I.MX6ULL/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
CURRENT_PATH := $(shell pwd)

obj-m := key.o gt1151.o

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
	find $(CURRENT_PATH) -type f \( -name "*.o" -o -name "*.mod.c" -o -name "*.mod.o" -o -name "*.order" -o -name ".*.cmd" -o -name "Module.symvers" \) -delete
	rm -rf ".tmp_versions"
clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean
