#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

if [ -f "/usr/bin/blksnap" ] || [ -f "/usr/sbin/blksnap" ]
then
	BLKSNAP=blksnap
else
	BLKSNAP="$(cd ../; pwd)/tools/blksnap/blksnap"
fi

ID=""
BLKSNAP_FILENAME=$(modinfo --field filename blksnap)
STRETCH_PROCESS_PID=""

blksnap_load()
{
	if [ ${BLKSNAP_FILENAME} = "(builtin)" ]
	then
		return
	fi

	modprobe blksnap $1
	sleep 2s
}

blksnap_unload()
{
	if [ ${BLKSNAP_FILENAME} = "(builtin)" ]
	then
		return
	fi

	echo "Unload module"
	modprobe -r blksnap 2>&1 || sleep 1 && modprobe -r blksnap && echo "Unload success"
}

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

blksnap_snapshot_appendstorage()
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
	STRETCH_PROCESS_PID=$!

	echo "Waiting for creating first portion"
	sleep 2s
}

blksnap_stretch_wait()
{
	echo "Waiting for streach process terminate"
	wait ${STRETCH_PROCESS_PID}
}
