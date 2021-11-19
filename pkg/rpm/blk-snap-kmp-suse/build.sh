#!/bin/bash

hash rpmbuild         2>/dev/null || { echo >&2 "The 'rpmbuild' package is required."; exit; }
hash rpmdev-setuptree 2>/dev/null || { echo >&2 "The 'rpmdevtoos' package is required."; exit; }
hash rpmdev-bumpspec  2>/dev/null || { echo >&2 "The 'rpmdevtoos' package is required."; exit; }

set -e

NAME="blk-snap"
#VERSION="1.0.0.0"
VERSION="$1"
VENDOR="Veeam Software Group GmbH"
#VENDOR="$2"

PROJECT_DIR=$(pwd)
cd "../../../"

. ./pkg/build_functions.sh

rpmdev-setuptree
BUILD_DIR="${HOME}/rpmbuild"

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
rpmdev-bumpspec --comment="Build ${VERSION}" --userstring=${USER} ${SPECFILE}
#echo " " >> ${SPECFILE}
#echo "* $(date +'%a %b %d %Y') "$(whoami) >> ${SPECFILE}
#echo "- Build ${PKG_VER}" >> ${SPECFILE}
echo "SPECS:"
ls ${BUILD_DIR}/SPECS

#tar -czf ./SOURCES/${NAME}.tar.gz ${NAME}
cd ${BUILD_DIR}
rpmbuild --ba SPECS/${NAME}.spec
cd ${PROJECT_DIR}
