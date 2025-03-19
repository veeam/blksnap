#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

TEST_NAME="Two snapshots"

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
echo "${TEST_NAME} test start"

blksnap_load
blksnap_version

TESTDIR=/tmp/blksnap-test
rm -rf ${TESTDIR}
mkdir -p ${TESTDIR}

MPDIR=/mnt/blksnap-test
rm -rf ${MPDIR}
mkdir -p ${MPDIR}

# -- 1 --
# create first device
IMAGEFILE_1=${TESTDIR}/simple_1.img
imagefile_make ${IMAGEFILE_1} 64
echo "new image file ${IMAGEFILE_1}"

DEVICE_1=$(loop_device_attach ${IMAGEFILE_1})
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}
generate_files ${MOUNTPOINT_1} "before" 9

# -- 2 --
# create second device
IMAGEFILE_2=${TESTDIR}/simple_2.img
imagefile_make ${IMAGEFILE_2} 128
echo "new image file ${IMAGEFILE_2}"

DEVICE_2=$(loop_device_attach ${IMAGEFILE_2})
echo "new device ${DEVICE_2}"

MOUNTPOINT_2=${MPDIR}/simple_2
mkdir -p ${MOUNTPOINT_2}
mount ${DEVICE_2} ${MOUNTPOINT_2}
generate_files ${MOUNTPOINT_2} "before" 9
drop_cache

#echo "Block device prepared, press ..."
#read -n 1

# -- 1 --
# Create first snapshot
blksnap_snapshot_create "${DEVICE_1}"

DIFF_STORAGE_1=${DIFF_STORAGE_DIR}/diff_storage1
rm -f ${DIFF_STORAGE_1}
fallocate --length 1GiB ${DIFF_STORAGE_1}
blksnap_snapshot_appendstorage ${DIFF_STORAGE_1}

blksnap_snapshot_take
SNAPID_1=${ID}

# -- 2 --
# Create second snapshot
blksnap_snapshot_create "${DEVICE_2}"

DIFF_STORAGE_2=${DIFF_STORAGE_DIR}/diff_storage2
rm -f ${DIFF_STORAGE_2}
fallocate --length 1GiB ${DIFF_STORAGE_2}
blksnap_snapshot_appendstorage ${DIFF_STORAGE_2}

blksnap_snapshot_take
SNAPID_2=${ID}

#echo "Snapshot was token, press ..."
#read -n 1


# -- 1 --
#Check first snapshot
echo "Write to original"
generate_files ${MOUNTPOINT_1} "after" 3
drop_cache

check_files ${MOUNTPOINT_1}

echo "Check snapshots"
IMAGE_1=${TESTDIR}/image1
mkdir -p ${IMAGE_1}
mount /dev/veeamblksnapimg0 ${IMAGE_1}
check_files ${IMAGE_1}

echo "Write to snapshot"
generate_files ${IMAGE_1} "snapshot" 3

drop_cache
umount /dev/veeamblksnapimg0
mount /dev/veeamblksnapimg0 ${IMAGE_1}

check_files ${IMAGE_1}

umount ${IMAGE_1}


# -- 2 --
#Check second snapshot
echo "Write to original"
generate_files ${MOUNTPOINT_2} "after" 3
drop_cache

check_files ${MOUNTPOINT_2}

echo "Check snapshots"
IMAGE_2=${TESTDIR}/image2
mkdir -p ${IMAGE_2}
mount /dev/veeamblksnapimg1 ${IMAGE_2}
check_files ${IMAGE_2}

echo "Write to snapshot"
generate_files ${IMAGE_2} "snapshot" 3

drop_cache
umount /dev/veeamblksnapimg1
mount /dev/veeamblksnapimg1 ${IMAGE_2}

check_files ${IMAGE_2}

umount ${IMAGE_2}


# -- 1 --
# Destroy second snapshot
ID=${SNAPID_2}
blksnap_snapshot_destroy

#echo "Destroy snapshot, press ..."
#read -n 1

rm ${DIFF_STORAGE_2}


# -- 2 --
# Destroy first snapshot
ID=${SNAPID_1}
blksnap_snapshot_destroy

#echo "Destroy snapshot, press ..."
#read -n 1

rm ${DIFF_STORAGE_1}


# -- 1 --
# check first device
drop_cache
umount ${DEVICE_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

check_files ${MOUNTPOINT_1}


# -- 2 --
# check second device
drop_cache
umount ${DEVICE_2}
mount ${DEVICE_2} ${MOUNTPOINT_2}

check_files ${MOUNTPOINT_2}


echo "Destroy second device"
umount ${MOUNTPOINT_2}
loop_device_detach ${DEVICE_2}
imagefile_cleanup ${IMAGEFILE_2}

echo "Destroy first device"
umount ${MOUNTPOINT_1}
loop_device_detach ${DEVICE_1}
imagefile_cleanup ${IMAGEFILE_1}

blksnap_unload

echo "${TEST_NAME} test finish"
echo "---"
