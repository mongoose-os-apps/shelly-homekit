#!/usr/bin/env python3
#  Copyright (c) 2020 Andrew Blackburn & Deomid "rojer" Ryabkov
#  All rights reserved
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#
#  This script will probe for any shelly device on the network and it will
#  attempt to update them to the latest firmware version available.
#  This script will not flash any firmware to a device that is not already on a
#  version of this firmware, if you are looking to flash your device from stock
#  or any other firmware please follow instructions here:
#  https://github.com/mongoose-os-apps/shelly-homekit/wiki
#
#  Shelly HomeKit flashing script utility
#  Usage: ./flash_script.py -{m|l|a|e|n|y|V|h} {hostname(s) optional}
#   [-m | --mode] {homekit|revert|keep}  Script mode.
#   [-l | --list]                        List info of shelly device.
#   [-a | --all]                         Run against all the devices on the network.
#   [-e | --exclude] {hostname}          Exclude hosts from found devices.
#   [-n | --assume-no]                   Do a dummy run through.
#   [-y | --assume-yes]                  Do not ask any confirmation to perform the flash.
#   [-V | --version] {version}           Force a particular version.
#   [-v | --verbose] {0|1}               Enable verbose logging level.
#   [--variant] {alpha|beta}             Prelease variant name.
#   [-h | --help]                        This help text.
#
#  usage: python3 flash_shelly.py -la
#  usage: python3 flash_shelly.py shelly1-034FFF

import functools
import getopt
import json
import logging
import platform
import re
import subprocess
import sys
import time
import urllib

logging.TRACE = 5
logging.addLevelName(logging.TRACE, 'TRACE')
logging.Logger.trace = functools.partialmethod(logging.Logger.log, logging.TRACE)
logging.trace = functools.partial(logging.log, logging.TRACE)
logging.basicConfig(format='%(message)s')
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

arch = platform.system()
# Windows does not support acsii colours
if not arch.startswith('Win'):
  WHITE = '\033[1m'
  RED = '\033[1;91m'
  GREEN = '\033[1;92m'
  YELLOW = '\033[1;93m'
  BLUE = '\033[1;94m'
  PURPLE = '\033[1;95m'
  NC = '\033[0m'
else:
  WHITE = ''
  RED = ''
  GREEN = ''
  YELLOW = ''
  BLUE = ''
  PURPLE = ''
  NC = ''

try:
  import zeroconf
except ImportError:
  logger.info("Installing zeroconf...")
  pipe = subprocess.check_output(['pip3', 'install', 'zeroconf'])
  import zeroconf

try:
  import requests
except ImportError:
  logger.info("Installing requests...")
  pipe = subprocess.check_output(['pip3', 'install', 'requests'])
  import requests

class MyListener:
  def __init__(self):
    self.device_list = []
    self.p_list = []

  def add_service(self, zeroconf, type, name):
      self.device_list.append(name.replace('._http._tcp.local.', ''))
      # info = zeroconf.get_service_info(type, name, 2000)
      # logger.trace(f"INFO: {info}")
      # properties = { y.decode('ascii'): info.properties.get(y).decode('ascii') for y in info.properties.keys() }
      # self.p_list.append(properties)
      # logger.trace("properties: {properties}")
      # json_object = json.dumps(properties, indent = 2)

def shelly_model(type, mode):
  if mode != 'stock':
    options = {'SHSW-1' : 'Shelly1',
               'switch1' : 'Shelly1',
               'SHSW-PM' : 'Shelly1PM',
               'switch1pm' : 'Shelly1PM',
               'SHSW-21' : 'Shelly2',
               'switch2' : 'Shelly2',
               'SHSW-25' : 'Shelly25',
               'switch25' : 'Shelly25',
               'SHPLG-S' : 'ShellyPlugS',
               'shelly-plug-s' : 'ShellyPlugS',
               'SHDM-1' : 'ShellyDimmer',
               'dimmer1' : 'ShellyDimmer',
               'SHRGBW2' : 'ShellyRGBW2',
               'rgbw2' : 'ShellyRGBW2',
    }
  else:
    options = {'switch1' : 'SHSW-1',
               'switch1pm' : 'SHSW-PM',
               'switch2' : 'SHSW-21',
               'switch25' : 'SHSW-25',
               'shelly-plug-s' : 'SHPLG-S',
               'dimmer1' : 'SHDM-1',
               'rgbw2' : 'SHRGBW2',
    }
  return(options[type])


