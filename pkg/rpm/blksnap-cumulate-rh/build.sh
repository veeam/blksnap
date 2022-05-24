#!/bin/bash -e

. /etc/os-release
PACKAGE_RELEASE=${ID/-/_}${VERSION_ID/-/_}

PACKAGE_NAME="blksnap"

PACKAGE_VERSION="$1"
PACKAGE_VENDOR="$2"

if [[ -z "${PACKAGE_VERSION}" ]]
then
	echo >&2 "Version parameters is not set."
	exit
fi
if [[ -z "${PACKAGE_VENDOR}" ]]
then
	echo >&2 "Vendor parameters is not set."
	PACKAGE_VENDOR="Veeam Software Group GmbH"
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
# copy '-loader' file
cp ${PROJECT_DIR}/${PACKAGE_NAME}-loader ${BUILD_DIR}/SOURCES/${PACKAGE_NAME}-loader

# generate module sources tarbal
SRC_DIR=${BUILD_DIR}/SOURCES/${PACKAGE_NAME}-${PACKAGE_VERSION}/module
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
sed -i "s/#PACKAGE_VERSION#/${PACKAGE_VERSION}/g; s/#PACKAGE_VENDOR#/${PACKAGE_VENDOR}/g" ${SPECFILE}
echo " " >> ${SPECFILE}
echo "* $(date +'%a %b %d %Y') "$(whoami) >> ${SPECFILE}
echo "- Build ${PKG_VER}" >> ${SPECFILE}
echo "SPECS:"
ls ${BUILD_DIR}/SPECS

KVERSION=""
for KERN in $(ls /usr/src/kernels/)
do
	if [[ -d "/usr/src/kernels/${KERN}" ]]
	then
		if [[ -z ${KVERSION} ]]
		then
			KVERSION="${KERN}"
		else
			KVERSION+=" ${KERN}"
		fi
	fi
done

cd ${BUILD_DIR}
rpmbuild --ba SPECS/${PACKAGE_NAME}.spec --define="kversion ${KVERSION}"
cd ${PROJECT_DIR}
