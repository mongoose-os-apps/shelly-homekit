#!/bin/bash
#
#  Copyright (c) 2020 Andrew Blackburn & Deomid "rojer" Ryabkov
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
#  attemp to update them to the latest firmware version availible.
#  This script will not flash any firmware to a device that is not already on a
#  version of this firmware, if you are looking to flash your device from stock
#  or any other firmware please follow instructions here:
#  https://github.com/mongoose-os-apps/shelly-homekit/blob/master/README.md
#
#  Shelly HomeKit flashing script utility
#  Usage: $0 -{m|l|a|n|y|h} $1{hostname(s) optional}
#   -m {homekit|revert|keep}   Script mode.
#   -l                         List info of shelly device.
#   -a                         Run against all the devices on the network.
#   -n                         Do a dummy run through.
#   -y                         Do not ask any confirmation to perform the flash.
#   -h                         This help text.
#
#  usage: ./flash_shelly.sh -la
#  usage: ./flash_shelly.sh shelly1-034FFF.local


function check_brew {
  if [ "$(which brew 2>/dev/null)" != "" ]; then
    return 0
  fi

  while true; do
    read -p "brew is not installed, would you like to install it ?" yn
    case $yn in
        [Yy]* ) install_brew; break;;
        [Nn]* ) echo "brew is required for this script to fuction. now exiting..."; exit 1;;
        * ) echo "Please answer yes or no.";;
    esac
  done

  echo -e '\033[1mInstalling brew...\033[0m'
  echo -e '\033[1mPlease follow instructions...\033[0m'
  echo -e '\033[1mYou will be asked for your password to install breww...\033[0m'
  echo -e '\033[1m \033[0m'
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
}

if [ "$(which timeout 2>/dev/null)" == "" ]; then
  check_brew
  echo -e '\033[1mInstalling coreutils...\033[0m'
  brew install coreutils
fi

if [ "$(which jq  2>/dev/null)" == "" ]; then
  check_brew
  echo -e '\033[1mInstalling jq...\033[0m'
  brew install jq
fi

function convert_to_integer {
  echo "$@" | awk -F "." '{ printf("%03d%03d%03d", $1,$2,$3); }';
}

