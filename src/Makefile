obj-m += membuf.o

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) clean

install:
	make -C $(KERNEL_DIR) M=$(PWD) modules_install
