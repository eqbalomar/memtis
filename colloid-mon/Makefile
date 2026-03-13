obj-m   := nucleus-mon.o
KDIR    := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

nucleus-mon-objs := colloid-mon.o translation-latency.o
  
all: 
		$(MAKE) -C $(KDIR) M=${PWD} SUBDIRS=$(PWD) modules

clean:
		$(MAKE) -C $(KDIR) M=${PWD} SUBDIRS=$(PWD) clean