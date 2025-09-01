#!/bin/bash -e

VERSION="$1"
ARCH="$2"
if [ -z ${VERSION} ]
then
	VERSION="1.0.0.0"
fi

CURR_DIR=$(pwd)
cd "../../"

. pkg/build_functions.sh

BUILD_DIR="build/pkg"
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}

# prepare module sources
mkdir -p ${BUILD_DIR}/src
cp module/*.c ${BUILD_DIR}/src/
cp module/*.h ${BUILD_DIR}/src/
cp module/Makefile* ${BUILD_DIR}/src/
generate_version ${BUILD_DIR}/src/version.h ${VERSION}

cp ./pkg/blksnap.dkms ${BUILD_DIR}/src/dkms.conf
sed -i 's/#PACKAGE_VERSION#/'${VERSION}'/g' ${BUILD_DIR}/src/dkms.conf

# prepare other package files
cp -r ${CURR_DIR}/blksnap-dkms ${BUILD_DIR}/debian
chmod +x ${BUILD_DIR}/debian/postinst
chmod +x ${BUILD_DIR}/debian/rules
if [ -n ${ARCH} ]
then
	if sed -i 's/amd64/'${ARCH}'/g' ${BUILD_DIR}/debian/control
	then
		echo "Set ${ARCH} architecture"
		cat ${BUILD_DIR}/debian/control
	else
		exit $?
	fi
fi

cat > ${BUILD_DIR}/debian/changelog << EOF
blksnap-dkms (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <veeam_team@veeam.com>  $(date -R)
EOF

cd ${BUILD_DIR} && dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}
