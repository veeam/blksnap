#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

echo "Execute all tests"
echo "Output will be also logged with a .log file for each test script"
echo "***"

for SCRIPT in $(ls ????-*.sh)
do
	. ${SCRIPT} |& tee -a ${SCRIPT}.log
done

echo "***"
echo "Complete"