def parseVersion(vs):
  pp = vs.split('-');
  v = pp[0].split('.');
  variant = ""
  varSeq = 0
  if len(pp) > 1:
    i = 0
    for x in pp[1]:
      c = pp[1][i]
      if not c.isnumeric():
        variant += c
      else:
        break
      i += 1
    varSeq = int(pp[1][i]) or 0
  major, minor, patch = [int(e) for e in v]
  return (major, minor, patch, variant, varSeq)

def isNewer(v1, v2):
  vi1 = parseVersion(v1)
  vi2 = parseVersion(v2)
  if (vi1[0] != vi2[0]):
    return (vi1[0] > vi2[0])
  elif (vi1[1] != vi2[1]):
    return (vi1[1] > vi2[1])
  elif (vi1[2] != vi2[2]):
    return (vi1[2] > vi2[2])
  elif (vi1[3] != vi2[3]):
    return True
  elif (vi1[4] != vi2[4]):
    return (vi1[4] > vi2[4])
  else:
    return False


def write_flash(device, lfw, dlurl, cfw_type, mode):
  logger.debug(f"\n{WHITE}write_flash{NC}")
  flashed = False
  host = device.replace('.local', '')
  if cfw_type == 'homekit':
    logger.info("Downloading Firmware...")
    logger.debug(f"DURL: {dlurl}")
    myfile = requests.get(dlurl)
    logger.info("Now Flashing...")
    files = {'file': ('shelly-flash.zip', myfile.content)}
    response = requests.post(f'http://{device}/update' , files=files)
    logger.debug(response.text)
  else:
    logger.info("Now Flashing...")
    dlurl = dlurl.replace('https', 'http')
    logger.debug(f"curl -qsS http://{device}/ota?url={dlurl}")
    response = requests.get(f'http://{device}/ota?url={dlurl}')
    logger.debug(response.text)
  time.sleep(2)
  n = 1
  waittextshown = False
  info = None
  while n < 40:
    if waittextshown == False:
      logger.info("waiting for %s to reboot" % host)
      waittextshown = True
    if mode == 'homekit':
      checkurl = f'http://{device}/rpc/Shelly.GetInfo'
    else:
      checkurl = f'http://{device}/Shelly.GetInfo'
    try:
      with urllib.request.urlopen(checkurl) as fp:
        info = json.load(fp)
    except (urllib.error.HTTPError, urllib.error.URLError) as err:
      logger.debug(f"Error: {err}")
      n += 1
    if info:
      n=41
    time.sleep(1)
  if mode == 'homekit':
    onlinecheck = info['version']
  else:
    onlinecheck = info['fw'].split('/v')[1].split('@')[0]
  if onlinecheck == lfw:
    logger.info(f"{GREEN}Successfully flashed {host} to {lfw}{NC}")
    if mode == 'stock' and info['type'] == 'SHRGBW2':
      logger.info("\nTo finalise flash process you will need to switch 'Modes' in the device WebUI,")
      logger.info(f"{WHITE}WARNING!!{NC} If you are using this device in conjunction with Homebridge it will")
      logger.info("result in ALL scenes / automations to be removed within HomeKit.")
      logger.info("Goto http://$device in your web browser")
      logger.info("Goto settings section")
      logger.info("Goto 'Device Type' and switch modes")
      logger.info("Once mode has been changed, you can switch it back to your preferred mode.")
  else:
    logger.info(f"{RED}Failed to flash {host} to {lfw}{NC}")
    logger.info("Current: %s" % onlinecheck)

