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

imagefile_make()
{
	local FILEPATH=$1
	local SIZE=$2

	dd if=/dev/zero of=${FILEPATH} count=${SIZE} bs=1M
	mkfs.ext4 ${FILEPATH}
}

imagefile_cleanup()
{
	local FILEPATH=$1

	rm -f ${FILEPATH}
}

loop_device_attach()
{
	local FILEPATH=$1
	local DEVICE=""

	DEVICE=$(losetup -f --show ${FILEPATH})
	echo "${DEVICE}"
}

loop_device_detach()
{
	local DEVICE=$1

	losetup -d ${DEVICE}
}

generate_files()
{
	local TARGET_DIR=$1
	local PREFIX=$2
	local CNT=$3
	local GEN_FILE_PWD=$(pwd)

	echo "generate files in ${TARGET_DIR}"
	cd ${TARGET_DIR}

	for ((ITER = 0 ; ITER < ${CNT} ; ITER++))
	do
		local FILE="./${PREFIX}-${ITER}"
		local SZ=$RANDOM

		let "SZ = ${SZ} % 100 + 8"
		echo "file: ${FILE} size: ${SZ} sectors"
		dd if=/dev/urandom of=${FILE} count=${SZ} bs=512 >/dev/null 2>&1
		md5sum ${FILE} >> ${TARGET_DIR}/hash.md5
	done
	cd ${GEN_FILE_PWD}
	echo "generate complete"
}

generate_block_MB()
{
	local TARGET_DIR=$1
	local PREFIX=$2
	local SZ_MB=$3
	local ITER_SZ_MB=0
	local ITER=0
	local GEN_FILE_PWD=$(pwd)

	echo "generate files in ${TARGET_DIR}"
	cd ${TARGET_DIR}


	while [ ${ITER_SZ_MB} -lt ${SZ_MB} ]
	do
		local FILE="./${PREFIX}-${ITER}"
		local SZ=${RANDOM:0:1}

		SZ=$((SZ + 1))
		echo "file: ${FILE} size: ${SZ} MiB"
		dd if=/dev/urandom of=${FILE} count=${SZ} bs=1048576 >/dev/null 2>&1
		md5sum ${FILE} >> ${TARGET_DIR}/hash.md5

		ITER_SZ_MB=$((SZ + ITER_SZ_MB))
		ITER=$((ITER + 1))

		echo "processed ${ITER_SZ_MB} MiB"
	done
	cd ${GEN_FILE_PWD}
	echo "generate complete"
}

check_files()
{
	local TARGET_DIR=$1
	local CHECH_FILE_PWD=$(pwd)

	echo "Check files in ${TARGET_DIR}"
	cd ${TARGET_DIR}

	md5sum -c "./hash.md5"

	cd ${CHECH_FILE_PWD}
}

drop_cache()
{
	echo 3 > /proc/sys/vm/drop_caches
}
