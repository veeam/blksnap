#!/bin/bash -e
#
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
echo "Diff storage test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB diff_storage portion size
modprobe blksnap diff_storage_minimum=262144

# check module is ready
blksnap_version

if [ -z $1 ]
then
	TEST_DIR=$(realpath ~/blksnap-test)
else
	TEST_DIR=$(realpath $1)
fi
MP_TEST_DIR=$(stat -c %m ${TEST_DIR})
DEVICE="/dev/block/"$(mountpoint -d ${MP_TEST_DIR})
echo "Test directory [${TEST_DIR}] on device [${DEVICE}] selected"

RELATIVE_TEST_DIR=${TEST_DIR#${MP_TEST_DIR}}

MP_DIR=/mnt/blksnap-test
DIFF_STORAGE=${TEST_DIR}/diff_storage/

rm -rf ${TEST_DIR}
rm -rf ${MP_DIR}
rm -rf ${DIFF_STORAGE}
mkdir -p ${TEST_DIR}
mkdir -p ${MP_DIR}
mkdir -p ${DIFF_STORAGE}


generate_block_MB ${TEST_DIR} "before" 10
check_files ${TEST_DIR}

blksnap_snapshot_create "${DEVICE}"

blksnap_stretch_snapshot ${DIFF_STORAGE} 1024
echo "Waiting for creating first portion"
sleep 2s

blksnap_snapshot_take

generate_block_MB ${TEST_DIR} "after" 1000
check_files ${TEST_DIR}

IMAGE=${MP_DIR}/image0
mkdir -p ${IMAGE}

echo "Mount image"
mount /dev/blksnap-image0 ${IMAGE}
# for XFS filesystem nouuid option needed
#mount -o nouuid /dev/blksnap-image0 ${IMAGE}

check_files ${IMAGE}/${RELATIVE_TEST_DIR}

echo "Umount image"
umount ${IMAGE}

echo "Destroy snapshot"
blksnap_snapshot_destroy

echo "Waiting for streach process terminate"
sleep 2s

echo "Unload module"
modprobe -r blksnap

echo "Diff storage test finish"
echo "---"
