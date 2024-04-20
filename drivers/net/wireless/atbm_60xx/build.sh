## STANDALONE BUILD

KERNEL_DIR="/home/matteius/ingenic-t31-zrt-kernel-4.4.94-matteius/"
CROSS_COMPILE_TOOLCHAIN="/home/matteius/output/toolchain_xburst1_musl_gcc13/host/bin/mipsel-linux-"

if [[ "$KERNEL_DIR" == "" ]]; then
	echo "Please edit this file and specify your kernel sources directory and toolchain"
	exit 1
fi

/usr/bin/make -j6 \
-C $KERNEL_DIR \
ARCH=mips \
LOCALVERSION= \
CROSS_COMPILE=$CROSS_COMPILE_TOOLCHAIN \
KSRC=$KERNEL_DIR \
PWD=$(pwd) \
M=$(pwd)/. modules