def probe_info(device, action, dry_run, silent_run, mode, exclude, exclude_device, forced_version, prerelease, variant, ffw, stock_release_info, homekit_release_info):
  flash = False
  info = None           # firmware versions info
  model = None          # device model
  lfw = None            # latest firmware available
  cfw = None            # current firmware on device
  cfw_type = 'homekit'  # current firmware type
  host = device
  host = host.replace('.local', '')
  if exclude_device:
    for i, item in enumerate(exclude_device):
      exclude_device[i] = exclude_device[i].replace('.local', '')
  logger.debug(f"\n{WHITE}probe_info{NC}")
  logger.debug(f"device: {device}")
  logger.debug(f"host: {host}")
  logger.debug(f"action: {action}")
  logger.debug(f"dry_run: {dry_run}")
  logger.debug(f"silent_run: {silent_run}")
  logger.debug(f"mode: {mode}")
  logger.debug(f"exclude: {exclude}")
  logger.debug(f"exclude_device: {exclude_device}")
  logger.debug(f"forced_version: {forced_version}")
  logger.debug(f"ffw: {ffw}")
  logger.debug(f"prerelease: {prerelease}")
  logger.debug(f"variant: {variant}")
  try:
    with urllib.request.urlopen(f'http://{device}/rpc/Shelly.GetInfo') as fp:
      info = json.load(fp)
  except (urllib.error.HTTPError) as err:
    try:
      cfw_type = 'stock'
      with urllib.request.urlopen(f'http://{device}/Shelly.GetInfo') as fp:
        info = json.load(fp)
    except (urllib.error.HTTPError, urllib.error.URLError) as err:
      return 1
  except (urllib.error.URLError) as err:
    logger.warning(f"Could not resolve host: {host}")
    return 1
  if mode == 'keep':
    mode = cfw_type
  if cfw_type == 'homekit':
    type = info['app']
    cfw = info['version']
    if mode == 'homekit':
      model = info['model'] if 'model' in info else shelly_model(type, mode)
      for i in homekit_release_info:
        if prerelease:
          re_search = '-*'
        else:
          re_search = i[0]
        if re.search(re_search, cfw):
          lfw = i[1]['version']
          if forced_version == False:
            dlurl = i[1]['urls'][model]
          else:
            dlurl=f'http://rojer.me/files/shelly/{ffw}/shelly-homekit-{model}.zip'
          break
    else: # stock
      model = info['stock_model'] if 'stock_model' in info else shelly_model(type, mode)
      lfw = stock_release_info['data'][model]['version'].split('/v')[1].split('@')[0]
      dlurl = stock_release_info['data'][model]['url']
  else: # cfw stock
    cfw = info['fw'].split('/v')[1].split('@')[0]
    type = info['type']
    if mode == 'homekit':
      model = shelly_model(type, mode)
      for i in homekit_release_info:
        if prerelease:
          re_search = '-*'
        else:
          re_search = i[0]
        if re.search(re_search, cfw):
          lfw = i[1]['version']
          if forced_version == False:
            try:
              dlurl = i[1]['urls'][model]
            except:
              dlurl = None
          else:
            dlurl = f'http://rojer.me/files/shelly/{ffw}/shelly-homekit-{model}.zip'
          break
    else: # stock
      model = type
      lfw = stock_release_info['data'][model]['version'].split('/v')[1].split('@')[0]
      dlurl = stock_release_info['data'][model]['url']
  if not dlurl:
    lfw_label = f"{RED}Not available{NC}"
    lfw = '0.0.0'
  else:
    lfw_label = lfw
  logger.info(f"{WHITE}Host: {NC}{host}")
  logger.info(f"{WHITE}Model: {NC}{model}")
  if cfw_type == 'homekit':
    logger.info(f"{WHITE}Current: {NC}HomeKit {cfw}")
  else:
    logger.info(f"{WHITE}Current: {NC}Official {cfw}")
  col = YELLOW if isNewer(lfw, cfw) else WHITE
  if mode == 'homekit':
    logger.info(f"{WHITE}Latest: {NC}HomeKit {col}{lfw_label}{NC}")
  else:
    logger.info(f"{WHITE}Latest: {NC}Official {col}{lfw_label}{NC}")
  if action != 'list':
    if forced_version == True and dlurl:
      lfw = ffw
      perform_flash = True
    elif exclude == True and host in exclude_device:
      perform_flash = False
    elif (cfw_type == 'stock' and mode == 'homekit' and dlurl) or (cfw_type == 'homekit' and mode == 'stock' and dlurl) \
         or ((isNewer(lfw, cfw)) and ((cfw_type == 'homekit' and mode == 'homekit') \
         or (cfw_type == 'stock' and mode == 'stock') or mode == 'keep')):
      perform_flash = True
    else:
      perform_flash = False
    if perform_flash == True and dry_run == False and silent_run == False:
      if input(f"Do you wish to flash {host} to firmware version {lfw} (y/n) ? ") == 'y':
        flash = True
      else:
        flash = False
    elif perform_flash == True and dry_run == False and silent_run == True:
      flash = True
    elif perform_flash == True and dry_run == True:
      if cfw_type == 'homekit' and mode == 'stock':
        keyword = "converted to Official firmware"
      elif cfw_type == 'stock' and mode == 'homekit':
        keyword = "converted to HomeKit firmware"
      elif isNewer(lfw, cfw):
        keyword = f"upgraded from {cfw} to version {lfw}"
      elif forced_version:
        keyword = f"reflashed version {ffw}"
      logger.info(f"Would have been {keyword}...")
    elif not dlurl:
      if ffw:
        keyword = f"Version {ffw} is not available yet..."
      else:
        keyword = "Is not supported yet..."
      logger.info(f"{keyword}\n")
      return 0
    elif exclude == True and host in exclude_device:
      logger.info("Skipping as device has been excluded...")
    else:
      logger.info("Does not need updating...\n")
      return 0
    if flash == True:
      write_flash(device, lfw, dlurl, cfw_type, mode)
    elif dry_run == False and exclude == False:
      logger.info("Skipping Flash...")
  logger.info(" ")


