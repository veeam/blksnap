#!/bin/bash

. /etc/os-release
PACKAGE_RELEASE=${ID/-/_}${VERSION_ID/-/_}

PACKAGE_NAME="blksnap"
#VERSION="1.0.0.0"
PACKAGE_VERSION="$1"
PACKAGE_VENDOR="Veeam Software Group GmbH"
#PACKAGE_VENDOR="$2"
if [ -z "${PACKAGE_VERSION}" ]
then
	echo >&2 "Version parameters is not set."
	exit
fi
if [ -z "${PACKAGE_VENDOR}" ]
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
cp ${PROJECT_DIR}/${PACKAGE_NAME}-preamble ${BUILD_DIR}/SOURCES/${PACKAGE_NAME}-${PACKAGE_VERSION}-preamble
# Generate own kernel-module-subpackage
# The package build system for SUSE does not allow to add own scriptlets for
# the kmp package uninstall script. So, I have to patch an existing one.
# If you know a better way, let me know.
cp /usr/lib/rpm/kernel-module-subpackage ${BUILD_DIR}/SOURCES/kernel-module-subpackage
cat ${PROJECT_DIR}/${PACKAGE_NAME}-unload.sh | sed -i '/^%preun/ r /dev/stdin' ${BUILD_DIR}/SOURCES/kernel-module-subpackage
# generate module sources tarbal
SRC_DIR=${BUILD_DIR}/SOURCES/${PACKAGE_NAME}-${PACKAGE_VERSION}/source
mkdir -p ${SRC_DIR}
cp ./module/*.c ${SRC_DIR}/
cp ./module/*.h ${SRC_DIR}/
cp ./module/Makefile* ${SRC_DIR}/
generate_version ${SRC_DIR}/version.h ${PACKAGE_VERSION}
CURR_DIR=$(pwd)
cd ${BUILD_DIR}/SOURCES
tar --format=gnu -zcf ${PACKAGE_NAME}-${PACKAGE_VERSION}.tar.gz ./${PACKAGE_NAME}-${PACKAGE_VERSION}
rm -rf ./${PACKAGE_NAME}-${PACKAGE_VERSION}
cd ${CURR_DIR}
echo "SOURCES:"
ls ${BUILD_DIR}/SOURCES/

# prepare spec files
rm -rf ${BUILD_DIR}/SPECS/*
SPECFILE=${BUILD_DIR}/SPECS/${PACKAGE_NAME}.spec
cp ${PROJECT_DIR}/${PACKAGE_NAME}.spec ${SPECFILE}
chmod +w ${SPECFILE}
sed -i "s/#PACKAGE_VERSION#/${PACKAGE_VERSION}/g; s/#PACKAGE_VENDOR#/${PACKAGE_VENDOR}/g; s/#PACKAGE_RELEASE#/${PACKAGE_RELEASE}/g" ${SPECFILE}
echo " " >> ${SPECFILE}
echo "* $(date +'%a %b %d %Y') "$(whoami) >> ${SPECFILE}
echo "- Build ${PKG_VER}" >> ${SPECFILE}
echo "SPECS:"
ls ${BUILD_DIR}/SPECS

cd ${BUILD_DIR}
rpmbuild --define "_topdir ${BUILD_DIR}" --ba SPECS/${PACKAGE_NAME}.spec
cd ${PROJECT_DIR}
