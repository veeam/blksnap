if [ -d /sys/firmware/efi ]
then
	if [ -f /sys/firmware/efi/efivars/SecureBoot-* ]
	then
		SB_ENABLE=$(od --address-radix=n --format=u1 --skip=4 -N 1 /sys/firmware/efi/efivars/SecureBoot-* | tr -d ' ')
		if [ ${SB_ENABLE} = "1" ]
		then
			echo "Active Secure Boot detected."
			if which mokutil
			then
				if mokutil --list-enrolled | grep -qw "CN=Veeam Software Group GmbH"
				then
					echo "Veeam Software certificate is installed."
				else
					echo "DEPLOY_WARNING: Veeam Software certificate not found."
					echo "DEPLOY_WARNING: Install 'veeam-ueficert' package and complete MOK enrollment."
				fi
			else
				echo "DEPLOY_WARNING: 'mokutil' is not installed."
				echo "DEPLOY_WARNING: Install 'mokutil' package, install 'veeam-ueficert' package and complete MOK enrollment."
			fi
		fi
	fi
fi