def device_scan(args, action, do_all, dry_run, silent_run, mode, exclude, forced_version, prerelease, variant, ffw, stock_release_info, homekit_release_info):
  device = args
  exclude_device = None
  logger.debug(f"\n{WHITE}device_scan{NC}")
  logger.debug(f"device: {device}")
  logger.debug(f"action: {action}")
  logger.debug(f"do_all: {do_all}")
  logger.debug(f"dry_run: {dry_run}")
  logger.debug(f"silent_run: {silent_run}")
  logger.debug(f"mode: {mode}")
  logger.debug(f"exclude: {exclude}")
  logger.debug(f"forced_version: {forced_version}")
  logger.debug(f"prerelease: {prerelease}")
  logger.debug(f"variant: {variant}")
  logger.debug(f"ffw: {ffw}")
  if do_all == False:
    logger.info(f"{WHITE}Probing Shelly device for info...\n{NC}")
    if  not '.local' in device:
      device = device + '.local'
    probe_info(device, action, dry_run, silent_run, mode, exclude, exclude_device, forced_version, prerelease, variant, ffw, stock_release_info, homekit_release_info)
  else:
    exclude_device = device
    logger.info(f"{WHITE}Scanning for Shelly devices...\n{NC}")
    zc = zeroconf.Zeroconf()
    listener = MyListener()
    browser = zeroconf.ServiceBrowser(zc, '_http._tcp.local.', listener)
    time.sleep(5)
    zc.close()
    logger.debug(f"device_test: {listener.device_list}")
    # logger.debug(f"\nproperties: {listener.p_list}")
    listener.device_list.sort()
    for device in listener.device_list:
      probe_info(device + '.local', action, dry_run, silent_run, mode, exclude, exclude_device, forced_version, prerelease, variant, ffw, stock_release_info, homekit_release_info)


def usage():
  logger.info("Shelly HomeKit flashing script utility")
  logger.info("Usage: ./flash_script.py -{m|l|a|e|n|y|V|h} {hostname(s) optional}")
  logger.info(" [-m | --mode] {homekit|revert|keep}  Script mode.")
  logger.info(" [-l | --list]                        List info of shelly device.")
  logger.info(" [-a | --all]                         Run against all the devices on the network.")
  logger.info(" [-e | --exclude] {hostname}          Exclude hosts from found devices.")
  logger.info(" [-n | --assume-no]                   Do a dummy run through.")
  logger.info(" [-y | --assume-yes]                  Do not ask any confirmation to perform the flash.")
  logger.info(" [-V | --version] {version}           Force a particular version.")
  logger.info(" [-v | --verbose] {0|1}               Enable verbose logging level.")
  logger.info(" [--variant] {alpha|beta}             Prelease variant name.")
  logger.info(" [-h | --help]                        This help text.")

