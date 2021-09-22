#!/bin/bash -e

# check module is ready
blksnap_version

TESTDIR=/tmp/blk-snap-test
mkdir -p ${TESTDIR}

IMAGEFILE=${TESTDIR}/simple.img
imagefile_make ${IMAGEFILE} 64M
echo "new image file ${IMAGEFILE}"

DEVICE=$(loop_device_attach ${IMAGEFILE})
echo "new device ${DEVICE}"

MPDIR=/mnt/blk-snap-test
MOUNTPOINT=${MPDIR}/simple
mkdir -p ${MOUNTPOINT}
mount ${DEVICE} ${MOUNTPOINT}

blksnap_snapshot_create_inmem ${DEVICE}

blksnap_snapshot_take

blksnap_snapshot_destroy


umount ${MOUNTPOINT}

loop_device_detach ${DEVICE}

imagefile_cleanup ${IMAGEFILE}
