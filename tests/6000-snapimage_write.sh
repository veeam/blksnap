#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Snapshot write test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB dikk_storage portion size
modprobe blksnap diff_storage_minimum=262144
sleep 2s

# check module is ready
blksnap_version

if [ -z $1 ]
then
	echo "Should use loop device"

	#echo "Create original loop device"
	LOOPFILE=~/blksnap-original.img
	dd if=/dev/zero of=${LOOPFILE} count=4096 bs=1M

	DEVICE=$(loop_device_attach ${LOOPFILE})
	mkfs.xfs -f ${DEVICE}

	MOUNTPOINT=/mnt/blksnap-original
	mkdir -p ${MOUNTPOINT}
	mount ${DEVICE} ${MOUNTPOINT}
else
	echo "Should use device [$1]"

	DEVICE=$1

	MNTDIR=$(findmnt -n -o TARGET ${DEVICE})
	echo ${MNTDIR}
	MOUNTPOINT=${MNTDIR}"/blksnap-original"
	rm -rf ${MOUNTPOINT}/*
	mkdir -p ${MOUNTPOINT}
fi

FSTYPE=$(findmnt -n -o FSTYPE ${DEVICE})

if [ ${FSTYPE} == "xfs" ]
then
	MOUNTOPT="-o nouuid"
else
	MOUNTOPT=""
fi

if [ -z $2 ]
then
	END="10"
else
	END="$2"
fi

IMAGEDEVICE=/dev/blksnap-image0
IMAGEMOUNTPOINT=/mnt/blksnap-image0
mkdir -p ${IMAGEMOUNTPOINT}

DIFF_STORAGE="${MOUNTPOINT}/diff_storage/"
mkdir -p ${DIFF_STORAGE}

fallocate --length 256MiB "${DIFF_STORAGE}/diff_storage" &

generate_files ${MOUNTPOINT} "original-it#0" 5
drop_cache

for ITERATOR in $(seq 1 $END)
do
	echo "Itearation: ${ITERATOR}"

	blksnap_snapshot_create ${DEVICE}
	blksnap_snapshot_append "${DIFF_STORAGE}/diff_storage"
	blksnap_snapshot_take

	#echo "Finita le comedy"
	#exit 1
	mount ${MOUNTOPT} ${IMAGEDEVICE} ${IMAGEMOUNTPOINT}

	generate_block_MB ${IMAGEMOUNTPOINT} "image-it#${ITERATOR}" 10 &
	generate_block_MB ${MOUNTPOINT} "original-it#${ITERATOR}" 10
	sleep 1s
	drop_cache

	check_files ${IMAGEMOUNTPOINT} &
	check_files ${MOUNTPOINT}
	sleep 1s
	drop_cache

	umount ${IMAGEDEVICE}

	mount ${MOUNTOPT} ${IMAGEDEVICE} ${IMAGEMOUNTPOINT}

	check_files ${IMAGEMOUNTPOINT} &
	check_files ${MOUNTPOINT}
	sleep 1s

	umount ${IMAGEDEVICE}

	blksnap_snapshot_destroy
done

if [ -z $1 ]
then
	echo "Destroy original loop device"
	umount ${MOUNTPOINT}

	loop_device_detach ${DEVICE}
	imagefile_cleanup ${LOOPFILE}
else
	echo "Cleanup directory [${MOUNTPOINT}]"
fi
rm -rf ${MOUNTPOINT}/*

echo "Unload module"
modprobe -r blksnap

echo "Snapshot write test finish"
echo "---"
