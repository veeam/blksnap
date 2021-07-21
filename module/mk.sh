#!/bin/bash -e
# SPDX-License-Identifier: GPL-2.0
KERNEL_MODULE_NAME=blk-snap-lp
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

case "$CMD" in
	build)
		echo Making all...
		make -j`nproc` -C /lib/modules/${KERNEL_RELEASE}/build M=$(pwd) modules
		echo Completed.
		;;
	clean)
		echo Cleaning ...
		make -C /lib/modules/${KERNEL_RELEASE}/build M=$(pwd) clean
		echo Completed.
		;;
	install)
		echo Installing veeamsnap kernel module
		mkdir -p /lib/modules/${KERNEL_RELEASE}/kernel/drivers
		cp ${KERNEL_MODULE_NAME}.ko /lib/modules/${KERNEL_RELEASE}/kernel/drivers
		depmod
		echo Completed.
		;;
	uninstall)
		rm -f /lib/modules/${KERNEL_RELEASE}/kernel/drivers/${KERNEL_MODULE_NAME}.ko
		;;
	load)
		echo "Loading ${KERNEL_MODULE_NAME} kernel module from current folder"
		insmod ./${KERNEL_MODULE_NAME}.ko zerosnapdata=1
		;;
	unload)
		echo "Unloading ${KERNEL_MODULE_NAME} kernel module"
		rmmod ${KERNEL_MODULE_NAME}
		;;
	*)
		echo "usage: $0 {build | clean | install | uninstall | load | unload}"
		exit 1
esac
