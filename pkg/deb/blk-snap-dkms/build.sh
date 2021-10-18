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

VERSION="$1"
if [ -n "$1" ]
then
	VERSION="$1"
else
	VERSION="1.0.0.0"
fi

CURR_DIR=$(pwd)
BUILD_DIR="build/pkg"

cd "../../../"

mkdir -p ${BUILD_DIR}/src
cp module/*.c ${BUILD_DIR}/src/
cp module/*.h ${BUILD_DIR}/src/
cp module/Makefile* ${BUILD_DIR}/src/

cp -r pkg/deb/blk-snap-dkms/debian ${BUILD_DIR}
chmod 0666 ${BUILD_DIR}/debian/control
chmod 0766 ${BUILD_DIR}/debian/rules

generate_version ${BUILD_DIR}/src/version.h ${VERSION}
find ${BUILD_DIR} -type f -exec sed -i 's/#PACKAGE_VERSION#/'${VERSION}'/g' {} +

cd ${BUILD_DIR} && dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}
