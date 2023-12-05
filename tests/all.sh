#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

echo "Execute all tests"
echo "***"

for SCRIPT in $(ls ????-*.sh)
do
	. ${SCRIPT}
done

echo "***"
echo "Complete"
