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
#  attemp to update them to the latest firmware version available.
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
#   -V                         Force a particular version.
#   -h                         This help text.
#
#  usage: ./flash_shelly.sh -la
#  usage: ./flash_shelly.sh shelly1-034FFF

WHITE='\033[1m' # Bright White
RED='\033[1;91m' # Bright Purple
GREEN='\033[1;92m' # Bright Green
YELLOW='\033[1;93m' # Bright Yellow
BLUE='\033[1;94m' # Bright Blue
PURPLE='\033[1;95m' # Bright Purple
NC='\033[0m' # Normal Color

arch=$(uname -s)
function check_installer {
  if [[ $arch == "Darwin" ]]; then
    check_brew
    installer="brew install "
  elif [ "$(which apt-get 2>/dev/null)" != "" ]; then
    installer="sudo apt-get install "
  else
    installer="sudo yum install "
  fi
  return 0
}

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

  echo -e "${WHITE}Installing brew...${NC}"
  echo -e "${WHITE}Please follow instructions...${NC}"
  echo -e "${WHITE}You may be asked for your password...${NC}"
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
}

if [ "$(which git 2>/dev/null)" == "" ]; then
  check_installer
  echo -e "${WHITE}Installing git...${NC}"
  echo -e "${WHITE}You may be asked for your password...${NC}"
  echo $($installer git)
fi

if [[ $arch == "Darwin" ]]; then
  if [ "$(which timeout 2>/dev/null)" == "" ]; then
    check_installer
    echo -e "${WHITE}Installing coreutils...${NC}"
    echo -e "${WHITE}You may be asked for your password...${NC}"
    echo $($installer coreutils)
  fi
else
  if [ "$(which avahi-browse 2>/dev/null)" == "" ]; then
    check_installer
    echo -e "${WHITE}Installing avahi-utils...${NC}"
    echo -e "${WHITE}You may be asked for your password...${NC}"
    echo $($installer avahi-utils)
  fi
fi

