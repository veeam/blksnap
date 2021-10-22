#!/bin/bash -e
# SPDX-License-Identifier: GPL-2.0
MODULE_NAME=blk-snap
FILTER_NAME=bdev-filter
CMD=$1
if [ -n "$2" ]
then
	if [ "-" = "$2" ]
	then
		echo "kernel version is not set"
    else
	    KERNEL_RELEASE="$2"
    fi
else
	KERNEL_RELEASE="$(uname -r)"
fi
MODULE_PATH=/lib/modules/${KERNEL_RELEASE}/kernel/drivers/block

case "$CMD" in
	build)
		echo Making ...
		make -j`nproc` -C /lib/modules/${KERNEL_RELEASE}/build M=$(pwd) modules
		echo Completed.
		;;
	clean)
		echo Cleaning ...
		make -C /lib/modules/${KERNEL_RELEASE}/build M=$(pwd) clean
		echo Completed.
		;;
	install)
		echo Installing ...
		mkdir -p ${MODULE_PATH}
		cp ${FILTER_NAME}.ko ${MODULE_PATH}
		cp ${MODULE_NAME}.ko ${MODULE_PATH}
		depmod
		echo Completed.
		;;
	uninstall)
		rm -f ${MODULE_PATH}/${FILTER_NAME}.ko
		rm -f ${FILTER_NAME}/${MODULE_NAME}.ko
		;;
	load)
		echo "Loading ${MODULE_NAME} kernel module from current folder"
		modprobe dm-mod
		insmod ./${FILTER_NAME}.ko
		insmod ./${MODULE_NAME}.ko
		;;
	unload)
		echo "Unloading ${MODULE_NAME} kernel module"
		rmmod ${MODULE_NAME}
		echo 0 > /sys/kernel/livepatch/bdev_filter/enabled
		sleep 2s
		rmmod ${FILTER_NAME}
		;;
	*)
		echo "usage: $0 {build | clean | install | uninstall | load | unload}"
		exit 1
esac
