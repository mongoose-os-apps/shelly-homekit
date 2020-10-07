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
#  Usage: -{m|l|a|n|y|h} {hostname(s) optional}
#   -m {homekit|revert|keep}   Script mode.
#   -l                         List info of shelly device.
#   -a                         Run against all the devices on the network.
#   -n                         Do a dummy run through.
#   -y                         Do not ask any confirmation to perform the flash.
#   -V                         Force a particular version.
#   -D                         Enable Debug Logging, you can increase logging with (-DD).")
#   -h                         This help text.
#
#  usage: python3 flash_shelly.py -la
#  usage: python3 flash_shelly.py shelly1-034FFF

import functools
import getopt
import importlib
import json
import logging
import os
import platform
import re
import subprocess
import sys
import time
import urllib

import importlib.util
from sys import argv

logging.TRACE = 5
logging.addLevelName(logging.TRACE, 'TRACE')
logging.Logger.trace = functools.partialmethod(logging.Logger.log, logging.TRACE)
logging.trace = functools.partial(logging.log, logging.TRACE)
logging.basicConfig(format='%(message)s')
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

arch = platform.system()

# Windows does not support acsii colours
WHITE = '\033[1m' if not arch.lower().startswith('win') else ""
RED = '\033[1;91m' if not arch.lower().startswith('win') else ""
GREEN = '\033[1;92m' if not arch.lower().startswith('win') else ""
YELLOW = '\033[1;93m' if not arch.lower().startswith('win') else ""
BLUE = '\033[1;94m' if not arch.lower().startswith('win') else ""
PURPLE = '\033[1;95m' if not arch.lower().startswith('win') else ""
NC = '\033[0m' if not arch.lower().startswith('win') else ""

if not importlib.util.find_spec("zeroconf"):
  logger.info('Installing zeroconf...')
  pipe = subprocess.check_output(['pip3', 'install', 'zeroconf'])
  import zeroconf
else:
  import zeroconf

if not importlib.util.find_spec("requests"):
  logger.info('Installing requests...')
  pipe = subprocess.check_output(['pip3', 'install', 'requests'])
  import requests
else:
  import requests

class MyListener:
  def __init__(self):
    self.device_list = []
    self.p_list = []

  def add_service(self, zeroconf, type, name):
      self.device_list.append(name.replace('._http._tcp.local.', ''))
      # info = zeroconf.get_service_info(type, name, 2000)
      # logger.trace('INFO: %s' % info)
      # properties = { y.decode('ascii'): info.properties.get(y).decode('ascii') for y in info.properties.keys() }
      # self.p_list.append(properties)
      # logger.trace('properties: %s' % properties)
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

def versionCompare(v1, v2):
  # This will split both the versions by '.'
  arr1 = v1.split(".")
  if 'beta' in v1:
    arr1b = arr1[2].split("-beta")
    arr1[2] = (arr1b[0])
    arr1.append(arr1b[1])
  else:
    arr1.append('0')

  arr2 = v2.split(".")
  if 'beta' in v2:
    arr2b = arr2[2].split("-beta")
    arr2[2] = (arr2b[0])
    arr2.append(arr2b[1])
  else:
    arr2.append('0')

  n = len(arr1)
  m = len(arr2)

  # converts to integer from string
  arr1 = [int(i) for i in arr1]
  arr2 = [int(i) for i in arr2]

  # compares which list is bigger and fills
  # smaller list with zero (for unequal delimeters)
  if n>m:
    for i in range(m, n):
      arr2.append(0)
  elif m>n:
    for i in range(n, m):
      arr1.append(0)

  # returns 1 if v1 is bigger and -1 if v2 is bigger and 0 if equal
  for i in range(len(arr1)):
    if arr1[i]>arr2[i]:
      return 1
    elif arr2[i]>arr1[i]:
      return -1
  return 0

