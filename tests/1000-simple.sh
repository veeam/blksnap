#!/bin/bash -e
# [TBD]
# Copyright (C) 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
#
# This file is part of blksnap-tests
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

. ./functions.sh
. ./blksnap.sh

echo "---"
echo "Simple test start"

modprobe blksnap
sleep 2s

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
imagefile_make ${IMAGEFILE_1} 64
echo "new image file ${IMAGEFILE_1}"

DEVICE_1=$(loop_device_attach ${IMAGEFILE_1})
echo "new device ${DEVICE_1}"

MOUNTPOINT_1=${MPDIR}/simple_1
mkdir -p ${MOUNTPOINT_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

# create second device
IMAGEFILE_2=${TESTDIR}/simple_2.img
imagefile_make ${IMAGEFILE_2} 128
echo "new image file ${IMAGEFILE_2}"

DEVICE_2=$(loop_device_attach ${IMAGEFILE_2})
echo "new device ${DEVICE_2}"

MOUNTPOINT_2=${MPDIR}/simple_2
mkdir -p ${MOUNTPOINT_2}
mount ${DEVICE_2} ${MOUNTPOINT_2}

generate_files ${MOUNTPOINT_1} "before" 9
drop_cache

echo "Block device prepared, press ..."
#read -n 1

blksnap_snapshot_create "${DEVICE_1} ${DEVICE_2}"

DIFF_STORAGE=~/diff_storage0
rm -f ${DIFF_STORAGE}
fallocate --length 1GiB ${DIFF_STORAGE}
blksnap_snapshot_append ${DIFF_STORAGE}

blksnap_snapshot_take

echo "Snapshot was token, press ..."
read -n 1

blksnap_snapshot_collect_all

echo "Write to original"
#echo "Write something" > ${MOUNTPOINT_1}/something.txt
generate_files ${MOUNTPOINT_1} "after" 3
drop_cache

check_files ${MOUNTPOINT_1}

echo "Check snapshots"
IMAGE_1=${TESTDIR}/image0
mkdir -p ${IMAGE_1}
mount /dev/blksnap-image0 ${IMAGE_1}
check_files ${IMAGE_1}

echo "Write to snapshot"
generate_files ${IMAGE_1} "snapshot" 3

drop_cache
umount /dev/blksnap-image0
mount /dev/blksnap-image0 ${IMAGE_1}

check_files ${IMAGE_1}

umount ${IMAGE_1}

#dd if=/dev/blksnap-image0 of=${TESTDIR}/image0 bs=1M
#generate_files ${MOUNTPOINT_1} "after" 3
#dd if=/dev/blksnap-image0 of=${TESTDIR}/image0 bs=1M

#dd if=/dev/blksnap-image1 of=${TESTDIR}/image1 bs=1M
#dd if=/dev/blksnap-image0 of=${TESTDIR}/image0 bs=4096 count=1
#dd if=/dev/blksnap-image1 of=${TESTDIR}/image1 bs=4096 count=1

blksnap_snapshot_destroy

echo "Destroy snapshot, press ..."
#read -n 1

rm ${DIFF_STORAGE}

drop_cache
umount ${DEVICE_1}
mount ${DEVICE_1} ${MOUNTPOINT_1}

check_files ${MOUNTPOINT_1}

echo "Destroy second device"
umount ${MOUNTPOINT_2}
loop_device_detach ${DEVICE_2}
imagefile_cleanup ${IMAGEFILE_2}

echo "Destroy first device"
umount ${MOUNTPOINT_1}
loop_device_detach ${DEVICE_1}
imagefile_cleanup ${IMAGEFILE_1}

echo "Tracking device info:"
blksnap_tracker_collect

echo "Unload module"
modprobe -r blksnap

echo "Simple test finish"
echo "---"
