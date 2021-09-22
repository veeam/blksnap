#!/bin/bash -e

# check module is ready
blksnap_version

TESTDIR=/tmp/blk-snap-test
mkdir -p ${TESTDIR}

MPDIR=/mnt/blk-snap-test
mkdir -p ${MPDIR}

# create first device
IMAGEFILE_1=${TESTDIR}/simple_1.img
imagefile_make ${IMAGEFILE_1} 64M
echo "new image file ${IMAGEFILE_1}"

DEVICE_1=$(loop_device_attach ${IMAGEFILE_1})
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

# create second device
IMAGEFILE_2=${TESTDIR}/simple_2.img
imagefile_make ${IMAGEFILE_2} 128M
echo "new image file ${IMAGEFILE_2}"

DEVICE_2=$(loop_device_attach ${IMAGEFILE_2})
echo "new device ${DEVICE_2}"

MOUNTPOINT_2=${MPDIR}/simple_2
mkdir -p ${MOUNTPOINT_2}
mount ${DEVICE_2} ${MOUNTPOINT_2}


blksnap_snapshot_create_inmem "${DEVICE_1} ${DEVICE_2}"

blksnap_snapshot_take

blksnap_snapshot_destroy


# destroy second device
umount ${MOUNTPOINT_2}
loop_device_detach ${DEVICE_2}
imagefile_cleanup ${IMAGEFILE_2}

# destroy first device
umount ${MOUNTPOINT_1}
loop_device_detach ${DEVICE_1}
imagefile_cleanup ${IMAGEFILE_1}