def app(argv):
  # Parse and interpret options.
  try:
    (opts, args) = getopt.getopt(argv, ':aelnyhm:V:v:', ['all', 'exclude=', 'list', 'assume-no', 'assume-yes', 'help', 'debug', 'mode', 'variant=', 'version=', 'verbose='])
  except getopt.GetoptError as err:
    logger.error(err)
    usage()
    sys.exit(2)
  action = 'flash'
  do_all = False
  dry_run = False
  silent_run = False
  forced_version = False
  exclude = False
  mode = 'homekit'
  ffw = None
  prerelease = False
  variant = None
  variant_check = False
  for (opt, value) in opts:
    if opt in ('-m', 'mode'):
      if value == 'homekit':
        mode = 'homekit'
      elif value == 'revert':
        mode='stock'
      elif value == 'keep':
        mode='keep'
      else:
        logger.info(f"Invalid option {value}")
        usage()
        sys.exit(2)
    elif opt in ('-a', '--all'):
      do_all = True
    elif opt in ('-e', '--exclude'):
      if not value:
        logger.info(f"No hostname supplied.")
        usage()
        sys.exit(2)
      exclude = True
    elif opt in ('-l', '--list'):
      action = 'list'
    elif opt in ('-n', '--assume-no'):
      dry_run = True
    elif opt in ('-y', '--assume-yes'):
      silent_run = True
    elif opt in ('-V', '--version'):
      if not value:
        logger.info(f"No version supplied.")
        usage()
        sys.exit(2)
      forced_version = True
      ffw = value
    elif opt in ('-v', '--verbose'):
      if not value or (value and value == '0'):
        logger.setLevel(logging.DEBUG)
      elif value and value == '1':
        logger.setLevel(logging.TRACE)
      else:
        logger.info(f"Invalid level {value}")
        usage()
        sys.exit(2)
    elif opt == '--variant':
      if not value:
        logger.info(f"No variant supplied.")
        usage()
        sys.exit(2)
      prerelease = True
      variant = value
    elif opt in ('-h', '--help'):
      usage()
      sys.exit(0)
  logger.debug(f"{WHITE}app{NC}")
  logger.debug(f"{PURPLE}OS: {arch}{NC}")
  logger.debug(f"ARG: {argv}")
  logger.debug(f"opts: {opts}")
  logger.debug(f"args: {args}")
  logger.debug(f"action: {action}")
  logger.debug(f"do_all: {do_all}")
  logger.debug(f"dry_run: {dry_run}")
  logger.debug(f"silent_run: {silent_run}")
  logger.debug(f"forced_version: {forced_version}")
  logger.debug(f"exclude: {exclude}")
  logger.debug(f"mode: {mode}")
  logger.debug(f"variant: {variant}")
  if action == 'list' and (not args and do_all == False):
    logger.info("List option requires a hostname or {-a|--all}")
    usage()
    sys.exit(2)
  if ((not opts and not args) and do_all == False) or (not args and do_all == False) or \
     (args and do_all == True and exclude == False) or (not args and do_all == True and exclude == True):
    usage()
    sys.exit(1)
  try:
    with urllib.request.urlopen("https://api.shelly.cloud/files/firmware") as fp:
      stock_release_info = json.load(fp)
  except:
    logger.warning("Failed to lookup version information")
    sys.exit(1)
  try:
    with urllib.request.urlopen("https://rojer.me/files/shelly/update.json") as fp:
      homekit_release_info = json.load(fp)
  except:
    logger.warning("Failed to lookup version information")
    sys.exit(1)
  logger.trace(f"\n{WHITE}stock_release_info:{NC}{stock_release_info}")
  logger.trace(f"\n{WHITE}homekit_release_info:{NC}{homekit_release_info}")
  if prerelease:
    for i in homekit_release_info:
      logger.trace(f"i: {i[1]}")
      logger.trace(f"version: {i[1]['version']}")
      if variant in i[1]['version']:
        variant_check = True
        break
      else:
        variant_check = False
    if not variant_check:
      logger.info(f"{RED}Firmware variant {variant} not found.{NC}")
      sys.exit(3)
  if args and exclude == False:
    for device in args:
      device_scan(device, action, do_all, dry_run, silent_run, mode, exclude, forced_version, prerelease, variant, ffw, stock_release_info, homekit_release_info)
  if do_all == True and exclude == False:
    device_scan('', action, do_all, dry_run, silent_run, mode, exclude, forced_version, prerelease, variant, ffw, stock_release_info, homekit_release_info)
  if do_all == True and exclude == True:
    device_scan(args, action, do_all, dry_run, silent_run, mode, exclude, forced_version, prerelease, variant, ffw, stock_release_info, homekit_release_info)


if __name__ == '__main__':
  app(sys.argv[1:])
