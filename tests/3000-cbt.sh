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
echo "Change tracking test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB dikk_storage portion size
modprobe blksnap diff_storage_minimum=262144
sleep 2s

# check module is ready
blksnap_version

TESTDIR=~/blksnap-test
MPDIR=/mnt/blksnap-test
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
modprobe -r blksnap

echo "Change tracking test finish"
echo "---"