def write_flash(device, lfw, dlurl, cfw_type, mode):
  logger.debug("\n" + WHITE + "write_flash" + NC)
  flashed = False
  host = device.replace('.local', '')
  if cfw_type == 'homekit':
    logger.info("Downloading Firmware...")
    logger.debug('DURL: %s' % dlurl)
    myfile = requests.get(dlurl)
    with open('shelly-flash.zip', 'wb') as f:
      f.write(myfile.content)
    if os.path.exists('shelly-flash.zip') or cfw_type == 'stock':
      logger.info("Now Flashing...")
      files = {
          'file': ('shelly-flash.zip', open('shelly-flash.zip', 'rb')),
      }
      response = requests.post('http://%s/update' % device , files=files)
      logger.debug(response.text)
  else:
    logger.info("Now Flashing...")
    dlurl = dlurl.replace('https', 'http')
    logger.debug("curl -qsS http://%s/ota?url=%s" % (device, dlurl))
    response = requests.get('http://%s/ota?url=%s' % (device, dlurl))
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
      checkurl = 'http://%s/rpc/Shelly.GetInfo' % device
    else:
      checkurl = 'http://%s/Shelly.GetInfo' % device
    try:
      with urllib.request.urlopen(checkurl) as fp:
        info = json.load(fp)
    except (urllib.error.HTTPError, urllib.error.URLError) as err:
      logger.debug("Error: %s" % err)
      n += 1
    if info:
      n=41
    time.sleep(1)

  if mode == 'homekit':
    onlinecheck = info['version']
  else:
    onlinecheck = info['fw'].split('/v')[1].split('@')[0]

  if onlinecheck == lfw:
    logger.info(GREEN + "Successfully flashed %s to %s\033[0m" % (host, lfw))
    if mode == 'homekit':
      os.remove('shelly-flash.zip')
    else:
      if info['type'] == "SHRGBW2":
        logger.info("\nTo finalise flash process you will need to switch 'Modes' in the device WebUI,")
        logger.info(WHITE + "WARNING!!" + NC + "If you are using this device in conjunction with Homebridge it will")
        logger.info("result in ALL scenes / automations to be os.removed within HomeKit.")
        logger.info("Goto http://$device in your web browser")
        logger.info("Goto settings section")
        logger.info("Goto 'Device Type' and switch modes")
        logger.info("Once mode has been changed, you can switch it back to your preferred mode.")
  else:
    logger.info(RED + "Failed to flash %s to %s\033[0m" % (host, lfw))
    logger.info("Current: %s" % onlinecheck)

