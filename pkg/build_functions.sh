#!/bin/bash -e

generate_version()
{
	local FILEPATH=$1
	local VER_STR=$2
	local VER_MAJOR=$(cut -d "." -f1 <<< ${VER_STR})
	local VER_MINOR=$(cut -d "." -f2 <<< ${VER_STR})
	local VER_REVIS=$(cut -d "." -f3 <<< ${VER_STR})
	local VER_BUILD=$(cut -d "." -f4 <<< ${VER_STR})

	echo "/* SPDX-License-Identifier: GPL-2.0 */" > ${FILEPATH}
	echo "#pragma once" >> ${FILEPATH}
	echo "#define VERSION_MAJOR	${VER_MAJOR}" >> ${FILEPATH}
	echo "#define VERSION_MINOR	${VER_MINOR}" >> ${FILEPATH}
	echo "#define VERSION_REVISION	${VER_REVIS}" >> ${FILEPATH}
	echo "#define VERSION_BUILD	${VER_BUILD}" >> ${FILEPATH}
	echo "#define VERSION_STR	\"${VER_STR}\"" >> ${FILEPATH}
}
