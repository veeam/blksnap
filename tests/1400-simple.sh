#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+


if [ -z $1 ]
then
	echo "Should be specified empty test block device."
	exit 255
else
	DEVICE=$1
fi

if [ -z $2 ]
then
	DIFF_STORAGE_DIR=${HOME}
else
	DIFF_STORAGE_DIR=$1
fi
echo "Diff storage directory ${DIFF_STORAGE_DIR}"

. ./functions.sh
. ./blksnap.sh
BLOCK_SIZE=$(block_size_mnt ${DIFF_STORAGE_DIR})

echo "---"
echo "Simple test start"

blksnap_load

# check module is ready
blksnap_version

MPDIR=/mnt/blksnap-test
rm -rf ${MPDIR}
mkdir -p ${MPDIR}

DIFF_STORAGE="${DIFF_STORAGE_DIR}/diff_storage"
fallocate --length 4KiB ${DIFF_STORAGE}

echo "Create new filesystem on ${DEVICE}"
#echo "press ..."
#read -n 1
mkfs.ext4 ${DEVICE}

MOUNTPOINT=${MPDIR}/simple
mkdir -p ${MOUNTPOINT}
mount ${DEVICE} ${MOUNTPOINT}

generate_files_direct ${MOUNTPOINT} "before" 9
drop_cache

#echo "Block device prepared, press ..."
#read -n 1

blksnap_snapshot_create "${DEVICE}" "${DIFF_STORAGE}" "2G"
blksnap_snapshot_take

#echo "Snapshot was token, press ..."
#read -n 1

#echo "Write something" > ${MOUNTPOINT}/something.txt
echo "Write to original"
generate_files_direct ${MOUNTPOINT} "after" 3
drop_cache

check_files ${MOUNTPOINT}

echo "Check snapshots"
DEVICE_IMAGE=$(blksnap_get_image ${DEVICE})
IMAGE=${TESTDIR}/image0
mkdir -p ${IMAGE}
mount ${DEVICE_IMAGE} ${IMAGE}
#echo "pause, press ..."
#read -n 1
check_files ${IMAGE}

echo "Write to snapshot"
generate_files_direct ${IMAGE} "snapshot" 3

drop_cache
umount ${DEVICE_IMAGE}
mount ${DEVICE_IMAGE} ${IMAGE}

#echo "pause, press ..."
#read -n 1
check_files ${IMAGE}

umount ${IMAGE}

blksnap_snapshot_destroy

#echo "Destroy snapshot, press ..."
#read -n 1

drop_cache
umount ${DEVICE}
mount ${DEVICE} ${MOUNTPOINT}

check_files ${MOUNTPOINT}

echo "Destroy first device"
blksnap_detach ${DEVICE}
umount ${MOUNTPOINT}

blksnap_unload

echo "Simple test finish"
echo "---"
