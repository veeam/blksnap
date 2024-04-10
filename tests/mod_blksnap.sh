#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

if [ -f "/usr/bin/blksnap" ] || [ -f "/usr/sbin/blksnap" ]
then
	BLKSNAP=blksnap
else
	BLKSNAP="$(cd ../; pwd)/tools/blksnap/bin/blksnap"
fi

ID=""
BLKSNAP_FILENAME=$(modinfo --field filename veeamblksnap)
STRETCH_PROCESS_PID=""
STRETCH_DIFF_STORAGE=""

blksnap_load()
{
	if [ ${BLKSNAP_FILENAME} = "(builtin)" ]
	then
		return
	fi

	modprobe veeamblksnap $1
	sleep 2s
}

blksnap_unload()
{
	if [ ${BLKSNAP_FILENAME} = "(builtin)" ]
	then
		return
	fi

	echo "Unload module"
	modprobe -r veeamblksnap 2>&1 || sleep 1 && modprobe -r veeamblksnap && echo "Unload success"
}

blksnap_version()
{
	${BLKSNAP} version
}

blksnap_log_debug()
{
	${BLKSNAP} setlog --level 7 --filepath "$1"
}

blksnap_log()
{
	${BLKSNAP} setlog --level 4 --filepath "$1"
}

blksnap_log_disable()
{
	${BLKSNAP} setlog --disable
}

blksnap_snapshot_create()
{
	PARAM=""

	for DEVICE in $1
	do
		PARAM="${PARAM} --device ${DEVICE}"
	done

	PARAM="${PARAM} --diff_storage $2"
	PARAM="${PARAM} --limit $3"
	if [ -d $2 ]
	then
		STRETCH_DIFF_STORAGE=$2
	fi

	ID=$(${BLKSNAP} snapshot_create ${PARAM})
	echo "New snapshot ${ID} was created"
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

blksnap_snapshot_collect()
{
	echo "Collect snapshots"

	${BLKSNAP} snapshot_collect
}

blksnap_attach()
{
	local DEVICE=$1

	${BLKSNAP} attach --device=${DEVICE}
}

blksnap_detach()
{
	local DEVICE=$1

	${BLKSNAP} detach --device=${DEVICE}
}

blksnap_cbtinfo()
{
	local DEVICE=$1

	${BLKSNAP} cbtinfo --device=${DEVICE} --file=${CBTMAP}
}

blksnap_readcbt()
{
	local DEVICE=$1
	local CBTMAP=$2

	${BLKSNAP} readcbtmap --device=${DEVICE} --file=${CBTMAP}
}

blksnap_markdirty()
{
	local DIRTYFILE=$2

	${BLKSNAP} markdirtyblock --file=${DIRTYFILE}
}

blksnap_snapshot_watcher()
{
	PARAM="--id=${ID}"
	if [ -e ${STRETCH_DIFF_STORAGE} ]
	then
		PARAM="${PARAM} --diff_storage ${STRETCH_DIFF_STORAGE} "
	fi
	${BLKSNAP} snapshot_watcher "${PARAM}" &
	STRETCH_PROCESS_PID=$!
}
blksnap_watcher_wait()
{
	echo "Waiting for streach process terminate"
	wait ${STRETCH_PROCESS_PID}
	STRETCH_DIFF_STORAGE=""
	STRETCH_PROCESS_PID=""
}

blksnap_get_image()
{
	${BLKSNAP} snapshot_info --field image --device $1
}

blksnap_cleanup()
{
	for ID in $(${BLKSNAP} snapshot_collect)
	do
		${BLKSNAP} snapshot_destroy --id=${ID}
	done
}
