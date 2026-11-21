obj-m := it8612.o

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
VERSION := 0.1.0

.PHONY: all clean dkms-install dkms-remove

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

# Install with DKMS, so it is rebuilt for every new kernel (run as root).
dkms-install:
	mkdir -p /usr/src/it8612-$(VERSION)
	cp it8612.c Makefile dkms.conf /usr/src/it8612-$(VERSION)/
	dkms add -m it8612 -v $(VERSION)
	dkms install -m it8612 -v $(VERSION)

dkms-remove:
	dkms remove -m it8612 -v $(VERSION) --all
	rm -rf /usr/src/it8612-$(VERSION)
