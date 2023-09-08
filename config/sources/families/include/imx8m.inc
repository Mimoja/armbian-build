#
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2013-2023 Igor Pecovnik, igor@armbian.com
#
# This file is a part of the Armbian Build Framework
# https://github.com/armbian/build/
#

ARCH=arm64

BOOT_SCENARIO="spl-blobs"
BOOT_SUPPORT_SPI="no"
BOOTDELAY=1

[[ -z $BOOTENV_FILE ]] && declare -g BOOTENV_FILE='imx8m.txt'

ATF_TOOLCHAIN2="arm-linux-gnueabi-:> 5.0"

# First partition offset 8MiBs
[[ -z $OFFSET ]] && declare -g OFFSET=8
# U-boot offset is 32 or 33 KiB
[[ -z $UBOOT_OFFSET ]] && declare -g UBOOT_OFFSET=33

SERIALCON=ttymxc0
[[ -z $KERNEL_IMAGE_TYPE ]] && declare -g KERNEL_IMAGE_TYPE="Image"
KERNELDIR="linux-imx8m-$BRANCH"

case $BRANCH in
	current)
		# Linux kernel
		[[ -z $KERNELSOURCE ]] && declare -g KERNELSOURCE="https://github.com/nxp-imx/linux-imx"
		[[ -z $KERNELBRANCH ]] && declare -g KERNELBRANCH='branch:lf-5.15.y_var01'
		[[ -z $KERNEL_MAJOR_MINOR ]] && declare -g KERNEL_MAJOR_MINOR="5.15"

		# u-boot
		[[ -z $BOOTSOURCE ]] && declare -g BOOTSOURCE="https://github.com/nxp-imx/uboot-imx.git"
		[[ -z $BOOTBRANCH ]] && declare -g BOOTBRANCH="branch:lf_v2022.04"
		git_parse_ref $BOOTBRANCH
		[[ -z $BOOTBRANCH ]] && declare -g BOOTPATCHDIR="u-boot-imx8m-${ref_name}"
		BOOTDIR='u-boot-imx8m'
		# flash.bin is created below in our custom u-boot postprocessing hook
		UBOOT_TARGET_MAP=';;flash.bin'

		# ATF
		[[ -z $ATFSOURCE ]] && declare -g ATFSOURCE='https://github.com/varigit/imx-atf.git'
		[[ -z $KERNATFDIRELSOURCE ]] && declare -g ATFDIR='arm-trusted-firmware-nxp'
		[[ -z $ATFBRANCH ]] && declare -g ATFBRANCH='branch:lf_v2.6_var02'
		declare -g ATF_TARGET_MAP="M0_CROSS_COMPILE=arm-linux-gnueabi- PLAT=$ATF_PLAT DEBUG=1 bl31;;build/$ATF_PLAT/debug/bl31.bin"
		;;

	edge)
		# Linux kernel
		KERNELSOURCE="https://github.com/mimoja/linux.git"
		KERNELBRANCH='branch:st7701-meticulous-2023-09-02'
		KERNELDIR='linux-mainline'
		KERNEL_MAJOR_MINOR="5.15"

		# uboot
		BOOTBRANCH='tag:v2022.04'

		# Ensure the DDR firmware files are next to the flash.bin for binman
		declare -g UBOOT_TARGET_MAP="ATF_LOAD_ADDR=${ATF_LOAD_ADDR} flash.bin;;flash.bin"

		# ATF
		declare -g ATF_TARGET_MAP="IMX_BOOT_UART_BASE=0x30a60000 PLAT=$ATF_PLAT bl31;;build/$ATF_PLAT/release/bl31.bin"
		;;

esac

family_tweaks() {
	if [[ "$BRANCH" == "edge" ]]; then
		return 0
	fi

	# Temporary disable RAM log due to hang during boot
	sed -i "s/^ENABLED=.*/ENABLED=false/" $SDCARD/etc/default/armbian-ramlog
	sed -i "s/^ENABLED=.*/ENABLED=false/" $SDCARD/etc/default/armbian-zram-config
}

function post_family_tweaks__config_imx8mn_fwenv() {
	display_alert "Configuring fw_printenv and fw_setenv" "for imx8mn" "info"
	cat <<- 'FW_ENV_CONFIG' > "${SDCARD}"/etc/fw_env.config
		/dev/mmcblk1                       0x400000                      0x4000
	FW_ENV_CONFIG
}

