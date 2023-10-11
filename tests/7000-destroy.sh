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
BLOCK_SIZE=$(block_size_mnt ${DIFF_STORAGE_DIR})

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

DEVICE_1=$(loop_device_attach ${IMAGEFILE_1} ${BLOCK_SIZE})
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

echo "Write to original before taking snapshot"
generate_files_sync ${MOUNTPOINT_1} "before" 9
drop_cache

DIFF_STORAGE=${DIFF_STORAGE_DIR}/diff_storage
fallocate --length 1GiB ${DIFF_STORAGE}
blksnap_snapshot_create "${DEVICE_1}" "${DIFF_STORAGE}" "128M"
blksnap_snapshot_take

echo "mount snapshot"
DEVICE_IMAGE_1=$(blksnap_get_image ${DEVICE_1})
IMAGE_1=${TESTDIR}/image0
mkdir -p ${IMAGE_1}
mount ${DEVICE_IMAGE_1} ${IMAGE_1}

set +e

echo "Write to original after taking snapshot"
generate_files_sync ${MOUNTPOINT_1} "after" 4 &
PID_GEN1=$!
dd if=${DEVICE_IMAGE_1} of=/dev/zero &
PID_DD1=$!

echo "Write to snapshot"
generate_files_sync ${IMAGE_1} "snapshot" 4 &
PID_GEN2=$!
dd if=${DEVICE_IMAGE_1} of=/dev/zero &
PID_DD2=$!

echo "Destroy snapshot ..."
blksnap_snapshot_destroy

umount --lazy --force ${IMAGE_1}

echo "Waiting for all process terminate"
wait ${PID_GEN1}
wait ${PID_DD1}
wait ${PID_GEN2}
wait ${PID_DD2}

set -e

echo "Destroy device"
blksnap_detach ${DEVICE_1}
umount ${MOUNTPOINT_1}
loop_device_detach ${DEVICE_1}
imagefile_cleanup ${IMAGEFILE_1}

blksnap_unload

echo "Destroy test finish"
echo "---"