def probe_info(device, action, dry_run, silent_run, mode, exclude, exclude_device, forced_version, ffw, stock_release_info, homekit_release_info):
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

  logger.debug("\n" + WHITE + "probe_info" + NC)
  logger.debug("device: %s" % device)
  logger.debug("host: %s" % host)
  logger.debug("action: %s" % action)
  logger.debug("dry_run: %s" % dry_run)
  logger.debug("silent_run: %s" % silent_run)
  logger.debug("mode: %s" % mode)
  logger.debug("exclude: %s" % exclude)
  logger.debug("exclude_device: %s" % exclude_device)
  logger.debug("forced_version: %s" % forced_version)
  logger.debug("ffw: %s" % ffw)

  try:
    with urllib.request.urlopen('http://%s/rpc/Shelly.GetInfo' % device) as fp:
      info = json.load(fp)
  except (urllib.error.HTTPError) as err:
    try:
      cfw_type = 'stock'
      with urllib.request.urlopen('http://%s/Shelly.GetInfo' % device) as fp:
        info = json.load(fp)
    except (urllib.error.HTTPError, urllib.error.URLError) as err:
      return 1
  except (urllib.error.URLError) as err:
    logger.warning("Could not resolve host: %s" % host)
    return 1

  if mode == 'keep':
    mode = cfw_type

  if cfw_type == 'homekit':
    type = info['app']
    cfw = info['version']
    if mode == 'homekit':
      model = info['model'] if 'model' in info else shelly_model(type, mode)
      for i in homekit_release_info:
        if re.search(i[0], cfw):
          lfw = i[1]['version']
          if forced_version == False:
            dlurl = i[1]['urls'][model]
          else:
            dlurl="http://rojer.me/files/shelly/%s/shelly-homekit-%s.zip" % (ffw, model)
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
        if re.search(i[0], cfw):
          lfw = i[1]['version']
          if forced_version == False:
            try:
              dlurl = i[1]['urls'][model]
            except:
              dlurl = None
          else:
            dlurl="http://rojer.me/files/shelly/%s/shelly-homekit-%s.zip" % (ffw, model)
          break
    else: # stock
      model = type
      lfw = stock_release_info['data'][model]['version'].split('/v')[1].split('@')[0]
      dlurl = stock_release_info['data'][model]['url']

  if not dlurl:
    lfw_label = RED + "Not available" + NC
    lfw = "0.0.0"
  else:
    lfw_label = lfw

  logger.info(WHITE + "Host: " + NC + "%s" % host)
  logger.info(WHITE + "Model: " + NC + "%s" % model)

  if cfw_type == 'homekit':
    logger.info(WHITE + "Current: " + NC + "HomeKit %s" % cfw)
  else:
    logger.info(WHITE + "Current: " + NC + "Official %s" % cfw)
  col = YELLOW if versionCompare(lfw, cfw) else WHITE
  if mode == 'homekit':
    logger.info(WHITE + "Latest: " + NC + "HomeKit " + col + "%s\033[0m" % lfw_label)
  else:
    logger.info(WHITE + "Latest: " + NC + "Official " + col + "%s\033[0m" % lfw_label)

  if action != 'list':
    if forced_version == True and dlurl:
      lfw = ffw
      perform_flash = True
    elif exclude == True and host in exclude_device:
      perform_flash = False
    elif (cfw_type == 'stock' and mode == 'homekit' and dlurl) or (cfw_type == 'homekit' and mode == 'stock' and dlurl) \
         or ((versionCompare(lfw, cfw) > 0) and ((cfw_type == 'homekit' and mode == 'homekit') \
         or (cfw_type == 'stock' and mode == 'stock') or mode == "keep")):
      perform_flash = True
    else:
      perform_flash = False

    if perform_flash == True and dry_run == False and silent_run == False:
      if input("Do you wish to flash %s to firmware version %s (y/n) ? " % (host, lfw)) == "y":
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
      elif versionCompare(lfw, cfw) > 0:
        keyword = "upgraded from %s to version %s" % (cfw, lfw)
      elif forced_version:
        keyword = "reflashed version %s" % ffw
      logger.info("Would have been %s..." % keyword)
    elif not dlurl:
      if ffw:
        keyword = "Version %s is not available yet..." % ffw
      else:
        keyword = "Is not supported yet..."
      logger.info(keyword + "\n")
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


def device_scan(args, action, do_all, dry_run, silent_run, mode, exclude, forced_version, ffw, stock_release_info, homekit_release_info):
  device = args
  exclude_device = None
  logger.debug("\n" + WHITE + "device_scan" + NC)
  logger.debug("device: %s" % device)
  logger.debug("action: %s" % action)
  logger.debug("do_all: %s" % do_all)
  logger.debug("dry_run: %s" % dry_run)
  logger.debug("silent_run: %s" % silent_run)
  logger.debug("mode: %s" % mode)
  logger.debug("exclude: %s" % exclude)
  logger.debug("forced_version: %s" % forced_version)
  logger.debug("ffw: %s" % ffw)

  if do_all == False:
    logger.info(WHITE + "Probing Shelly device for info...\n" + NC)
    if  not ".local" in device:
      device = device + ".local"
    probe_info(device, action, dry_run, silent_run, mode, exclude, exclude_device, forced_version, ffw, stock_release_info, homekit_release_info)
  else:
    exclude_device = device
    logger.info(WHITE + "Scanning for Shelly devices...\n" + NC)
    zc = zeroconf.Zeroconf()
    listener = MyListener()
    browser = zeroconf.ServiceBrowser(zc, "_http._tcp.local.", listener)
    time.sleep(2)
    zc.close()

    logger.debug('device_test: %s' % listener.device_list)
    # logger.debug('\nproperties: %s' % listener.p_list)

    listener.device_list.sort()
    for device in listener.device_list:
      probe_info(device + '.local', action, dry_run, silent_run, mode, exclude, exclude_device, forced_version, ffw, stock_release_info, homekit_release_info)


