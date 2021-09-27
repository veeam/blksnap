#!/bin/bash -e
BLKSNAP="$(cd ../../; pwd)/bin/blksnap"
ID=""

blksnap_version()
{
	${BLKSNAP} version
}

blksnap_snapshot_create_inmem()
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

blksnap_tracker_remove()
{
	local DEVICE=$1

	${BLKSNAP} tracker_remove --device=${DEVICE}
}
