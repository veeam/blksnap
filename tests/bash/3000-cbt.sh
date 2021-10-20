#!/bin/bash -e

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Change tracking test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB dikk_storage portion size
modprobe blk-snap diff_storage_minimum=262144
sleep 2s

# check module is ready
blksnap_version

TESTDIR=~/blk-snap-test
MPDIR=/mnt/blk-snap-test
DIFF_STORAGE=~/diff_storage/
rm -rf ${TESTDIR}
rm -rf ${MPDIR}
rm -rf ${DIFF_STORAGE}
mkdir -p ${TESTDIR}
mkdir -p ${MPDIR}
mkdir -p ${DIFF_STORAGE}

# create first device
IMAGEFILE_1=${TESTDIR}/simple_1.img
imagefile_make ${IMAGEFILE_1} 4096
echo "new image file ${IMAGEFILE_1}"

DEVICE_1=$(loop_device_attach ${IMAGEFILE_1})
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

generate_files ${MOUNTPOINT_1} "before" 5
drop_cache

fallocate --length 256MiB "${DIFF_STORAGE}/diff_storage"

# full
echo "First snapshot for just attached devices"
blksnap_snapshot_create ${DEVICE_1}
blksnap_snapshot_append "${DIFF_STORAGE}/diff_storage"
blksnap_snapshot_take

blksnap_readcbt ${DEVICE_1} ${TESTDIR}/cbt0.map
echo "CBT map size: "
stat -c%s "${TESTDIR}/cbt0.map"
generate_bulk_MB ${MOUNTPOINT_1} "full" 10
check_files ${MOUNTPOINT_1}
blksnap_readcbt ${DEVICE_1} ${TESTDIR}/cbt0_.map

blksnap_snapshot_destroy
cmp -l ${TESTDIR}/cbt0.map ${TESTDIR}/cbt0_.map

# increment 1
echo "First increment"
blksnap_snapshot_create ${DEVICE_1}
blksnap_snapshot_append "${DIFF_STORAGE}/diff_storage"
blksnap_snapshot_take

blksnap_readcbt ${DEVICE_1} ${TESTDIR}/cbt1.map
generate_bulk_MB ${MOUNTPOINT_1} "inc-first" 10
check_files ${MOUNTPOINT_1}
blksnap_readcbt ${DEVICE_1} ${TESTDIR}/cbt1_.map

blksnap_snapshot_destroy
cmp -l ${TESTDIR}/cbt1.map ${TESTDIR}/cbt1_.map

# increment 2
echo "Second increment"
blksnap_snapshot_create ${DEVICE_1}
blksnap_snapshot_append "${DIFF_STORAGE}/diff_storage"
blksnap_snapshot_take

blksnap_readcbt ${DEVICE_1} ${TESTDIR}/cbt2.map
generate_bulk_MB ${MOUNTPOINT_1} "inc-second" 10
check_files ${MOUNTPOINT_1}
blksnap_readcbt ${DEVICE_1} ${TESTDIR}/cbt2_.map

blksnap_snapshot_destroy
cmp -l ${TESTDIR}/cbt2.map ${TESTDIR}/cbt2_.map

# increment 3
echo "Second increment"
blksnap_snapshot_create ${DEVICE_1}
blksnap_snapshot_append "${DIFF_STORAGE}/diff_storage"
blksnap_snapshot_take

blksnap_readcbt ${DEVICE_1} ${TESTDIR}/cbt3.map
fallocate --length 2MiB "${MOUNTPOINT_1}/dirty_file"
blksnap_markdirty ${DEVICE_1} "${MOUNTPOINT_1}/dirty_file"
blksnap_readcbt ${DEVICE_1} ${TESTDIR}/cbt3_.map

blksnap_snapshot_destroy
set +e
echo "dirty blocks:"
cmp -l ${TESTDIR}/cbt3.map ${TESTDIR}/cbt3_.map 2>&1
set -e

echo "Destroy first device"
echo "press..."
umount ${MOUNTPOINT_1}
loop_device_detach ${DEVICE_1}
imagefile_cleanup ${IMAGEFILE_1}

echo "Unload module"
echo 0 > /sys/kernel/livepatch/blk_snap/enabled
sleep 2s
modprobe -r blk-snap

echo "Change tracking test finish"
echo "---"