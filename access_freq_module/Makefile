obj-m   := memtis_access_freq.o
KDIR    := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: 
	$(MAKE) -C $(KDIR) M=${PWD} SUBDIRS=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=${PWD} SUBDIRS=$(PWD) clean