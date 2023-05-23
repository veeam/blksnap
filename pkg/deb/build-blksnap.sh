#!/bin/bash -e

if [ -n "$1" ]
then
	VERSION="$1"
else
	VERSION="2.0.0.0"
fi

CURR_DIR=$(pwd)
cd "../../"

# prepare debian directory
rm -rf ./debian
cp -r ${CURR_DIR}/blksnap ./debian

cat > ./debian/changelog << EOF
blksnap (${VERSION}) stable; urgency=low

  * Release.
 -- Veeam Software Group GmbH <veeam_team@veeam.com>  $(date -R)
EOF

# build backage
dpkg-buildpackage -us -uc -b

cd ${CURR_DIR}
