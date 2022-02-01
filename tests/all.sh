#!/bin/bash -e

./build_and_install.sh

echo "Execute all tests"
echo "***"

for SCRIPT in $(ls ????-*.sh)
do
	. ${SCRIPT}
done

echo "***"
echo "Complete"
