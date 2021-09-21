#!/bin/bash -e

TESTDIR=/tmp/blk-snap-test
mkdir -p ${TESTDIR}

IMAGEFILE=${TESTDIR}/simple.img
imagefile_make ${IMAGEFILE} 64M
echo "new image file ${IMAGEFILE}"

DEVICE=$(loop_device_attach ${IMAGEFILE})
echo "new device ${DEVICE}"

blksnap_snapshot_create_inmem ${DEVICE}


blksnap_snapshot_destroy

loop_device_detach ${DEVICE}

imagefile_cleanup ${IMAGEFILE}
