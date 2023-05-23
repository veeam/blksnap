#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+


if [ -z $1 ]
then
	DIFF_STORAGE_DIR=${HOME}
else
	DIFF_STORAGE_DIR=$1
fi
echo "Diff storage directory ${DIFF_STORAGE_DIR}"

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Destroy test start"

blksnap_load

# check module is ready
blksnap_version

TESTDIR=/tmp/blksnap-test
rm -rf ${TESTDIR}
mkdir -p ${TESTDIR}

MPDIR=/mnt/blksnap-test
rm -rf ${MPDIR}
mkdir -p ${MPDIR}


# create first device
IMAGEFILE_1=${TESTDIR}/simple_1.img
imagefile_make ${IMAGEFILE_1} 128

DEVICE_1=$(loop_device_attach ${IMAGEFILE_1})
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

echo "Write to original before taking snapshot"
generate_files ${MOUNTPOINT_1} "before" 9
drop_cache

blksnap_snapshot_create "${DEVICE_1}"

DIFF_STORAGE=${DIFF_STORAGE_DIR}/diff_storage0
fallocate --length 128MiB ${DIFF_STORAGE}
chattr +i ${DIFF_STORAGE}
blksnap_snapshot_appendstorage ${DIFF_STORAGE}

blksnap_snapshot_take

echo "mount snapshot"
DEVICE_IMAGE_1=$(blksnap_get_image ${DEVICE_1})
IMAGE_1=${TESTDIR}/image0
mkdir -p ${IMAGE_1}
mount ${DEVICE_IMAGE_1} ${IMAGE_1}

echo "Write to original after taking snapshot"
generate_files ${MOUNTPOINT_1} "after" 4 &

dd if=${DEVICE_IMAGE_1} of=/dev/zero &

echo "Write to snapshot"
generate_files ${IMAGE_1} "snapshot" 4 &

dd if=${DEVICE_IMAGE_1} of=/dev/zero &

echo "Destroy snapshot ..."
blksnap_snapshot_destroy

umount --lazy --force ${IMAGE_1}

chattr -i ${DIFF_STORAGE}
rm ${DIFF_STORAGE}

echo "Destroy device"
blksnap_detach ${DEVICE_1}
umount ${MOUNTPOINT_1}
loop_device_detach ${DEVICE_1}
imagefile_cleanup ${IMAGEFILE_1}

blksnap_unload

echo "Destroy test finish"
echo "---"
