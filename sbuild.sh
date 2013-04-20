#!/bin/bash

BASE_SEMA_VER="Semaphore_N4_0.3.0"
VER=""
SEMA_VER=$BASE_SEMA_VER$VER

#export KBUILD_BUILD_VERSION="2"
export LOCALVERSION="-"`echo $SEMA_VER`
#export CROSS_COMPILE=/opt/toolchains/gcc-linaro-arm-linux-gnueabihf-2012.09-20120921_linux/bin/arm-linux-gnueabihf-
export CROSS_COMPILE=../arm-linux-androideabi-4.7/bin/arm-linux-androideabi-
export ARCH=arm
export SUBARCH=arm
export KBUILD_BUILD_USER=stratosk
export KBUILD_BUILD_HOST="semaphore.gr"

echo 
echo "Making semaphore_mako_defconfig"

DATE_START=$(date +"%s")

make "semaphore_mako_defconfig"

#eval $(grep CONFIG_INITRAMFS_SOURCE .config)
#INIT_DIR=$CONFIG_INITRAMFS_SOURCE
#MODULES_DIR=`echo $INIT_DIR`files/modules
KERNEL_DIR=`pwd`
OUTPUT_DIR=output/
CWM_DIR=cwm/

echo "LOCALVERSION="$LOCALVERSION
echo "CROSS_COMPILE="$CROSS_COMPILE
echo "ARCH="$ARCH
echo "INIT_DIR="$INIT_DIR
echo "MODULES_DIR="$MODULES_DIR
echo "KERNEL_DIR="$KERNEL_DIR
echo "OUTPUT_DIR="$OUTPUT_DIR
echo "CWM_DIR="$CWM_DIR

#make -j16 modules

#rm `echo $MODULES_DIR"/*"`
#find $KERNEL_DIR -name '*.ko' -exec cp -v {} $MODULES_DIR \;

make -j16 zImage

#cd arch/arm/boot
#tar cvf `echo $SEMA_VER`.tar zImage
#mv `echo $SEMA_VER`.tar ../../../$OUTPUT_DIR$VARIANT
#echo "Moving to "$OUTPUT_DIR$VARIANT"/"
#cd ../../../

cp arch/arm/boot/zImage ../boot.img
cd ../
./mkbootimg

cp boot.img $CWM_DIR
cd $CWM_DIR
zip -r `echo $SEMA_VER`.zip *
mv  `echo $SEMA_VER`.zip ../$OUTPUT_DIR

DATE_END=$(date +"%s")
echo
DIFF=$(($DATE_END - $DATE_START))
echo "Build completed in $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds."
