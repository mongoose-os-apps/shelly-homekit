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
#  usage: flash_shelly.py [-h] [-m {homekit,keep,revert}] [-a] [-l] [-e [EXCLUDE [EXCLUDE ...]]] [-n] [-y] [-V VERSION]
#                         [--variant VARIANT] [-v {0,1}]
#                         [hosts [hosts ...]]
#  positional arguments:
#    hosts
#
#  optional arguments:
#    -h, --help            show this help message and exit
#    -m {homekit,keep,revert}, --mode {homekit,keep,revert}
#                          Script mode.
#    -a, --all             Run against all the devices on the network.
#    -l, --list            List info of shelly device.
#    -e [EXCLUDE [EXCLUDE ...]], --exclude [EXCLUDE [EXCLUDE ...]]
#                          Exclude hosts from found devices.
#    -n, --assume-no       Do a dummy run through.
#    -y, --assume-yes      Do not ask any confirmation to perform the flash.
#    -V VERSION, --version VERSION
#                          Force a particular version.
#    --variant VARIANT     Prerelease variant name.
#    -v {0,1}, --verbose {0,1}
#                          Enable verbose logging level.


import argparse
import functools
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

def get_info(host):
  if '.local' not in host:
    host = host + '.local'
  try:
    with urllib.request.urlopen(f'http://{host}/rpc/Shelly.GetInfo') as fp:
      info = json.load(fp)
    if 'device_id' not in info:
      info['device_id'] = info['id']
    if 'model' not in info:
      info['model'] = shelly_model(info['app'])
    info['fw_type'] = 'homekit'
    info['fw_type_str'] = 'HomeKit'
    return(info)
  except (urllib.error.HTTPError) as err:
    pass
  except (urllib.error.URLError) as err:
    logger.warning(f"{RED}Could not resolve host: {host}\n{NC}")
    return None
  except (json.decoder.JSONDecodeError) as err:
    logger.trace(f"{RED}{err}\n{NC}")
    logger.trace(f"data: {fp}")
    return None
  try:
    with urllib.request.urlopen(f'http://{host}/Shelly.GetInfo') as fp:
      info = json.load(fp)
    info['host'] = host
    info['device_id'] = host.replace('.local','')
    info['model'] = shelly_model(info['type'])
    info['stock_model'] = info['type']
    info['version'] = info['fw'].split('/v')[1].split('@')[0]
    info['fw_type'] = 'stock'
    info['fw_type_str'] = 'Official'
    return(info)
  except (urllib.error.HTTPError, urllib.error.URLError) as err:
    logger.trace(f"Error: {err}")
    return None


class MyListener:
  def __init__(self):
    self.device_list = []
    self.p_list = []

  def add_service(self, zeroconf, type, device):
    device = device.replace('._http._tcp.local.', '')
    logger.debug(f"Valid Hostname: {device} {is_valid_hostname(device)}")
    if is_valid_hostname(device):
      info = get_info(device)
      logger.trace(f"Info: {info}")
      if info is not None:
        self.device_list.append(info)
    # info = zeroconf.get_service_info(type, name, 2000)
    # logger.trace(f"INFO: {info}")
    # properties = { y.decode('ascii'): info.properties.get(y).decode('ascii') for y in info.properties.keys() }
    # self.p_list.append(properties)
    # logger.trace("properties: {properties}")
    # json_object = json.dumps(properties, indent = 2)

