#!/bin/bash
# 
#  Copyright (c) 2020 Andrew Blackburn
#  All rights reserved
# 
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
# 
#      http://www.apache.org/licenses/LICENSE-2.0
#  
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#  
#  This script will probe for any shelly device on the network and it will
#  attemp to update them to the lastest firmware version availible.
#  This script will not flash any firmware to a device that is not already on a
#  version of this firmware, if you are looking to flash your device from stock
#  or any other firmware please follow instructions here: 
#  https://github.com/mongoose-os-apps/shelly-homekit/blob/master/README.md
#  
#  -f, --flash         Flash the lastest available firmware.
#  -c, --check-only    only check for updates.
#  -h, --help          This help text.
# 
#  usage: ./flash_shellys.sh -f


function install_brew {
  echo -e '\033[1mInstalling brew...\033[0m'
  echo -e '\033[1mPlease follow instructions...\033[0m'
  echo -e '\033[1mYou will be asked for your password to install breww...\033[0m'
  echo -e '\033[1m \033[0m'
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
}

if [ "$(which brew 2>/dev/null)" == "" ]; then
  while true; do
    read -p "brew is not installed, would you like to install it ?" yn
    case $yn in
        [Yy]* ) install_brew; break;;
        [Nn]* ) echo "brew is required for this script to fuction. now exiting..."; exit;;
        * ) echo "Please answer yes or no.";;
    esac
  done
fi

if [ "$(which timeout 2>/dev/null)" == "" ]; then
  echo -e '\033[1mInstalling coreutils...\033[0m'
  brew install coreutils
fi

if [ "$(which jq  2>/dev/null)" == "" ]; then
  echo -e '\033[1mInstalling jq...\033[0m'
  brew install jq
fi

function convert_to_integer {
  echo "$@" | awk -F "." '{ printf("%03d%03d%03d", $1,$2,$3); }';
}

function download {
  curl -L -o shelly-flash.zip "$1"
}

function write_flash {
  download "$3"
  if [ -f shelly-flash.zip ];then
    while true; do
      read -p "Are you sure you want to flash $1 to firmware version $2 ? " yn
      case $yn in
        [Yy]* )  echo "Now Flashing.."; curl -F file=shelly-flash.zip http://$1/update; break;;
        [Nn]* ) echo "Skipping..";break;;
        * ) echo "Please answer yes or no.";;
      esac
    done
  fi
  echo "waiting for $1 to reboot"
  sleep 10
  if [ $(curl -qsS -m 5 http://$1/rpc/Sys.GetInfo | jq -r .fw_version) == $2 ];then
    echo "Successfully flashed $1 to $2"
  else
    echo "Flash failed!!!"
  fi
  rm -f shelly-flash.zip
  read -p "Press enter to continue"
}

function device-scan {
  echo -e '\033[1mScanning for Shelly devices...\033[0m'
  release_info=$(curl -qsS -m 5 https://api.github.com/repos/mongoose-os-apps/shelly-homekit/releases/latest)
  lfw=$(echo "$release_info" | jq -r .tag_name)

  for device in $(timeout 2 dns-sd -B _hap . | awk '/shelly/ {print $7}'); do
    device="$device.local"
    flash=null
    if [[ $(curl -qsS -m 5 http://$device/rpc/Shelly.GetInfo) == "Not Found" ]]; then
      continue
    fi

    info=$(curl -qsS -m 5 http://$device/rpc/Shelly.GetInfo)
    cfw=$(echo "$info" | jq -r .version)
    type=$(echo "$info" | jq -r .app)
    model=$(echo "$info" | jq -r .model) 

    if [[ $model != *Shelly* ]]; then
      case $type in
        switch1)  
          model="Shelly1";;
        switch1pm)
          model="Shelly1PM";;
        switch25) 
          model="Shelly25";;
        shelly-plug-s)  
          model="ShellyPlugS";;
        *) ;;
      esac
    fi

    dlurl=$(echo "$release_info" | jq -r '.assets[] | select(.name=="shelly-homekit-'$model'.zip").browser_download_url')

    if [ $scriptmode == "flash" ];then 
      clear
      echo "Host: $device"
      echo "Type: $type"
      echo "Model: $model"
      echo "Current: $cfw"
      echo "Latest: $lfw"
      echo "DURL: $dlurl"

      cfw=2.0.11
      cfw_V=$(convert_to_integer $cfw)
      lfw_V=$(convert_to_integer $lfw)
  
      if [ $(echo "$lfw_V $cfw_V -p" | dc) == 1 ]; then
        while true; do
          read -p "Do you wish to flash $device to firmware version $lfw ? " yn
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
        write_flash $device $lfw $dlurl
      elif [ "$flash" = "no" ]; then
        echo "Skipping Flash..."
        read -p "Press enter to continue"
      fi
    else
      echo "Host: $device"
      echo "Type: $type"
      echo "Model: $model"
      echo "Current: $cfw"
      echo "Latest: $lfw"
      echo " "
    fi
  done
}

function help {
  echo " -f, --flash         Flash the lastest available firmware."
  echo " -c, --check-only    only check for updates."
  echo " -h, --help          This help text."
}

if [ -n "$1" ]; then 
  if [ $1 == "-h" -o $1 == "--help" ]; then
    help
  elif [ $1 == "-f" -o $1 == "--flash" ]; then
    scriptmode="flash"
    device-scan
  elif [ $1 == "-c" -o $1 == "--check-only" ]; then
    scriptmode="check-only"
    device-scan
  fi
else
	help
fi
