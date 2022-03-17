#!/bin/bash
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

set -e
DEVICE_PATH="/tmp/loopbackfile.img"
#echo "Prepare device $DEVICE_PATH"

#echo "fill zero"
dd if=/dev/zero of=$DEVICE_PATH bs=1G count=1

#echo "mkfs"
mkfs.ext4 $DEVICE_PATH

LOOP_PATH=$(losetup -f --show $DEVICE_PATH)
echo $LOOP_PATH
