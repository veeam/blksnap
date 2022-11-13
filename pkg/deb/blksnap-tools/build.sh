#!/bin/bash -e

if [ -n "$1" ]
then
	VERSION="$1"
else
	VERSION="1.0.0.0"
fi

CURR_DIR=$(pwd)
cd "../../../"
ROOT_DIR=$(pwd)

BUILD_DIR="build/pkg"
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}

SOURCE_DIR="tools/blksnap/bin"
TARGET_DIR="usr/sbin"

# build
rm -rf ${SOURCE_DIR}/*
mkdir -p ${SOURCE_DIR}
cd ${SOURCE_DIR}
cmake ../
make
cd ${ROOT_DIR}

# copy binaries
mkdir -p ${BUILD_DIR}/${TARGET_DIR}
cp ${SOURCE_DIR}/blksnap ${BUILD_DIR}/${TARGET_DIR}/
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
Source: blksnap-tools
Section: admin
Priority: optional
Maintainer: Veeam Software Group GmbH <veeam_team@veeam.com>
Build-Depends: debhelper (>= 9.0.0), g++, cmake, uuid-dev, libboost-program-options-dev, libboost-filesystem-dev
Homepage: https://github.org/veeam/blksnap

Package: blksnap-tools
Architecture: linux-any
Depends: \${shlibs:Depends}, \${misc:Depends}
Description: [TBD] The tools for managing the blksnap kernel module.
EOF
chmod 0666 ${BUILD_DIR}/debian/control

cat > ${BUILD_DIR}/debian/copyright << EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: blksnap
Source: https://github.org/veeam/blksnap

Files: *
Copyright: 2022 Veeam Software Group GmbH <veeam_team@veeam.com>
License: GPL-2+
 This package is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 .
 This package is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 .
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 .
 On Debian systems, the complete text of the GNU General Public License
 version 2 can be found in \`/usr/share/common-licenses/GPL-2'.
EOF

cat > ${BUILD_DIR}/debian/changelog << EOF
blksnap-tools (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <veeam_team@veeam.com>  $(date -R)
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

