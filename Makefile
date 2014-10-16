obj-m += st7565.o
KVER := $(shell uname -r)
KSRC ?= /lib/modules/$(KVER)/build
SUBARCH := $(shell uname -m | sed -e s/i.86/i386/ | sed -e s/ppc/powerpc/ | sed -e s/armv.l/arm/)
ARCH ?= $(SUBARCH)

all:
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC) M=`pwd` modules
clean:
	make -C $(KSRC) M=`pwd` clean