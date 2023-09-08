# I.MX8mn variscite som in a coffee machine
BOARD_NAME="meticulous1"
BOARDFAMILY="imx8mn"
BOARD_MAINTAINER="meticulous"

BOOT_LOGO="desktop"
BOOT_FDT_NAME="imx8mn-var-som-symphony.dtb"
BOOT_FDT_FILE="freescale/$BOOT_FDT_NAME"
BOOTCONFIG="imx8mn_var_som_defconfig"

KERNEL_TARGET="current,edge"
LINUXCONFIG="linux-imx8mn-meticulous1-$BRANCH"

FULL_DESKTOP="yes"

PACKAGE_LIST_BOARD="libubootenv-tool" # libubootenv-tool provides fw_printenv and fw_setenv, for talking to U-Boot environment

KERNELSOURCE="https://github.com/mimoja/linux.git"
KERNELBRANCH='branch:st7701-meticulous-2023-09-02'
KERNEL_MAJOR_MINOR="5.15"
