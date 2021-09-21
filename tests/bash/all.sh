#!/bin/bash -e

echo "Execute all tests"
echo "***"

for SCRIPT in $(ls ????-*.sh)
do
	. ${SCRIPT}
done

echo "***"
echo "Complete"