def usage():
  print ("Shelly HomeKit flashing script utility")
  print ("Usage: -{m|l|a|e|n|y|V|h} {hostname(s) optional}")
  print (" -m {homekit|revert|keep}   Script mode.")
  print (" -l            List info of shelly device.")
  print (" -a            Run against all the devices on the network.")
  print (" -e            Exclude hosts from found devices.")
  print (" -n            Do a dummy run through.")
  print (" -y            Do not ask any confirmation to perform the flash.")
  print (" -V            Force a particular version.")
  print (" -D            Enable Debug Logging, you can increase logging with (-DD).")
  print (" -h            This help text.")

def app(argv):
  # Parse and interpret options.

  try:
    (opts, args) = getopt.getopt(argv[1:], ":aelnyhDm:V:")
  except getopt.GetoptError as err:
    logger.error(err)
    usage()
    sys.exit(2)

  action = "flash"
  do_all = False
  dry_run = False
  silent_run = False
  forced_version = False
  exclude = False
  mode = 'homekit'
  ffw = None

  for (opt, value) in opts:
    if opt == "-m":
      if value == 'homekit':
        mode = 'homekit'
      elif value == "revert":
        mode='stock'
      elif value == "keep":
        mode="keep"
      else:
        logger.info("Invalid option")
        usage()
        sys.exit(2)
    elif opt == "-a":
      do_all = True
    elif opt == "-e":
      exclude = True
    elif opt == "-l":
      action = "list"
    elif opt == "-n":
      dry_run = True
    elif opt == "-y":
      silent_run = True
    elif opt == "-V":
      forced_version = True
      ffw = value
    elif opt == '-D':
      if logger.getEffectiveLevel() >= 20:
        logger.setLevel(logging.DEBUG)
      else:
        logger.setLevel(logging.TRACE)
    elif opt == '-h':
      usage()
      sys.exit(0)

  logger.debug(WHITE + "app" + NC)
  logger.debug(PURPLE + "OS: %s\033[0m"% arch)
  logger.debug("ARG: %s" % argv)
  logger.debug("opts: %s" % opts)
  logger.debug("args: %s" % args)
  logger.debug("action: %s" % action)
  logger.debug("do_all: %s" % do_all)
  logger.debug("dry_run: %s" % dry_run)
  logger.debug("silent_run: %s" % silent_run)
  logger.debug("forced_version: %s" % forced_version)
  logger.debug("exclude: %s" % exclude)
  logger.debug("mode: %s" % mode)

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

  logger.trace("\n" + WHITE + "stock_release_info:" + NC + " %s" % stock_release_info)
  logger.trace("\n" + WHITE + "homekit_release_info:" + NC + " %s" % homekit_release_info)

  if args and exclude == False:
    for device in args:
      device_scan(device, action, do_all, dry_run, silent_run, mode, exclude, forced_version, ffw, stock_release_info, homekit_release_info)

  if do_all == True and exclude == False:
    device_scan("", action, do_all, dry_run, silent_run, mode, exclude, forced_version, ffw, stock_release_info, homekit_release_info)

  if do_all == True and exclude == True:
    device_scan(args, action, do_all, dry_run, silent_run, mode, exclude, forced_version, ffw, stock_release_info, homekit_release_info)


if __name__ == '__main__':
  app(argv)
