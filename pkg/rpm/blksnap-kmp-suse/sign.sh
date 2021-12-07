#!/bin/bash -e

BUILD_DIR="${HOME}/rpmbuild"

# signing only if private key exist in current directory
if [ -f ./signing_key.pem ]
then
	echo "The current directory must contain a public 'signing_key.x509' and private 'signing_key.pem' keys."
	exit 1;
fi

for PKG in ${BUILD_DIR}/RPMS/*/blksnap-kmp*.rpm
do
	modsign-repackage -c ./signing_key.x509 -k ./signing_key.pem ${PKG}
done
exit 0
