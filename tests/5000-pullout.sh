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
echo "pullout test start"

blksnap_load
mkdir -p /var/log/veeam/
rm -f /var/log/veeam/blksnap.log
blksnap_log_debug /var/log/veeam/blksnap.log

# check module is ready
blksnap_version

TESTDIR=/tmp/blksnap-test
rm -rf ${TESTDIR}
mkdir -p ${TESTDIR}

MPDIR=/mnt/blksnap-test
rm -rf ${MPDIR}
mkdir -p ${MPDIR}

ALG="lzo"
modprobe zram num_devices=2 && sleep 1

# create first device
DEVICE_1="/dev/zram0"
zramctl --size 128M --algorithm ${ALG} ${DEVICE_1}
mkfs.ext4 ${DEVICE_1}
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

# create second device
DEVICE_2="/dev/zram1"
zramctl --size 128M --algorithm ${ALG} ${DEVICE_2}
mkfs.ext4 ${DEVICE_2}
echo "new device ${DEVICE_2}"

MOUNTPOINT_2=${MPDIR}/simple_2
mkdir -p ${MOUNTPOINT_2}
mount ${DEVICE_2} ${MOUNTPOINT_2}

generate_files_sync ${MOUNTPOINT_1} "before" 9
drop_cache

echo "Block device prepared"
#echo "press ..."
#read -n 1

DIFF_STORAGE="${DIFF_STORAGE_DIR}/diff_storage"
rm -f ${DIFF_STORAGE}
fallocate --length 256MiB ${DIFF_STORAGE}
blksnap_snapshot_create "${DEVICE_1} ${DEVICE_2}" "${DIFF_STORAGE}" "1G"
blksnap_snapshot_take

echo "Snapshot was token"
#echo "press ..."
#read -n 1

blksnap_snapshot_collect

echo "Write to original"
#echo "Write something" > ${MOUNTPOINT_1}/something.txt
generate_files_sync ${MOUNTPOINT_1} "after" 3
drop_cache

check_files ${MOUNTPOINT_1}

echo "Check snapshots"
DEVICE_IMAGE_1=$(blksnap_get_image ${DEVICE_1})
IMAGE_1=${TESTDIR}/image0
mkdir -p ${IMAGE_1}
mount ${DEVICE_IMAGE_1} ${IMAGE_1}
check_files ${IMAGE_1}

echo "Write to snapshot"
generate_files_sync ${IMAGE_1} "snapshot" 3

drop_cache
umount ${DEVICE_IMAGE_1}
mount ${DEVICE_IMAGE_1} ${IMAGE_1}

check_files ${IMAGE_1}

umount ${IMAGE_1}

blksnap_snapshot_destroy

echo "Destroy snapshot"
#echo "press ..."
#read -n 1

drop_cache
umount ${DEVICE_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

check_files ${MOUNTPOINT_1}

echo "Destroy devices"
umount ${MOUNTPOINT_2}
umount ${MOUNTPOINT_1}
modprobe -r zram

blksnap_unload

echo "pullout test finish"
echo "---"
