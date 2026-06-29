obj-m := digilent-hdmi.o

ccflags-y += -Wno-error=all -Wno-incompatible-pointer-types -Wno-missing-prototypes -Wno-implicit-function-declaration

SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
