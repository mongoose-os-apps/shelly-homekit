#!/bin/bash
# version 0.5

if [ "$(brew list | grep -i "jq" | awk '{print $1}' 2>/dev/null)" != "jq" ]; then
	echo -e '\033[1mInstalling jq...\033[0m'
	brew install jq
fi

if [ "$(brew list | grep -i "wget" | awk '{print $1}' 2>/dev/null)" != "wget" ]; then
	echo -e '\033[1mInstalling wget...\033[0m'
	brew install wget
fi

function convert_to_integer {
 echo "$@" | awk -F "." '{ printf("%03d%03d%03d", $1,$2,$3); }';
}

function download {
 wget "$@" -O shelly-flash.zip
}

function flash {
	download "$@"
	if [ -f shelly-flash.zip ];then
		while true; do
			read -p "Are you sure you want to flash $device to firmware version $lfw ?" yn
			case $yn in
				[Yy]* )  echo "Now Flashing.."; curl -F file=shelly-flash.zip http://$device/update; break;;
				[Nn]* ) echo "Skipping..";break;;
				* ) echo "Please answer yes or no.";;
			esac
		done
	fi
	echo "waiting for $device to reboot"
	sleep 10
	if [ $(curl -qsS http://$device/rpc/Sys.GetInfo | jq -r .fw_version) == $lfw ];then
		echo "Successfully flashed $device to $lfw"
	else
		echo "Flash failed!!!"
	fi
	rm -f shelly-flash.zip
	read -p "Press enter to continue"
}

lfw=$(curl --silent "https://api.github.com/repos/mongoose-os-apps/shelly-homekit/releases/latest" | jq -r .tag_name)
dlurls=$(curl --silent "https://api.github.com/repos/mongoose-os-apps/shelly-homekit/releases/latest" | jq -r '.assets[] | select(.name).browser_download_url')


for device in $(arp -a | awk '/shelly/ {print $1}' | awk '{gsub(/\n/,"n")}1'); do
	flash=null
	clear
	if [[ $(curl -qsS http://$device/rpc/Sys.GetInfo) == "Not Found" ]]; then
		continue
	fi

	cfw=$(curl -qsS http://$device/rpc/Sys.GetInfo | jq -r .fw_version)
	type=$(curl -qsS http://$device/rpc/Sys.GetInfo | jq -r .app)

	case $type in
		switch1)	
			model="Shelly1.";;
		switch1pm)
			model="Shelly1PM.";;
		switch25)	
			model="Shelly25.";;
		shelly-plug-s)	
			model="ShellyPlugS.";;
		*) ;;
	esac

	for url in $dlurls; do
		if [[ $url == *$model* ]]; then
			dlurl=$url
			break
		fi
	done

	echo "Host: $device"
	echo "Type: $type"
	echo "Model: $model"
	echo "Current: $cfw"
	echo "Latest: $lfw"
	echo "DURL: $dlurl"
	
	cfw_V=$(convert_to_integer $cfw)
	lfw_V=$(convert_to_integer $lfw)
	# echo "$cfw_V"
	# echo "$lfw_V"
	# echo "$lfw_V $cfw_V -p" | dc
	
	if [ $(echo "$lfw_V $cfw_V -p" | dc) == 1 ]; then
		while true; do
		    read -p "Do you wish to flash $device to firmware version $lfw ?" yn
		    case $yn in
		        [Yy]* )  flash="yes"; break;;
		        [Nn]* ) flash="no";break;;
		        * ) echo "Please answer yes or no.";;
		    esac
		done
	else
		echo "$device DOSE NOT NEED UPDATING..."
		read -p "Press enter to continue"
	fi

	if [ "$flash" = "yes" ]; then
		flash $dlurl
	elif [ "$flash" = "no" ]; then
		echo "Skipping Flash..."
		read -p "Press enter to continue"
	fi

done