function write_flash {
  local flashed=false
  local device=$1
  local lfw=$2
  local durl=$3
  local cfw_type=$4

  if [ $cfw_type == "homekit" ]; then
    echo "Downloading Firmware.."
    curl  -qsS -L -o shelly-flash.zip $durl
    flashcmd="curl -qsS -F file=@shelly-flash.zip http://$device/update"
  else
    flashcmd="curl -qsS http://$device/ota?url=$dlurl"
  fi
  if [ -f shelly-flash.zip ] || [ $cfw_type == "stock" ];then
    echo "Now Flashing.."
    echo $($flashcmd)
  fi
  echo "waiting for $device to reboot"
  sleep 10
  if [ $cfw_type == "homekit" ]; then
    if [ $(curl -qs -m 5 http://$device/rpc/Shelly.GetInfo | jq -r .version) == $lfw ];then
      echo "Successfully flashed $device to $lfw"
    else
      echo "Flash failed!!!"
    fi
    rm -f shelly-flash.zip
  else
    if [ $(curl -qs -m 5 http://$device/Shelly.GetInfo | jq -r .fw | awk '{split($0,a,"/v"); print a[2]}' | awk '{split($0,a,"@"); print a[1]}') == $lfw ];then
      flashed=true
      echo "Successfully flashed $device to $lfw"
    else
      echo "still waiting for $device to reboot"
      sleep 10
    fi
    if [ flashed == true ] || [ $(curl -qs -m 5 http://$device/Shelly.GetInfo | jq -r .fw | awk '{split($0,a,"/v"); print a[2]}' | awk '{split($0,a,"@"); print a[1]}') == $lfw ];then
      echo "Successfully flashed $device to $lfw"
    else
      echo "Flash failed!!!"
    fi
  fi
}

function probe_info {
  local flash=null
  local info=null      # firmware versions info
  local model=null     # device model
  local lfw=null       # latest firmware availible
  local cfw=null       # current firmware on device
  local cfw_type=null  # current firmware type
  local device=$1
  local action=$2
  local dry_run=$3
  local mode=$4

  info=$(curl -qs -m 5 http://$device/rpc/Shelly.GetInfo)||info="error"
  if [[ $info == "error" ]]; then
    echo "Could not resolve host: $device"
    if [[ -z $device ]]; then
      read -p "Press enter to continue"
      continue
    else
      exit 1
    fi
  fi
  cfw_type="homekit"
  if [[ $info == "Not Found" ]]; then
    info=$(curl -qsS -m 5 http://$device/Shelly.GetInfo)
    cfw_type="stock"
    if [[ -z $device ]]; then
      continue
    fi
  fi

  if [[ $mode == "keep" ]]; then
    mode=$cfw_type
  fi

  if [[ $cfw_type == "homekit" ]]; then
    type=$(echo "$info" | jq -r .app)
    cfw=$(echo "$info" | jq -r .version)
    if [[ $mode == "stock" ]]; then
      case $type in
        switch1)
          model="SHSW-1";;
        switch1pm)
          model="SHSW-PM";;
        switch25)
          model="SHSW-25";;
        shelly-plug-s)
          model="SHPLG-S";;
        dimmer1)
          model="SHDM-1";;
        rgbw2)
          model="SHRGBW2";;
        *) ;;
      esac
      lfw=$(echo "$stock_release_info" | jq -r '.data."'$model'".version' | awk '{split($0,a,"/v"); print a[2]}' | awk '{split($0,a,"@"); print a[1]}')
      dlurl=$(echo "$stock_release_info" | jq -r '.data."'$model'".url')
    else
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
      fi
      lfw=$(echo "$homekit_release_info" | jq -r .tag_name)
      dlurl=$(echo "$homekit_release_info" | jq -r '.assets[] | select(.name=="shelly-homekit-'$model'.zip").browser_download_url')
    fi
  else
    cfw=$(echo "$info" | jq -r .fw | awk '{split($0,a,"/v"); print a[2]}' | awk '{split($0,a,"@"); print a[1]}')
    type=$(echo "$info" | jq -r .type)
    if [[ $mode == "homekit" ]]; then
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
      lfw=$(echo "$homekit_release_info" | jq -r .tag_name)
      dlurl="http://rojer.me/files/shelly/$lfw/shelly-homekit-$model.zip"
    else
      model=$type
      lfw=$(echo "$stock_release_info" | jq -r '.data."'$type'".version' | awk '{split($0,a,"/v"); print a[2]}' | awk '{split($0,a,"@"); print a[1]}')
      dlurl=$(echo "$stock_release_info" | jq -r '.data."'$type'".url')
    fi
    if [[ ! $(curl --head --silent --fail $dlurl 2> /dev/null) ]]; then
      unset dlurl
    fi
  fi
  if [ -z $dlurl ]; then
    lfw="Not Supported"
  fi

  if [ $action != "list" ]; then
    echo "Host: $device"
    echo "Model: $model"
    if [ $cfw_type == "homekit" ]; then
      echo "Current: HomeKit $cfw"
    else
      echo "Current: Official $cfw"
    fi
    if [ $mode == "homekit" ]; then
      echo "Latest: HomeKit $lfw"
    else
      echo "Latest: Official $lfw"
    fi
    echo "DURL: $dlurl"

    cfw_V=$(convert_to_integer $cfw)
    lfw_V=$(convert_to_integer $lfw)

    if [ $(echo "$lfw_V $cfw_V -p" | dc) -ge 1 ]; then
      if [ $cfw_type == "homekit" ] && [ $mode == "homekit" ]; then
        perform_flash=true
      elif [ $cfw_type == "stock" ] && [ $mode == "stock" ]; then
        perform_flash=true
      elif [ $mode == "keep" ]; then
        perform_flash=true
      fi
    elif [ $cfw_type == "stock" ] && [ $mode == "homekit" ] && [[ ! -z $dlurl ]]; then
      perform_flash=true
    elif [ $cfw_type == "homekit" ] && [ $mode == "stock" ] && [[ ! -z $dlurl ]]; then
      perform_flash=true
    else
      perform_flash=false
    fi
    if [ $perform_flash == true ] && [ $dry_run == false ] && [ $silent_run == false ]; then
      while true; do
        read -p "Do you wish to flash $device to firmware version $lfw ? " yn
        case $yn in
          [Yy]* )  flash="yes"; break;;
          [Nn]* ) flash="no";break;;
          * ) echo "Please answer yes or no.";;
        esac
      done
    elif [ $perform_flash == true ] && [ $dry_run == false ] && [ $silent_run == true ]; then
      flash="yes"
    elif [ $perform_flash == true ] && [ $dry_run == true ]; then
      if [[ $mode == "stock" ]] && [ $cfw_type == "homekit" ]; then
        local keyword="converted to Official firmware"
      elif [ $cfw_type == "homekit" ] || [[ $mode == "stock" ]]; then
        local keyword="upgraded from $cfw to version $lfw"
      else
        local keyword="converted to HomeKit firmware"
      fi
      echo "Would have been $keyword..."
    elif [ -z $dlurl ]; then
      echo "$model is not supported yet..."
    else
      echo "$device dose not need updating..."
    fi

    if [ "$flash" = "yes" ]; then
      write_flash $device $lfw $dlurl $cfw_type
    elif [ "$flash" = "no" ]; then
      echo "Skipping Flash..."
    fi
    echo " "
  else
    if [ -z $dlurl ]; then
      lfw="Not Supported"
    fi
    echo "Host: $device"
    echo "Model: $model"
    if [ $cfw_type == "homekit" ]; then
      echo "Current: HomeKit $cfw"
    else
      echo "Current: Official $cfw"
    fi
    if [[ $mode == "homekit" ]]; then
      echo "Latest: HomeKit $lfw"
    else
      echo "Latest: Official $lfw"
    fi
    echo " "
  fi
}