write_uboot_platform() {
	dd if=$1/flash.bin of=$2 bs=1K seek=${UBOOT_OFFSET} status=noxfer conv=fsync > /dev/null 2>&1
}


function add_host_dependencies__uboot_deps() {
	DEPS="python3-pyelftools libgnutls28-dev"
	display_alert "Adding $DEPS for uboot" "${EXTENSION}" "info"
	declare -g EXTRA_BUILD_DEPS="${EXTRA_BUILD_DEPS} $DEPS"
}

function install_boot_firmware() {
	FIRMWARE_INSTALL_DIR=$(pwd $1)
	# http://sources.buildroot.net/firmware-imx/
	FIRMWARE_VERSION=8.9
	FIRMWARE_DOWNLOAD_URL=https://www.nxp.com/lgfiles/NMG/MAD/YOCTO/firmware-imx-$FIRMWARE_VERSION.bin

	FIRMWARE_FOLDER=${SRC}/cache/sources/firmware-imx
	FIRMWARE_INSTALLER=firmware-imx-$FIRMWARE_VERSION.bin
	FIRMWARE_PATH=firmware-imx-$FIRMWARE_VERSION

	display_alert "Installing custom DDR firmware" "$FIRMWARE_VERSION" "info"

	echo "FIRMWARE_INSTALL_DIR=$FIRMWARE_INSTALL_DIR"
	echo "FIRMWARE_VERSION=$FIRMWARE_VERSION"
	echo "FIRMWARE_DOWNLOAD_URL=$FIRMWARE_DOWNLOAD_URL"
	echo "FIRMWARE_FOLDER=$FIRMWARE_FOLDER"
	echo "FIRMWARE_INSTALLER=$FIRMWARE_INSTALLER"

	mkdir -p $FIRMWARE_FOLDER
	pushd $FIRMWARE_FOLDER > /dev/null

	if [ ! -e firmware-imx-$FIRMWARE_VERSION.bin ]; then
			echo "Downloading IMX firmare binaries..."
			echo $FIRMWARE_DOWNLOAD_URL
			wget $FIRMWARE_DOWNLOAD_URL -O ${FIRMWARE_INSTALLER}
	fi

	chmod a+x ${FIRMWARE_INSTALLER}
	if [ ! -d ${FIRMWARE_PATH} ]; then
			echo "Running firmware unpacker, auto accepting EULA"
			./${FIRMWARE_INSTALLER} --auto-accept
	fi
	echo "Copy firmware blobs..."
	mkdir -p $FIRMWARE_INSTALL_DIR
	# Synopsys DDR
	for ddr_firmware in ${DDR_FIRMWARE_NAME}; do
		cp -v ${FIRMWARE_PATH}/firmware/ddr/synopsys/${ddr_firmware} $FIRMWARE_INSTALL_DIR
	done
	# Cadence DP and HDMI
	cp -v ${FIRMWARE_PATH}/firmware/hdmi/cadence/signed_dp_imx8m.bin $FIRMWARE_INSTALL_DIR
	cp -v ${FIRMWARE_PATH}/firmware/hdmi/cadence/signed_hdmi_imx8m.bin $FIRMWARE_INSTALL_DIR

	popd > /dev/null
}

