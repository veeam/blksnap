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

BLKSNAP="$(cd ../../; pwd)/tools/blksnap/bin/blksnap"
ID=""

blksnap_version()
{
	${BLKSNAP} version
}

blksnap_snapshot_create()
{
	PARAM=""

	for DEVICE in $1
	do
		PARAM="${PARAM} --device ${DEVICE}"
	done

	${BLKSNAP} version
	ID=$(${BLKSNAP} snapshot_create ${PARAM})
	echo "New snapshot ${ID} was created"
}

blksnap_snapshot_append()
{
	local FILE=$1

	echo "Append file ${FILE} to diff storage"
	${BLKSNAP} snapshot_appendstorage --id=${ID} --file=${FILE}
}

blksnap_snapshot_destroy()
{
	echo "Destroy snapshot ${ID}"
	${BLKSNAP} snapshot_destroy --id=${ID}
}

blksnap_snapshot_take()
{
	echo "Take snapshot ${ID}"

	${BLKSNAP} snapshot_take --id=${ID}
}

blksnap_snapshot_take()
{
	echo "Take snapshot ${ID}"

	${BLKSNAP} snapshot_take --id=${ID}
}

blksnap_snapshot_collect()
{
	echo "Collect snapshot ${ID}"

	${BLKSNAP} snapshot_collect --id=${ID}
}

blksnap_snapshot_collect_all()
{
	${BLKSNAP} snapshot_collect
}

blksnap_tracker_remove()
{
	local DEVICE=$1

	${BLKSNAP} tracker_remove --device=${DEVICE}
}

blksnap_tracker_collect()
{
	${BLKSNAP} tracker_collect
}

blksnap_readcbt()
{
	local DEVICE=$1
	local CBTMAP=$2

	${BLKSNAP} tracker_readcbtmap --device=${DEVICE} --file=${CBTMAP}
}

blksnap_markdirty()
{
	local DIRTYFILE=$2

	${BLKSNAP} tracker_markdirtyblock --file=${DIRTYFILE}
}

blksnap_stretch_snapshot()
{
	local DIFF_STORAGE_PATH=$1
	local LIMIT_MB=$2

	${BLKSNAP} stretch_snapshot --id=${ID} --path=${DIFF_STORAGE_PATH} --limit=${LIMIT_MB} &
}
