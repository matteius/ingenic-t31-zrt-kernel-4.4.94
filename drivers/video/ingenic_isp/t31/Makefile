# Include paths and linker options
ccflags-y += -I$(srctree)/drivers/video/ingenic_isp/t31/include

# Module and objects
obj-$(CONFIG_INGENIC_ISP) += tx-isp-t31.o
tx-isp-t31-objs := tx-isp-module.o tx-isp-debug.o libt31-firmware.a

# Build tx-isp-debug.o
tx-isp-debug.o: $(srctree)/drivers/video/ingenic_isp/t31/tx-isp-debug.c
	$(CC) $(c_flags) -c -o $@ $<
