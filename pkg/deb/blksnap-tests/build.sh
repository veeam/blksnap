#!/bin/bash -e
NAME=blksnap-tests

if [ -n "$1" ]
then
	VERSION="$1"
else
	VERSION="1.0.0.0"
fi
if [ -n "$2" ]
then
	ARCH="$2"
else
	ARCH="amd64"
fi

CURR_DIR=$(pwd)
cd "../../../"
ROOT_DIR=$(pwd)

BUILD_DIR="build/pkg"
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}

SOURCE_DIR="tests"
TARGET_DIR="opt/blksnap/tests"

# build
rm -rf ${SOURCE_DIR}/cpp/bin/*
mkdir -p ${SOURCE_DIR}/cpp/bin/*
cd ${SOURCE_DIR}/cpp/bin
cmake ../
make
cd ${ROOT_DIR}

# copy binaries and scripts
mkdir -p ${BUILD_DIR}/${TARGET_DIR}
for FILE in ${SOURCE_DIR}/*
do
	if [ -f "${FILE}" ]
	then
		cp ${FILE} ${BUILD_DIR}/${TARGET_DIR}/
	fi
done
chmod +x ${BUILD_DIR}/${TARGET_DIR}/*
INSTALL_FILES+=(${TARGET_DIR}/*)

# prepare other package files
mkdir -p ${BUILD_DIR}/debian
mkdir -p ${BUILD_DIR}/debian/source

cat > ${BUILD_DIR}/debian/source/format << EOF
3.0 (native)
EOF

cat > ${BUILD_DIR}/debian/compat << EOF
9
EOF

cat > ${BUILD_DIR}/debian/control << EOF
Source: ${NAME}
Section: utils
Priority: standard
Maintainer: Veeam Software Group GmbH <veeam_team@veeam.com>
Build-Depends: debhelper (>= 9.0.0), bash,

Package: ${NAME}
Architecture: ${ARCH}
Provides: ${NAME}, ${NAME}-${VERSION}
Depends: blksnap-tools (= ${VERSION}), bash, ${misc:Depends}
Homepage: https://github.org/veeam/blksnap/
Description: [TBD] The tests for checking the blksnap kernel module.
EOF
chmod 0666 ${BUILD_DIR}/debian/control

cat > ${BUILD_DIR}/debian/copyright << EOF
Format: http://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: ${NAME}

Files: debian/*
Copyright: 2022 Veeam Software Group GmbH <https://www.veeam.com/contacts.html>
License: GNU GPL-2.0
EOF

cat > ${BUILD_DIR}/debian/changelog << EOF
${NAME} (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <https://www.veeam.com/contacts.html>  $(date -R)
EOF

cat > ${BUILD_DIR}/debian/rules << EOF
#!/usr/bin/make -f
# -*- makefile -*-
include /usr/share/dpkg/pkg-info.mk

%:
	dh \$@

EOF
chmod 0766 ${BUILD_DIR}/debian/rules

cd ${BUILD_DIR}

# enumerate installation files
for FILE in ${INSTALL_FILES[@]}
do
	echo "${FILE} "$(dirname ${FILE}) >> debian/install
done

# build backage
dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}

