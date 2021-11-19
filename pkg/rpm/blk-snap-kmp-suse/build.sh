#!/bin/bash

NAME="blk-snap"
#VERSION="1.0.0.0"
VERSION="$1"
VENDOR="Veeam Software Group GmbH"
#VENDOR="$2"
if [ -z ${VERSION} ]
then
	echo >&2 "Version parameters is not set."
	exit
fi
if [ -z ${VENDOR} ]
then
	echo >&2 "Vendor parameters is not set."
	exit
fi

hash rpmbuild 2>/dev/null || { echo >&2 "The 'rpmbuild' package is required."; exit; }

set -e

PROJECT_DIR=$(pwd)
cd "../../../"

. ./pkg/build_functions.sh

BUILD_DIR="${HOME}/rpmbuild"
# rpmdev-setuptree
for DIR in BUILD BUILDROOT OTHER RPMS SOURCES SPECS SRPMS
do
	mkdir -p ${BUILD_DIR}/${DIR}
done

# prepare module sources
rm -rf ${BUILD_DIR}/SOURCES/*
# copy '-preamble' file
cp ${PROJECT_DIR}/${NAME}-preamble ${BUILD_DIR}/SOURCES/${NAME}-${VERSION}-preamble
# generate module sources tarbal
SRC_DIR=${BUILD_DIR}/SOURCES/${NAME}-${VERSION}/source
mkdir -p ${SRC_DIR}
cp ./module/*.c ${SRC_DIR}/
cp ./module/*.h ${SRC_DIR}/
cp ./module/Makefile* ${SRC_DIR}/
generate_version ${SRC_DIR}/version.h ${VERSION}
CURR_DIR=$(pwd)
cd ${BUILD_DIR}/SOURCES
tar --format=gnu -zcf ${NAME}-${VERSION}.tar.gz ./${NAME}-${VERSION}
rm -rf ./${NAME}-${VERSION}
cd ${CURR_DIR}
echo "SOURCES:"
ls ${BUILD_DIR}/SOURCES/

# prepare spec files
rm -rf ${BUILD_DIR}/SPECS/*
SPECFILE=${BUILD_DIR}/SPECS/${NAME}.spec
cp ${PROJECT_DIR}/${NAME}.spec ${SPECFILE}
chmod +w ${SPECFILE}
sed -i "s/#PACKAGE_VERSION#/${VERSION}/g; s/#PACKAGE_VENDOR#/${VENDOR}/g" ${SPECFILE}
echo " " >> ${SPECFILE}
echo "* $(date +'%a %b %d %Y') "$(whoami) >> ${SPECFILE}
echo "- Build ${PKG_VER}" >> ${SPECFILE}
echo "SPECS:"
ls ${BUILD_DIR}/SPECS

cd ${BUILD_DIR}
rpmbuild --ba SPECS/${NAME}.spec
cd ${PROJECT_DIR}
