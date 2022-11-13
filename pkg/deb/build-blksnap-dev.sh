#!/bin/bash -e

if [ -n "$1" ]
then
	VERSION="$1"
else
	VERSION="1.0.0.0"
fi

CURR_DIR=$(pwd)
cd "../../"
ROOT_DIR=$(pwd)

BUILD_DIR="build/pkg"
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}

# build library
mkdir -p "lib/blksnap/bin"
cd "lib/blksnap/bin"
cmake ../
make
cd ${ROOT_DIR}

# copy library
mkdir -p ${BUILD_DIR}/usr/lib/
cp lib/libblksnap.a ${BUILD_DIR}/usr/lib/
INSTALL_FILES+=("usr/lib/*")
# copy include
mkdir -p ${BUILD_DIR}/usr/include/blksnap/
cp include/blksnap/* ${BUILD_DIR}/usr/include/blksnap/
INSTALL_FILES+=("usr/include/blksnap/*")

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
Source: blksnap-dev
Section: devel
Priority: optional
Maintainer: Veeam Software Group GmbH <veeam_team@veeam.com>
Build-Depends: debhelper (>= 9.0.0), bash, g++, cmake, uuid-dev, libboost-filesystem-dev
Homepage: https://github.org/veeam/blksnap

Package: blksnap-dev
Architecture: linux-any
Depends: \${misc:Depends}
Description: [TBD] The static library and header files for managing the
        blksnap kernel module.
EOF
chmod 0666 ${BUILD_DIR}/debian/control

cat > ${BUILD_DIR}/debian/copyright << EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: blksnap
Source: https://github.org/veeam/blksnap

Files: *
Copyright: 2022 Veeam Software Group GmbH <veeam_team@veeam.com>
License: LGPL-3+
 This program is free software; you can redistribute it and/or modify it under
 the terms of the GNU Library General Public License as published by the Free
 Software Foundation; either version 3 of the License, or (at your option) any
 later version.
 .
 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the GNU Library General Public License for
 more details.
 .
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 .
 On Debian systems, the complete text of the GNU Library General Public License
 version 3 can be found in \`/usr/share/common-licenses/LGPL-3'.
EOF

cat > ${BUILD_DIR}/debian/changelog << EOF
blksnap-dev (${VERSION}) stable; urgency=low

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