def shelly_model(type):
  options = {'SHSW-1' : 'Shelly1',
             'SHSW-PM' : 'Shelly1PM',
             'SHSW-21' : 'Shelly2',
             'SHSW-25' : 'Shelly25',
             'SHPLG-S' : 'ShellyPlugS',
             'SHDM-1' : 'ShellyDimmer1',
             'SHDM-2' : 'ShellyDimmer2',
             'SHRGBW2' : 'ShellyRGBW2',
             'switch1' : 'Shelly1',
             'switch1pm' : 'Shelly1PM',
             'switch2' : 'Shelly2',
             'switch25' : 'Shelly25',
             'shelly-plug-s' : 'ShellyPlugS',
             'dimmer1' : 'ShellyDimmer1',
             'dimmer2' : 'ShellyDimmer2',
             'rgbw2' : 'ShellyRGBW2',
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


def is_valid_hostname(hostname):
  if len(hostname) > 255:
      return False
  allowed = re.compile("(?!-)[A-Z\d-]{1,63}(?<!-)$", re.IGNORECASE)
  return all(allowed.match(x) for x in hostname.split("."))


def write_flash(host, lfw, dlurl, cfw_type, mode):
  logger.debug(f"\n{WHITE}write_flash{NC}")
  flashed = False
  friendly_host = host.replace('.local', '')
  if cfw_type == 'homekit':
    logger.info("Downloading Firmware...")
    logger.debug(f"DURL: {dlurl}")
    myfile = requests.get(dlurl)
    logger.info("Now Flashing...")
    files = {'file': ('shelly-flash.zip', myfile.content)}
    response = requests.post(f'http://{host}/update' , files=files)
    logger.debug(response.text)
  else:
    logger.info("Now Flashing...")
    dlurl = dlurl.replace('https', 'http')
    logger.debug(f"curl -qsS http://{host}/ota?url={dlurl}")
    response = requests.get(f'http://{host}/ota?url={dlurl}')
    logger.debug(response.text)
  time.sleep(2)
  n = 1
  waittextshown = False
  info = None
  while n < 40:
    if waittextshown == False:
      logger.info(f"waiting for {friendly_host} to reboot")
      waittextshown = True
    if mode == 'homekit':
      checkurl = f'http://{host}/rpc/Shelly.GetInfo'
    else:
      checkurl = f'http://{host}/Shelly.GetInfo'
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
    logger.info(f"{GREEN}Successfully flashed {friendly_host} to {lfw}{NC}")
    if mode == 'stock' and info['type'] == 'SHRGBW2':
      logger.info("\nTo finalise flash process you will need to switch 'Modes' in the device WebUI,")
      logger.info(f"{WHITE}WARNING!!{NC} If you are using this device in conjunction with Homebridge it will")
      logger.info("result in ALL scenes / automations to be removed within HomeKit.")
      logger.info(f"Goto http://{host} in your web browser")
      logger.info("Goto settings section")
      logger.info("Goto 'Device Type' and switch modes")
      logger.info("Once mode has been changed, you can switch it back to your preferred mode.")
  else:
    logger.info(f"{RED}Failed to flash {friendly_host} to {lfw}{NC}")
    logger.info("Current: %s" % onlinecheck)

def parse_info(device_info, action, dry_run, silent_run, mode, exclude, version, variant, stock_release_info, homekit_release_info):
  logger.debug(f"\n{WHITE}parse_info{NC}")
  logger.trace(f"device_info: {device_info}")
  perform_flash = False
  flash = False
  dlurl = None
  lfw = None                         # latest firmware available
  cfw = device_info['version']       # current firmware version
  cfw_type = device_info['fw_type']  # current firmware type
  cfw_type_str = device_info['fw_type_str']
  friendly_host = device_info['host'].replace('.local', '')
  host = device_info['host']
  device = device_info['device_id']
  model = device_info['model'] if 'model' in device_info else device_info['stock_model']
  logger.debug(f"host: {host}")
  logger.debug(f"device: {device}")
  logger.debug(f"model: {model}")
  logger.debug(f"stock_model: {device_info['stock_model']}")
  logger.debug(f"action: {action}")
  logger.debug(f"dry_run: {dry_run}")
  logger.debug(f"silent_run: {silent_run}")
  logger.debug(f"mode: {mode}")
  logger.debug(f"exclude: {exclude}")
  logger.debug(f"version: {version}")
  logger.debug(f"variant: {variant}")

  if mode == 'keep':
    mode = cfw_type
  logger.debug(f'flash_mode: {mode}\n')
  if mode == 'homekit':
    for i in homekit_release_info:
      if variant:
        re_search = '-*'
      else:
        re_search = i[0]
      if re.search(re_search, cfw):
        lfw = i[1]['version']
        if not version:
          dlurl = i[1]['urls'][model] if model in i[1]['urls'] else None
        else:
          dlurl = f'http://rojer.me/files/shelly/{version}/shelly-homekit-{model}.zip'
        break
  else: # stock
    lfw = stock_release_info['data'][device_info['stock_model']]['version'].split('/v')[1].split('@')[0]
    if not version:
      dlurl = stock_release_info['data'][device_info['stock_model']]['url']
    else:
      dlurl = f'http://archive.shelly-faq.de/version/v{version}/{model}.zip'
  if dlurl:
    durl_request = requests.get(dlurl)
  if not dlurl or durl_request.status_code != 200:
    lfw_label = f"{RED}Not available{NC}"
    lfw = '0.0.0'
    dlurl = None
  else:
    lfw_label = lfw
  logger.info(f"{WHITE}Host: {NC}{host}")
  logger.info(f"{WHITE}Model: {NC}{model}")
  logger.info(f"{WHITE}Current: {NC}{cfw_type_str} {cfw}")
  col = YELLOW if isNewer(lfw, cfw) else WHITE
  logger.info(f"{WHITE}Latest: {NC}{cfw_type_str} {col}{lfw_label}{NC}")
  logger.debug(f"{WHITE}D_URL: {NC}{dlurl}")
  if action != 'list':
    if exclude and friendly_host in exclude:
      logger.info("Skipping as device has been excluded...\n")
      return 0
    elif version and dlurl:
      perform_flash = True
      lfw = version
      keyword = f"reflashed version {version}"
    elif cfw_type != mode and dlurl:
      perform_flash = True
      if mode == 'stock':
        keyword = "converted to Official firmware"
      elif mode == 'homekit':
        keyword = "converted to HomeKit firmware"
    elif ((cfw_type == mode or mode == 'keep') and isNewer(lfw, cfw)):
      perform_flash = True
      keyword = f"upgraded from {cfw} to version {lfw}"
    elif not dlurl:
      if version:
        keyword = f"Version {version} is not available yet..."
      else:
        keyword = "Is not supported yet..."
      logger.info(f"{keyword}\n")
      return 0
    else:
      logger.info("Does not need flashing...\n")
      return 0

    logger.debug(f"perform_flash: {perform_flash}")
    if perform_flash == True and dry_run == False and silent_run == False:
      if input(f"Do you wish to flash {friendly_host} to firmware version {lfw} (y/n) ? ") == 'y':
        flash = True
      else:
        flash = False
        logger.info("Skipping Flash...")
    elif perform_flash == True and dry_run == False and silent_run == True:
      flash = True
    elif perform_flash == True and dry_run == True:
      logger.info(f"Would have been {keyword}...")
    if flash == True:
      write_flash(host, lfw, dlurl, cfw_type, mode)
  logger.info(" ")


def device_scan(hosts, action, do_all, dry_run, silent_run, mode, exclude, version, variant, stock_release_info, homekit_release_info):
  logger.debug(f"\n{WHITE}device_scan{NC}")
  logger.debug(f"devices: {hosts}")
  logger.debug(f"action: {action}")
  logger.debug(f"do_all: {do_all}")
  logger.debug(f"dry_run: {dry_run}")
  logger.debug(f"silent_run: {silent_run}")
  logger.debug(f"mode: {mode}")
  logger.debug(f"exclude: {exclude}")
  logger.debug(f"version: {version}")
  logger.debug(f"variant: {variant}")

  if not do_all:
    device_list = []
    logger.info(f"{WHITE}Probing Shelly device for info...\n{NC}")
    for host in hosts:
      info = get_info(host)
      if info is not None:
        device_list.append(info)
  else:
    logger.info(f"{WHITE}Scanning for Shelly devices...\n{NC}")
    zc = zeroconf.Zeroconf()
    listener = MyListener()
    browser = zeroconf.ServiceBrowser(zc, '_http._tcp.local.', listener)
    time.sleep(10)
    zc.close()
    device_list = listener.device_list
  sorted_device_list = sorted(device_list, key=lambda k: k['host'])
  logger.trace(f"device_test: {sorted_device_list}")
  # logger.debug(f"\nproperties: {listener.p_list}")
  for device in sorted_device_list:
    parse_info(device, action, dry_run, silent_run, mode, exclude, version, variant, stock_release_info, homekit_release_info)


if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Shelly HomeKit flashing script utility')
  parser.add_argument('-m', '--mode', action="store", choices=['homekit', 'keep', 'revert'], default="homekit", help="Script mode.")
  parser.add_argument('-a', '--all', action="store_true", dest='do_all', default=False, help="Run against all the devices on the network.")
  parser.add_argument('-l', '--list', action="store_true", default=False, help="List info of shelly device.")
  parser.add_argument('-e', '--exclude', action="store", dest="exclude", nargs='*', help="Exclude hosts from found devices.")
  parser.add_argument('-n', '--assume-no', action="store_true", dest='dry_run', default=False, help="Do a dummy run through.")
  parser.add_argument('-y', '--assume-yes', action="store_true", dest='silent_run', default=False, help="Do not ask any confirmation to perform the flash.")
  parser.add_argument('-V', '--version',type=str, action="store", dest="version", default=False, help="Force a particular version.")
  parser.add_argument('--variant', action="store", dest="variant", default=False, help="Prerelease variant name.")
  parser.add_argument('-v', '--verbose', action="store", dest="verbose", choices=['0', '1'], help="Enable verbose logging level.")
  parser.add_argument('hosts', type=str, nargs='*')
  args = parser.parse_args()
  action = 'list' if args.list else 'flash'
  args.mode = 'stock' if args.mode == 'revert' else args.mode

  if args.verbose and '0' in args.verbose:
    logger.setLevel(logging.DEBUG)
  elif args.verbose and '1' in args.verbose:
    logger.setLevel(logging.TRACE)
  logger.debug(f"{WHITE}app{NC}")
  logger.debug(f"{PURPLE}OS: {arch}{NC}")
  logger.debug(f"action: {action}")
  logger.debug(f"mode: {args.mode}")
  logger.debug(f"do_all: {args.do_all}")
  logger.debug(f"dry_run: {args.dry_run}")
  logger.debug(f"silent_run: {args.silent_run}")
  logger.debug(f"mode: {args.mode}")
  logger.debug(f"version: {args.version}")
  logger.debug(f"exclude: {args.exclude}")
  logger.debug(f"variant: {args.variant}")
  logger.debug(f"verbose: {args.verbose}")
  logger.debug(f"hosts: {args.hosts}")
  if not args.hosts and not args.do_all:
    logger.info("Requires a hostname or {-a|--all}.")
    parser.print_help()
    sys.exit(1)
  elif args.hosts and args.do_all:
    logger.info("Invalid option hostname or {-a|--all} not both.")
    parser.print_help()
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
  if args.variant:
    for i in homekit_release_info:
      logger.trace(f"i: {i[1]}")
      logger.trace(f"version: {i[1]['version']}")
      if args.variant in i[1]['version']:
        variant_check = True
        break
      else:
        variant_check = False
    if not variant_check:
      logger.info(f"{WHITE}Firmware variant {args.variant} not found.{NC}")
      sys.exit(3)
  device_scan(args.hosts, action, args.do_all, args.dry_run, args.silent_run, args.mode, args.exclude, args.version, args.variant, stock_release_info, homekit_release_info)