function uboot_custom_postprocess() {
	# Only needed for current branch with imx-mkimage flash.bin creation
	if [[ "$BRANCH" == "edge" ]]; then
		return 0
	fi

	UBOOT_DIR=${PWD}

	display_alert  "Building u-boot with imx-mkimage" "imx-mkimage" "info"

	VARISCITE_PATCHES_URL="https://raw.githubusercontent.com/varigit/meta-variscite-bsp-imx/kirkstone-var02/recipes-bsp/imx-mkimage/imx-boot/"



	pushd $SRC/cache/sources/imx-mkimage > /dev/null
	wget -q $VARISCITE_PATCHES_URL/0001-iMX8M-soc-allow-dtb-override.patch -O 0001-iMX8M-soc-allow-dtb-override.patch
	wget -q $VARISCITE_PATCHES_URL/0002-iMX8M-soc-change-padding-of-DDR4-and-LPDDR4-DMEM-fir.patch -O 0002-iMX8M-soc-change-padding-of-DDR4-and-LPDDR4-DMEM-fir.patch
	git apply 0001-iMX8M-soc-allow-dtb-override.patch
	git apply 0002-iMX8M-soc-change-padding-of-DDR4-and-LPDDR4-DMEM-fir.patch
	rm *.patch

	cd iMX8M
	install_boot_firmware .

	display_alert "Packing uboot-imx flash.bin" "$IMXBOOT_TARGETS" "info"

    cp -v $UBOOT_DIR/spl/u-boot-spl.bin ./
    cp -v $UBOOT_DIR/u-boot-nodtb.bin ./
    cp -v $UBOOT_DIR/arch/arm/dts/${BOOT_FDT_NAME} ./
	cp -v $UBOOT_DIR/tools/mkimage mkimage_uboot

	git_parse_ref $ATFBRANCH
    cp -v $SRC/cache/sources/$ATFDIR/${ref_name}/build/imx8mn/debug/bl31.bin ./

	cd ..
	make \
		SOC=${IMX_BOOT_SOC_TARGET} \
		dtbs=${BOOT_FDT_NAME} \
		OUTIMG=$UBOOT_DIR/flash.bin \
		${IMXBOOT_TARGETS}

	make clean
	popd
}



function fetch_sources_tools__imx-mkimage() {
	# Only needed for current branch with imx-mkimage flash.bin creation
	if [[ "$BRANCH" == "edge" ]]; then
		return 0
	fi

	fetch_from_repo "https://github.com/nxp-imx/imx-mkimage" "imx-mkimage" "branch:lf-5.15.71_2.2.0"
}

function pre_config_uboot_target__install_firmware()
{
	# Upstream u-boot automatically packs the firmware into the image, so download it after config
	if [[ "$BRANCH" == "edge" ]]; then
			rm spl/u-boot-spl.bin
			install_boot_firmware .
	fi
	return 0
}

ignore() {
    install -d ${D}${SDCARD}/lib/firmware/imx

    # SDMA Firmware section
    install -d ${D}${SDCARD}/lib/firmware/imx/sdma
    install -m 0644 ${S}/firmware/sdma/* ${D}${SDCARD}/lib/firmware/imx/sdma

    # EASRC Firmware section
    install -d ${D}${SDCARD}/lib/firmware/imx/easrc
    install -m 0644 ${S}/firmware/easrc/* ${D}${SDCARD}/lib/firmware/imx/easrc/

    # XCVR Firmware section
    install -d ${D}${SDCARD}/lib/firmware/imx/xcvr
    install -m 0644 ${S}/firmware/xcvr/* ${D}${SDCARD}/lib/firmware/imx/xcvr/

    # XUVI Firmware section
    install -d ${D}${SDCARD}/lib/firmware/imx/xuvi
    install -m 0644 ${S}/firmware/xuvi/* ${D}${SDCARD}/lib/firmware/imx/xuvi/

    # EPDC Firmware section
    # NOTE:
    # epdc_ED060XH2C1.fw file has .nonrestricted suffix in the source archive, hence it should
    # be installed with a different name
    install -d ${D}${SDCARD}/lib/firmware/imx/epdc
    install -m 0644 ${S}/firmware/epdc/*.fw ${D}${SDCARD}/lib/firmware/imx/epdc/
    install -m 0644 ${S}/firmware/epdc/epdc_ED060XH2C1.fw.nonrestricted ${D}${SDCARD}/lib/firmware/imx/epdc/epdc_ED060XH2C1.fw

    # HDMI Firmware section
    # NOTE:
    # Only install pre-defined list of firmware files, since the source archive contains
    # also HDMI binary files for imx8m derivatives, which are taken care of by another recipe
    install -m 0644 ${S}/firmware/hdmi/cadence/hdmitxfw.bin ${D}${SDCARD}/lib/firmware
    install -m 0644 ${S}/firmware/hdmi/cadence/hdmirxfw.bin ${D}${SDCARD}/lib/firmware
    install -m 0644 ${S}/firmware/hdmi/cadence/dpfw.bin ${D}${SDCARD}/lib/firmware

    # VPU Firmware section
    # NOTE:
    # Do the same thing as above for HDMI - only install a pre-defined list of firmware files,
    # as some of other files are provided by packages from other recipes.
    install -d ${D}${SDCARD}/lib/firmware/vpu
    install -m 0644 ${S}/firmware/vpu/vpu_fw_imx*.bin ${D}${SDCARD}/lib/firmware/vpu
}

