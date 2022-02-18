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
echo "Diff storage test"

# diff_storage_minimum=262144 - set 256 K sectors, it's 125MiB dikk_storage portion size
modprobe blksnap diff_storage_minimum=262144

# check module is ready
blksnap_version

if [ -z $1 ]
then
	TESTDIR=~/blksnap-test
else
	TESTDIR=$1
fi
DEVICE="/dev/block/"$(mountpoint -d $(stat -c %m ~/))
echo "Test directory [${TESTDIR}] on device [${DEVICE}] selected"

MPDIR=/mnt/blksnap-test
DIFF_STORAGE=${TESTDIR}/diff_storage/

rm -rf ${TESTDIR}
rm -rf ${MPDIR}
rm -rf ${DIFF_STORAGE}
mkdir -p ${TESTDIR}
mkdir -p ${MPDIR}
mkdir -p ${DIFF_STORAGE}



echo "Unload module"
modprobe -r blksnap

echo "Diff storage test finish"
echo "---"
