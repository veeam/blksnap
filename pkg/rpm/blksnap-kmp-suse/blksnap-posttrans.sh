set +x -e

function check_certificate()
{
	local MODULE_NAME="veeamblksnap"
	local NAME="blksnap"

	if ! modinfo ${MODULE_NAME} 1>/dev/null
	then
		echo "DEPLOY_WARNING: Failed to execute 'modinfo' for ${MODULE_NAME} module."
		echo "DEPLOY_WARNING: Check is '${NAME}' package is installed."
		return 1
	fi

	local MODULE_AUTHOR=$(modinfo ${MODULE_NAME} 2>/dev/null | grep "^author:" | sed 's/author:[[:space:]]*//')
	if [ $? -ne 0 ] || [ -z "${MODULE_AUTHOR}" ]
	then
		echo "DEPLOY_WARNING: Cannot get 'author' value for ${MODULE_NAME} module."
		echo "DEPLOY_WARNING: Check is '${NAME}' package is installed."
		return 1
	fi
	echo "Found module by ${MODULE_AUTHOR}"

	local MODULE_SIG_KEY=$(modinfo ${MODULE_NAME} 2>/dev/null | grep "^sig_key:" | sed 's/sig_key:[[:space:]]*//')
	if [ $? -ne 0 ] || [ -z "${MODULE_SIG_KEY}" ]
	then
		echo "DEPLOY_WARNING: Cannot get 'sig_key' value for ${MODULE_NAME} module."
		echo "DEPLOY_WARNING: Check is '${NAME}' package is installed."
		return 1
	fi
	echo "Found Veeam Software module with signature key: ${MODULE_SIG_KEY}."

	local SIG_KEY_CLEAN=$(echo "${MODULE_SIG_KEY}" | tr -d ': ' | tr '[:lower:]' '[:upper:]')

	local CERT_SN=$(mokutil --list-enrolled 2>/dev/null | awk -v cn="CN=${MODULE_AUTHOR}" -v key="${SIG_KEY_CLEAN}" '
		/Serial Number:/ {
			getline
			gsub(/^[[:space:]]+/, "")
			gsub(/[[:space:]]+$/, "")
			serial = $0
			clean = toupper(serial)
			gsub(/[: ]/, "", clean)
		}
		/Subject:/ {
			if ($0 ~ cn && clean == key) {
				print serial
				exit
			}
			serial = ""
			clean = ""
		}
	')
	if [ -z "${CERT_SN}" ]
	then
		echo "DEPLOY_WARNING: Veeam Software certificate matching the module signature was not found."
		echo "DEPLOY_WARNING: Module signature key: ${MODULE_SIG_KEY}."
		echo "DEPLOY_WARNING: Install 'veeam-ueficert' package and complete MOK enrollment."
		return 1
	fi
	echo "Found Veeam Software certificate with serial number: ${CERT_SN}."

	return 0
}

SB_ENABLE=""
if [ -d /sys/firmware/efi ]
then
	if [ -f /sys/firmware/efi/efivars/SecureBoot-* ]
	then
		SB_ENABLE=$(od --address-radix=n --format=u1 --skip=4 -N 1 /sys/firmware/efi/efivars/SecureBoot-* | tr -d ' ')
		if [ ${SB_ENABLE} = "1" ]
		then
			echo "Active Secure Boot detected."
			if which mokutil 1>/dev/null
			then
				if check_certificate
				then
					echo "Veeam Software certificate is installed."
				fi
			else
				echo "DEPLOY_WARNING: 'mokutil' is not installed."
				echo "DEPLOY_WARNING: Install 'mokutil' package, install 'veeam-ueficert' package and complete MOK enrollment."
			fi
		else
			echo "Secure Boot is not active."
		fi
	fi
fi
if [ -z ${SB_ENABLE} ]
then
	echo "Secure Boot was not detected."
fi
