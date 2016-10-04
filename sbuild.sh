#!/bin/bash

BASE_SEMA_VER="Semaphore_N5X_0.5.0"
VER=""
SEMA_VER=$BASE_SEMA_VER$VER

#export KBUILD_BUILD_VERSION="2"
export LOCALVERSION="-"`echo $SEMA_VER`
export CROSS_COMPILE=/opt/toolchains/gcc-linaro-4.9-2016.02-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-
#export CROSS_COMPILE=/home/stratosk/kernels/n5x/aarch64-linux-android-4.9/bin/aarch64-linux-android-
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_BUILD_USER=stratosk
export KBUILD_BUILD_HOST="semaphore.gr"

echo 
echo "Making semaphore_bullhead_defconfig"

DATE_START=$(date +"%s")

make "semaphore_bullhead_defconfig"

INIT_DIR=../initramfs_nougat
MODULES_DIR=../initramfs_nougat/lib/modules
KERNEL_DIR=`pwd`
OUTPUT_DIR=output/
CWM_DIR=cwm/
CWM_ANY_DIR=cwm_any/

echo "LOCALVERSION="$LOCALVERSION
echo "CROSS_COMPILE="$CROSS_COMPILE
echo "ARCH="$ARCH
echo "INIT_DIR="$INIT_DIR
echo "MODULES_DIR="$MODULES_DIR
echo "KERNEL_DIR="$KERNEL_DIR
echo "OUTPUT_DIR="$OUTPUT_DIR
echo "CWM_DIR="$CWM_DIR
echo "CWN_ANY_DIR="$CWM_ANY_DIR

make -j8 > /dev/null

rm `echo $MODULES_DIR"/*"`
rm `echo ../$CWM_ANY_DIR"system/lib/modules/*"`
find $KERNEL_DIR -name '*.ko' -exec cp -v {} $MODULES_DIR \;
find $KERNEL_DIR -name '*.ko' -exec cp -v {} ../$CWM_ANY_DIR"kernel/lib/modules/" \;
cd $INIT_DIR
find . \( ! -regex '.*/\..*' \) | cpio -o -H newc -R root:root | gzip -9 > ../initrd.img
cd  $KERNEL_DIR

cp arch/arm64/boot/Image.gz ../boot.img
cp arch/arm64/boot/Image.gz ../$CWM_ANY_DIR/kernel/
cd ../
./mkbootimg

cp boot.img $CWM_DIR
cd $CWM_DIR
zip -r `echo $SEMA_VER`_legacy.zip *
mv  `echo $SEMA_VER`_legacy.zip ../$OUTPUT_DIR

cd ../
cd $CWM_ANY_DIR
zip -r `echo $SEMA_VER`.zip *
mv  `echo $SEMA_VER`.zip ../$OUTPUT_DIR

DATE_END=$(date +"%s")
echo
DIFF=$(($DATE_END - $DATE_START))
echo "Build completed in $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."