if [ "$(which jq 2>/dev/null)" == "" ]; then
  check_installer
  echo -e "${WHITE}Installing jq...${NC}"
  echo -e "${WHITE}You may be asked for your password...${NC}"
  echo $($installer jq)
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
  local mode=$5
  local host=${device//.local/}

  if [ $cfw_type == "homekit" ]; then
    echo "Downloading Firmware..."
    curl  -qsS -L -o shelly-flash.zip $durl
    flashcmd="curl -qsS -F file=@shelly-flash.zip http://$device/update"
  else
    flashcmd="curl -qsS http://$device/ota?url=$dlurl"
  fi
  if [ -f shelly-flash.zip ] || [ $cfw_type == "stock" ];then
    echo "Now Flashing..."
    echo $($flashcmd)
  fi

  sleep 10
  n=1
  waittextshown=false
  while [ $n -le 30 ]; do
    if [[ $mode == "homekit" ]]; then
      onlinecheck=$(curl -qsf -m 5 http://$device/rpc/Shelly.GetInfo)||onlinecheck="error"
    else
      onlinecheck=$(curl -qsf -m 5 http://$device/Shelly.GetInfo)||onlinecheck="error"
    fi
    if [[ $onlinecheck == "error" ]]; then
      if [[ $waittextshown == false ]]; then
        echo "waiting for $host to reboot"
        waittextshown=true
      fi
      sleep 2
      n=$(( $n + 1 ))
    else
      if [[ $mode == "homekit" ]]; then
        onlinecheck=$(echo $onlinecheck | jq -r .version)
      else
        onlinecheck=$(echo $onlinecheck | jq -r .fw | awk '{split($0,a,"/v"); print a[2]}' | awk '{split($0,a,"@"); print a[1]}')
      fi
      n=31
    fi
  done

  if [[ $onlinecheck == $lfw ]];then
    echo -e "${GREEN}Successfully flashed $host to $lfw${NC}"
    if [[ $mode == "homekit" ]]; then
      rm -f shelly-flash.zip
    else
      if [ $(echo "$info" | jq -r .type) == "SHRGBW2" ]; then
        echo -e "\nTo finalise flash process you will need to switch 'Modes' in the device WebUI,"
        echo -e "${WHITE}WARNING!!${NC} If you are using this device in conjunction with Homebridge it will"
        echo "result in ALL scenes / automations to be removed within HomeKit."
        echo "Goto http://$device in your web browser"
        echo "Goto settings section"
        echo "Goto 'Device Type' and switch modes"
        echo "Once mode has been changed, you can switch it back to your preferred mode."
      fi
    fi
  else
    echo -e "${RED}Failed to flash $host to $lfw${NC}"
    echo "Current: $onlinecheck"
  fi
}

function probe_info {
  local flash=false
  local info=null      # firmware versions info
  local model=null     # device model
  local lfw=null       # latest firmware available
  local cfw=null       # current firmware on device
  local cfw_type=null  # current firmware type
  local device=$1
  local action=$2
  local dry_run=$3
  local mode=$4
  local host=${device//.local/}

  info=$(curl -qs -m 5 http://$device/rpc/Shelly.GetInfo)||info="error"
  if [[ $info == "error" ]]; then
    echo "Could not resolve host: $host"
    if [[ -z $device ]]; then
      read -p "Press enter to continue"
      return 0
    else
      return 1
    fi
  fi
  cfw_type="homekit"
  if [[ $info == "Not Found" ]]; then
    info=$(curl -qsS -m 5 http://$device/Shelly.GetInfo)
    cfw_type="stock"
    if [[ -z $device ]]; then
      return 0
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
      model=$(echo "$info" | jq -r .model | sed 's#\.##g' | sed 's#-##g')
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
      if [[ $forced_version == false ]]; then
        lfw=$(echo "$homekit_release_info" | jq -r .tag_name)
        dlurl=$(echo "$homekit_release_info" | jq -r '.assets[] | select(.name=="shelly-homekit-'$model'.zip").browser_download_url')
      else
        lfw=$ffw
        dlurl="http://rojer.me/files/shelly/$lfw/shelly-homekit-$model.zip"
      fi
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
      if [[ $forced_version == false ]]; then
        lfw=$(echo "$homekit_release_info" | jq -r .tag_name)
      else
        lfw=$ffw
      fi
      dlurl="http://rojer.me/files/shelly/$lfw/shelly-homekit-$model.zip"
    else
      model=$type
      lfw=$(echo "$stock_release_info" | jq -r '.data."'$type'".version' | awk '{split($0,a,"/v"); print a[2]}' | awk '{split($0,a,"@"); print a[1]}')
      dlurl=$(echo "$stock_release_info" | jq -r '.data."'$type'".url')
    fi
  fi
  if [[ ! $(curl --head --silent --fail $dlurl 2> /dev/null) ]]; then
    unset dlurl
    lfw="${RED}Not available${NC}"
  fi

  echo -e "\n${WHITE}Host:${NC} $host"
  echo -e "${WHITE}Model:${NC} $model"
  if [ $cfw_type == "homekit" ]; then
    echo -e "${WHITE}Current:${NC} HomeKit $cfw"
  else
    echo -e "${WHITE}Current:${NC} Official $cfw"
  fi
  if [ $mode == "homekit" ]; then
    echo -e "${WHITE}Latest:${NC} HomeKit ${YELLOW}$lfw${NC}"
  else
    echo -e "${WHITE}Latest:${NC} Official ${YELLOW}$lfw${NC}"
  fi

  if [ $action != "list" ]; then
    # echo "DURL: $dlurl" # only needed when debugging
    cfw_V=$(convert_to_integer $cfw)
    lfw_V=$(convert_to_integer $lfw)

    if [ $cfw_type == "stock" ] && [ $mode == "homekit" ] && [[ ! -z $dlurl ]]; then
      perform_flash=true
    elif [ $cfw_type == "homekit" ] && [ $mode == "stock" ] && [[ ! -z $dlurl ]]; then
      perform_flash=true
    elif [ $(echo "$lfw_V $cfw_V -p" | dc) -ge 1 ]; then
      if [ $cfw_type == "homekit" ] && [ $mode == "homekit" ]; then
        perform_flash=true
      elif [ $cfw_type == "stock" ] && [ $mode == "stock" ]; then
        perform_flash=true
      elif [ $mode == "keep" ]; then
        perform_flash=true
      fi
    elif [ $forced_version == true ] && [ ! -z $dlurl ]; then
      perform_flash=true
    else
      perform_flash=false
    fi
    if [[ $perform_flash == true ]] && [[ $dry_run == false ]] && [[ $silent_run == false ]]; then
      while true; do
        read -p "Do you wish to flash $host to firmware version $lfw ? " yn
        case $yn in
          [Yy]* ) flash=true; break;;
          [Nn]* ) flash=false; break;;
          * ) echo "Please answer yes or no.";;
        esac
      done
    elif [[ $perform_flash == true ]] && [[ $dry_run == false ]] && [[ $silent_run == true ]]; then
      flash=true
    elif [[ $perform_flash == true ]] && [[ $dry_run == true ]]; then
      if [[ $mode == "stock" ]] && [ $cfw_type == "homekit" ]; then
        local keyword="converted to Official firmware"
      elif [ $cfw_type == "homekit" ] || [[ $mode == "stock" ]]; then
        local keyword="upgraded from $cfw to version $lfw"
      else
        local keyword="converted to HomeKit firmware"
      fi
      echo "Would have been $keyword..."
    elif [ -z $dlurl ]; then
      if [ ! -z $ffw ];then
        keyword="Version $ffw is not available yet..."
      else
        keyword="Is not supported yet..."
      fi
      echo "$keyword"
      return 0
    else
      echo "Does not need updating..."
      return 0
    fi

    if [[ $flash == true ]]; then
      write_flash $device $lfw $dlurl $cfw_type $mode
    elif [[ $dry_run == false ]]; then
      echo "Skipping Flash..."
    fi
  fi
}

function device_scan {
  local device_list=null
  local device=${1//,/}
  local action=$2
  local do_all=$3
  local dry_run=$4
  local mode=$5
  local exclude=$6

  if [ $do_all == false ]; then
    if [[ $device != *.local* ]]; then
      device=$device.local
    fi
    probe_info $device $action $dry_run $mode
  else
    echo -e "${WHITE}Scanning for Shelly devices...${NC}"
    if [[ $arch == "Darwin" ]]; then
      device_list=$(timeout 2 dns-sd -B _http . | awk '/shelly/ {print $7}' 2>/dev/null)
    else
      device_list=$(avahi-browse -p -d local -t _http._tcp 2>/dev/null)
      device_list=$(echo $device_list | sed 's#+#\n#g' | awk -F';' '{print $4}' | awk '/shelly/ {print $1}' 2>/dev/null)
    fi

    if [ $exclude == true ]; then
      dl=$device
      for d in $dl; do
        device_list=${device_list//$d/}
      done
    fi
    for device in $device_list; do
      probe_info $device.local $action $dry_run $mode || continue
    done
  fi
}

function help {
  echo -e "${WHITE}Shelly HomeKit flashing script utility${NC}"
  echo "Usage: $0 -{m|l|a|e|n|y|V|h} $1{hostname(s) optional}"
  echo " -m {homekit|revert|keep}   Script mode."
  echo " -l                         List info of shelly device."
  echo " -a                         Run against all the devices on the network."
  echo " -e                         Exclude hosts from found devices."
  echo " -n                         Do a dummy run through."
  echo " -y                         Do not ask any confirmation to perform the flash."
  echo " -V                         Force a particular version."
  echo " -h                         This help text."
}

check=null
action=flash
do_all=false
dry_run=false
silent_run=false
forced_version=false
exclude=false
mode="homekit"

while getopts ":aelnyhm:V:" opt; do
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
    e )
      exclude=true
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
    V )
      forced_version=true
      ffw=$OPTARG
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
elif [[ ! -z $@ ]] && [ $do_all == true ] && [ $exclude == false ]; then
  help
  exit 1
elif [[ -z $@ ]] && [ $do_all == true ] && [ $exclude == true ]; then
  help
  exit 1
fi


stock_release_info=$(curl -qsS -m 5 https://api.shelly.cloud/files/firmware)||check="error"
homekit_release_info=$(curl -qsS -m 5 https://api.github.com/repos/mongoose-os-apps/shelly-homekit/releases/latest)||check="error"
if [ $check == "error" ]; then
  echo -e "${RED}Failed to lookup version information${NC}"
  exit 1
fi

echo -e "${PURPLE}OS: $arch${NC}"
if [[ ! -z $@ ]] && [ $exclude == false ];then
  for device in $@; do
    device_scan $device $action $do_all $dry_run $mode $exclude || continue
  done
fi
if [ $do_all == true ] && [ $exclude == false ];then
  device_scan null $action $do_all $dry_run $mode $exclude
fi
if [ $do_all == true ] && [ $exclude == true ];then
  device=$@
  device_scan "$@" $action $do_all $dry_run $mode $exclude
fi
