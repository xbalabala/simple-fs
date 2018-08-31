obj-m += aufs.o

build:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	# insmod
	sudo insmod aufs.ko
	# mount
	sudo mkdir -p /au
	sudo mount -t aufs none /au
	tree /au

uninstall:
	# umount
	sudo umount -t aufs /au
	sudo rm -r /au
	# rmmod
	sudo rmmod -f aufs

dmesg:
	sudo dmesg

dmesg-clear:
	sudo dmesg -C

test: dmesg-clear install dmesg uninstall

lsmod:
	lsmod | grep aufs

.PHONY: build clean install uninstall dmesg-clear dmesg test lsmod
