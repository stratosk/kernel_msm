export ARCH=arm
unset CROSS_COMPILE
#export CROSS_COMPILE=/opt/gcc-4.8-linaro/bin/arm-cortex_a15-linux-gnueabi- 
#export CROSS_COMPILE=/opt/gcc-4.7-linaro/bin/arm-cortex_a15-linux-gnueabi- 
export CROSS_COMPILE=/opt/sabermod-gcc/bin/arm-eabi-

#echo "Cleaning old craps..."
#make distclean

export KBUILD_BUILD_USER=najmi
export KBUILD_BUILD_HOST="kampung-pandan"
export LOCALVERSION="-Semaphore-Pandan-kernel-"

#echo "Copy backup config..."
#cp najmi-mako-config .config
make semaphore_mako_defconfig
make menuconfig
#echo "Begin compile..."
#make -j8


