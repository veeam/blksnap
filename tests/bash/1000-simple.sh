#!/bin/bash -e

echo "---"
echo "Simple test start"

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

echo "Press any key to continue..."
read -n 1

dd if=/dev/blk-snap-image0 of=${TESTDIR}/image0 bs=4096 count=1
# dd if=/dev/blk-snap-image1 of=${TESTDIR}/image1 bs=4096 count=1

echo "Press any key to continue..."
read -n 1

blksnap_snapshot_destroy


echo "Destroy second device"
umount ${MOUNTPOINT_2}
loop_device_detach ${DEVICE_2}
imagefile_cleanup ${IMAGEFILE_2}

echo "Destroy first device"
umount ${MOUNTPOINT_1}
loop_device_detach ${DEVICE_1}
imagefile_cleanup ${IMAGEFILE_1}

echo "Simple test finish"
echo "---"
