#!/bin/bash -e

modprobe -r blksnap

echo 0 > /sys/kernel/livepatch/bdevfilter/enabled
sleep 3s
modprobe -r bdevfilter
