#!/bin/bash -e
BLKSNAP="$(cd ../../; pwd)/bin/blksnap"
ID=""

blksnap_version()
{
	${BLKSNAP} version
}

blksnap_snapshot_create_inmem()
{
	DEVICE=$1

	${BLKSNAP} version
	ID=$(${BLKSNAP} snapshot_create --device ${DEVICE})
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
