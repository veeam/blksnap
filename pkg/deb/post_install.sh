if [ -z "$(dkms status -m blksnap -v #PACKAGE_VERSION# -k $(uname -r) | grep 'installed')" ]
then
	echo "DEPLOY_ERROR: 'linux-headers' package for the current '$(uname -r)' kernel was not found."
	echo "DEPLOY_ERROR: Install 'linux-headers-$(uname -r)' package into the system"
	echo "DEPLOY_ERROR: or Install latest 'linux-image-$(dpkg --print-architecture)' and 'linux-headers-$(dpkg --print-architecture)' packages and reboot the system."
fi
