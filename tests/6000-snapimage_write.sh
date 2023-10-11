#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Snapshot write test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB.
modprobe blksnap diff_storage_minimum=262144 chunk_maximum_in_queue=16
sleep 2s

# check module is ready
blksnap_version

if [ -z $1 ]
then
	echo "Should use loop device"

	#echo "Create original loop device"
	LOOPFILE=${HOME}/blksnap-original.img
	dd if=/dev/zero of=${LOOPFILE} count=1024 bs=1M

	DEVICE=$(loop_device_attach ${LOOPFILE})
	mkfs.xfs -f ${DEVICE}
	# mkfs.ext4 ${DEVICE}

	ORIGINAL=/mnt/blksnap-original
	mkdir -p ${ORIGINAL}
	mount ${DEVICE} ${ORIGINAL}
else
	echo "Should use device [$1]"

	DEVICE=$1

	MNTDIR=$(findmnt -n -o TARGET ${DEVICE})
	echo ${MNTDIR}
	ORIGINAL=${MNTDIR}"/blksnap-original"
	rm -rf ${ORIGINAL}/*
	mkdir -p ${ORIGINAL}
fi

FSTYPE=$(findmnt -n -o FSTYPE ${DEVICE})

if [ "${FSTYPE}" = "xfs" ]
then
	MOUNTOPT="-o nouuid"
else
	MOUNTOPT=""
fi

if [ -z $2 ]
then
	ITERATION_CNT="3"
else
	ITERATION_CNT="$2"
fi

IMAGE=/mnt/blksnap-image0
mkdir -p ${IMAGE}

DIFF_STORAGE="/dev/shm"

generate_files_direct ${ORIGINAL} "original-it#0" 5
drop_cache

for ITERATOR in $(seq 1 $ITERATION_CNT)
do
	echo "Itearation: ${ITERATOR}"

	blksnap_snapshot_create ${DEVICE} "${DIFF_STORAGE}" "256M"
	blksnap_snapshot_take

	DEVICE_IMAGE=$(blksnap_get_image ${DEVICE})
	mount ${MOUNTOPT} ${DEVICE_IMAGE} ${IMAGE}

	generate_block_MB ${IMAGE} "image-it#${ITERATOR}" 10 &
	IMAGE_PID=$!
	generate_block_MB ${ORIGINAL} "original-it#${ITERATOR}" 10
	wait ${IMAGE_PID}

	drop_cache

	check_files ${IMAGE} &
	IMAGE_PID=$!
	check_files ${ORIGINAL}
	wait ${IMAGE_PID}

	drop_cache

	#echo "pause, press ..."
	#read -n 1

	echo "Remount image device "${DEVICE_IMAGE}
	umount ${DEVICE_IMAGE}
	mount ${MOUNTOPT} ${DEVICE_IMAGE} ${IMAGE}

	check_files ${IMAGE} &
	IMAGE_PID=$!
	check_files ${ORIGINAL}
	wait ${IMAGE_PID}

	#echo "pause, press ..."
	#read -n 1

	umount ${IMAGE}

	blksnap_snapshot_destroy
done

if [ -z $1 ]
then
	echo "Destroy original loop device"
	umount ${ORIGINAL}

	blksnap_detach ${DEVICE}

	loop_device_detach ${DEVICE}
	imagefile_cleanup ${LOOPFILE}
else
	echo "Cleanup directory [${ORIGINAL}]"
	rm -rf ${ORIGINAL}/*
fi

echo "Unload module"
modprobe -r blksnap

echo "Snapshot write test finish"
echo "---"