function device_scan {
  local device=$1
  local action=$2
  local do_all=$3
  local dry_run=$4
  local mode=$5

  if [ $do_all == false ]; then
    probe_info $device $action $dry_run $mode
  else
    echo -e '\033[1mScanning for Shelly devices...\033[0m'
    for device in $(timeout 2 dns-sd -B _http . | awk '/shelly/ {print $7}'); do
      probe_info $device.local $action $dry_run $mode
    done
  fi
}

function help {
  echo "Shelly HomeKit flashing script utility"
  echo "Usage: $0 -{m|l|a|n|y|h} $1{hostname(s) optional}"
  echo " -m {homekit|revert|keep}   Script mode."
  echo " -l                         List info of shelly device."
  echo " -a                         Run against all the devices on the network."
  echo " -n                         Do a dummy run through."
  echo " -y                         Do not ask any confirmation to perform the flash."
  echo " -h                         This help text."
}

check=null
action=flash
do_all=false
dry_run=false
silent_run=false
mode="homekit"

while getopts ":alnyhm:" opt; do
  case ${opt} in
    m )
      if [ $OPTARG == "homekit" ]; then
        mode="homekit"
      elif [ $OPTARG == "revert" ]; then
        mode="stock"
      elif [ $OPTARG == "keep" ]; then
        mode="keep"
      else
        echo "Invalid option"
        help
        exit 1
      fi
      ;;
    a )
      do_all=true
      ;;
    l )
      action=list
      ;;
    n )
      dry_run=true
      ;;
    y )
      silent_run=true
      ;;
    h )
      help
      exit 0
      ;;
    \? )
      echo "Invalid option"
      help
      exit 1
      ;;
  esac
done
shift $((OPTIND -1))

if [ $# == 0 -a $do_all == false ]; then
  help
  exit 1
elif [[ ! -z $@ ]] && [ $do_all == true ]; then
  help
  exit 1
fi

stock_release_info=$(curl -qsS -m 5 https://api.shelly.cloud/files/firmware)||check="error"
homekit_release_info=$(curl -qsS -m 5 https://api.github.com/repos/mongoose-os-apps/shelly-homekit/releases/latest)||check="error"
if [ $check == "error" ]; then
  echo "Failed to lookup version information"
  exit 1
fi

if [[ ! -z $@ ]];then
  for device in $@; do
    device_scan $device $action $do_all $dry_run $mode
  done
fi
if [ $do_all == true ];then
  device_scan null $action $do_all $dry_run $mode
fi
