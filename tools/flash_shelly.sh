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
#  -u, --update        Update device(s) to the lastest available firmware.
#  -c, --check-only    Only check for updates.
#  -h, --help          This help text.
#
#  usage: ./flash_shelly.sh -u
#  usage: ./flash_shelly.sh -u shelly1-034FFF.local


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
  if [ $4 == "false" ]; then
    download "$3"
  fi
  if [ -f shelly-flash.zip ] || [ $4 == "true" ];then
    while true; do
      read -p "Are you sure you want to flash $1 to firmware version $2 ? " yn
      case $yn in
        [Yy]* ) echo "Now Flashing.."; echo $($flashcmd); break;;
        [Nn]* ) echo "Skipping..";break;;
        * ) echo "Please answer yes or no.";;
      esac
    done
  fi
  echo "waiting for $1 to reboot"
  sleep 10
  if [ $4 == "false" ]; then
    if [ $(curl -qs -m 5 http://$1/rpc/Shelly.GetInfo | jq -r .version) == $2 ];then
      echo "Successfully flashed $1 to $2"
    else
      echo "Flash failed!!!"
    fi
    rm -f shelly-flash.zip
  else
    if [ $(curl -qs -m 5 http://$1/Shelly.GetInfo | jq -r .fw | awk '{split($0,a,"/v"); print a[2]}' | awk '{split($0,a,"@"); print a[1]}') == $2 ];then
      echo "Successfully flashed $1 to $2"
    else
      echo "Flash failed!!!"
    fi
  fi
  read -p "Press enter to continue"
}

function probe_info {
  official="false"
  flash=null
  device="$2"
  lfw=$(echo "$release_info" | jq -r .tag_name)
  info=$(curl -qs -m 5 http://$device/rpc/Shelly.GetInfo)||info="error"
  if [[ $info == "error" ]]; then
    echo "Could not resolve host: $device"
    if [[ -z $device ]]; then
      read -p "Press enter to continue"
      continue
    else
      exit
    fi
  fi
  if [[ $info == "Not Found" ]]; then
    info=$(curl -qsS -m 5 http://$device/Shelly.GetInfo)
    if [[ -z $device ]]; then
      continue
    fi
  fi

  if [[ $(echo "$info" | jq -r .version) != "null" ]]; then
    flashcmd="curl -F file=@shelly-flash.zip http://$device/update"
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
        dimmer1)
          model="ShellyDimmer";;
        rgbw2)
          model="ShellyRGBW2";;
        *) ;;
      esac
      dlurl=$(echo "$release_info" | jq -r '.assets[] | select(.name=="shelly-homekit-'$model'.zip").browser_download_url')
    fi
  else
    official="true"
    cfw=$(echo "$info" | jq -r .fw | awk '{split($0,a,"/v"); print a[2]}' | awk '{split($0,a,"@"); print a[1]}')
    type=$(echo "$info" | jq -r .type)
    case $type in
      SHSW-1)
        model="Shelly1";;
      SHSW-PM)
        model="Shelly1PM";;
      SHSW-25)
        model="Shelly25";;
      SHPLG-S)
        model="ShellyPlugS";;
      SHDM-1)
        model="ShellyDimmer";;
      SHRGBW2)
        model="ShellyRGBW2";;
      *) ;;
    esac
    dlurl=$(echo "$release_info" | jq -r '.assets[] | select(.name=="shelly-homekit-'$model'.zip").browser_download_url')
    flashcmd="curl http://$device/ota?url=$dlurl"
  fi
  if [ -z $dlurl ]; then
    lfw=0
  fi

  if [ $1 == "update" ]; then
    clear
    echo "Host: $device"
    echo "Model: $model"
    echo "Current: $cfw"
    echo "Latest: $lfw"
    echo "DURL: $dlurl"

    cfw_V=$(convert_to_integer $cfw)
    lfw_V=$(convert_to_integer $lfw)

    if [ $(echo "$lfw_V $cfw_V -p" | dc) -ge 1 ] || [ $official == "true" ] && [ ! -z $dlurl ]; then
      while true; do
        read -p "Do you wish to flash $device to firmware version $lfw ? " yn
        case $yn in
          [Yy]* )  flash="yes"; break;;
          [Nn]* ) flash="no";break;;
          * ) echo "Please answer yes or no.";;
        esac
      done
    elif [ -z $dlurl ]; then
      echo "$device IS NOT SUPPORTED YET..."
      read -p "Press enter to continue"
    else
      echo "$device DOSE NOT NEED UPDATING..."
      read -p "Press enter to continue"
    fi

    if [ "$flash" = "yes" ]; then
      write_flash $device $lfw $dlurl $official
    elif [ "$flash" = "no" ]; then
      echo "Skipping Flash..."
      read -p "Press enter to continue"
    fi
  else
    if [ -z $dlurl ]; then
      lfw="Not Supported"
    fi
    echo "Host: $device"
    echo "Model: $model"
    echo "Current: $cfw"
    echo "Latest: $lfw"
    echo " "
  fi
}

function device_scan {
  if [ "$2" != "null" ]; then
    probe_info $1 $2
  else
    echo -e '\033[1mScanning for Shelly devices...\033[0m'
    for device in $(timeout 2 dns-sd -B _http . | awk '/shelly/ {print $7}'); do
      probe_info $1 $device.local
    done
  fi
}

function help {
  echo "Shelly HomeKit flashing script utility"
  echo "Usage: $1 {-c|-u} $2{hostname optional}"
  echo " -c, --check-only    Only check for updates."
  echo " -u, --update        Update device(s) to the lastest available firmware."
  echo " -h, --help          This help text"
}

case $1 in
  -h|--help)
    help; exit;;
  -c|--check-only)
    scriptmode="check-only";;
  -u|--update)
    scriptmode="update";;
  *)
    if [ -n "$1" ]; then
        echo "flash_shelly: option $1: is unknown"
    fi
    echo "flash_shelly: try flash_shelly --help"
    exit;;
esac

release_info=$(curl -qsS -m 5 https://api.github.com/repos/mongoose-os-apps/shelly-homekit/releases/latest)
if [ -n "$2" ]; then
  device_scan $scriptmode $2
else
  device_scan $scriptmode null
fi
