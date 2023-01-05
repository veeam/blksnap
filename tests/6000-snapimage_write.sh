#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

if [ -z $1 ]
then
	echo "Should use loop devices"
else
	DEVICE="$1"
fi

if [ -z $2 ]
then
	END="10"
else
	END="$2"
fi

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Snapshot write test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB dikk_storage portion size
modprobe blksnap diff_storage_minimum=262144
sleep 2s

# check module is ready
blksnap_version

TESTDIR=~/blksnap-test
MPDIR=/mnt/blksnap-test
DIFF_STORAGE=~/diff_storage/
rm -rf ${TESTDIR}
rm -rf ${MPDIR}
rm -rf ${DIFF_STORAGE}
mkdir -p ${TESTDIR}
mkdir -p ${MPDIR}
mkdir -p ${DIFF_STORAGE}

if [ -z ${DEVICE} ]
then
	#echo "Create original loop device"
	LOOPFILE=${TESTDIR}/original.img
	dd if=/dev/zero of=${LOOPFILE} count=4096 bs=1M

	DEVICE=$(loop_device_attach ${LOOPFILE})

fi
mkfs.xfs -f ${DEVICE}

MOUNTPOINT=${MPDIR}/original
mkdir -p ${MOUNTPOINT}

IMAGEDEVICE=/dev/blksnap-image0
IMAGEMOUNTPOINT=${MPDIR}/image
mkdir -p ${IMAGEMOUNTPOINT}

mount ${DEVICE} ${MOUNTPOINT}

generate_files ${MOUNTPOINT} "original-it#0" 5
drop_cache

fallocate --length 256MiB "${DIFF_STORAGE}/diff_storage"

for ITERATOR in $(seq 1 $END)
do
	echo "Itearation: ${ITERATOR}"

	blksnap_snapshot_create ${DEVICE}
	blksnap_snapshot_append "${DIFF_STORAGE}/diff_storage"
	blksnap_snapshot_take

	mount -o nouuid ${IMAGEDEVICE} ${IMAGEMOUNTPOINT}

	generate_block_MB ${IMAGEMOUNTPOINT} "image-it#${ITERATOR}" 10 &
	generate_block_MB ${MOUNTPOINT} "original-it#${ITERATOR}" 10
	sleep 1s
	drop_cache

	check_files ${IMAGEMOUNTPOINT} &
	check_files ${MOUNTPOINT}
	sleep 1s
	drop_cache

	umount ${IMAGEDEVICE}
	mount -o nouuid ${IMAGEDEVICE} ${IMAGEMOUNTPOINT}

	check_files ${IMAGEMOUNTPOINT} &
	check_files ${MOUNTPOINT}
	sleep 1s

	umount ${IMAGEDEVICE}

	blksnap_snapshot_destroy

	umount ${DEVICE}
	mount ${DEVICE} ${MOUNTPOINT}
done


umount ${MOUNTPOINT}

if [ -z ${LOOPFILE} ]
then
	echo "Release original device "${DEVICE}
else
	echo "Destroy original loop device"
	loop_device_detach ${DEVICE}
	imagefile_cleanup ${LOOPFILE}
fi

echo "Unload module"
modprobe -r blksnap

echo "Snapshot write test finish"
echo "---"
