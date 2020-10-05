import getopt, importlib, platform, urllib.request, json, logging
from sys import argv, exit
from os import path, remove, popen
from subprocess import Popen, PIPE, check_output
from time import sleep
from importlib import util

"""
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
#  attemp to update them to the latest firmware version available.
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
"""

logging.basicConfig(format='%(message)s')
logger = logging.getLogger(__name__)
logger.setLevel(logging.WARNING)

WHITE='\033[1m' # Bright White
RED='\033[1;91m' # Bright Purple
GREEN='\033[1;92m' # Bright Green
YELLOW='\033[1;93m' # Bright Yellow
BLUE='\033[1;94m' # Bright Blue
PURPLE='\033[1;95m' # Bright Purple
NC='\033[0m' # Normal Color

arch = platform.system()
print(PURPLE + "OS: %s\033[0m"% arch)

if not importlib.util.find_spec("zeroconf"):
  print('Installing zeroconf...')
  pipe = Popen('pip3 install zeroconf', shell=True, stdout=PIPE).stdout
  out = (pipe.read())
  from zeroconf import ServiceBrowser, Zeroconf
else:
  from zeroconf import ServiceBrowser, Zeroconf

if not importlib.util.find_spec("requests"):
  print('Installing requests...')
  pipe = Popen('pip3 install requests', shell=True, stdout=PIPE).stdout
  out = (pipe.read())
  import requests
else:
  import requests

if not importlib.util.find_spec("packaging"):
  print('Installing packaging...')
  pipe = Popen('pip3 install packaging', shell=True, stdout=PIPE).stdout
  out = (pipe.read())
  from packaging import version
else:
  from packaging import version

class MyListener:
  global device_list, p_list
  device_list=[]
  p_list=[]

  def add_service(self, zeroconf, type, name):
      device_list.append(name.replace('._http._tcp.local.',''))
      # info = zeroconf.get_service_info(type, name, 2000)
      # logger.debug('INFO: %s' % info)
      # properties = { y.decode('ascii'): info.properties.get(y).decode('ascii') for y in info.properties.keys() }
      # p_list.append(properties)
      # logger.debug('properties: %s' % properties)
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

def write_flash(device, lfw, dlurl, cfw_type, mode):
  flashed = False
  host = device.replace('.local','')
  if cfw_type == 'homekit':
    print("Downloading Firmware...")
    logger.info('DURL: %s' % dlurl)
    myfile = requests.get(dlurl)
    open('shelly-flash.zip', 'wb').write(myfile.content)
    if path.exists('shelly-flash.zip') or cfw_type == 'stock':
      print("Now Flashing...")
      files = {
          'file': ('shelly-flash.zip', open('shelly-flash.zip', 'rb')),
      }
      response = requests.post('http://%s/update' % device , files=files)
      logger.info(response.text)
  else:
    print("Now Flashing...")
    dlurl = dlurl.replace('https', 'http')
    logger.info("curl -qsS http://%s/ota?url=%s" % (device, dlurl))
    response = requests.get('http://%s/ota?url=%s' % (device, dlurl))
    logger.info(response.text)

  sleep(2)
  n = 1
  waittextshown = False
  info = None
  while n < 40:
    if waittextshown == False:
      print("waiting for %s to reboot" % host)
      waittextshown = True
    if mode == 'homekit':
      checkurl = 'http://%s/rpc/Shelly.GetInfo' % device
    else:
      checkurl = 'http://%s/Shelly.GetInfo' % device
    try:
      fp = urllib.request.urlopen(checkurl)
      info = json.load(fp)
      fp.close()
    except (urllib.error.HTTPError, urllib.error.URLError) as err:
      logger.info("Error: %s" % err)
      n += 1
    if info:
      n=41
    sleep(1)

  if mode == 'homekit':
    onlinecheck = info['version']
  else:
    onlinecheck = info['fw'].split('/v')[1].split('@')[0]

  if onlinecheck == lfw:
    print(GREEN + "Successfully flashed %s to %s\033[0m" % (host, lfw))
    if mode == 'homekit':
      remove('shelly-flash.zip')
    else:
      if info['type'] == "SHRGBW2":
        print("\nTo finalise flash process you will need to switch 'Modes' in the device WebUI,")
        print(WHITE + "WARNING!!" + NC + "If you are using this device in conjunction with Homebridge it will")
        print("result in ALL scenes / automations to be removed within HomeKit.")
        print("Goto http://$device in your web browser")
        print("Goto settings section")
        print("Goto 'Device Type' and switch modes")
        print("Once mode has been changed, you can switch it back to your preferred mode.")
  else:
    print(RED + "Failed to flash %s to %s\033[0m" % (host, lfw))
    print("Current: %s" % onlinecheck)

def probe_info(device, action, dry_run, silent_run, mode, forced_version, ffw):
  flash = False
  info = None           # firmware versions info
  model = None          # device model
  lfw = None            # latest firmware available
  cfw = None            # current firmware on device
  cfw_type = 'homekit'  # current firmware type
  host = device
  host = host.replace('.local','')

  logger.info("\n\ndevice: %s" % device)
  logger.info("action: %s" % action)
  logger.info("dry_run: %s" % dry_run)
  logger.info("silent_run: %s" % silent_run)
  logger.info("mode: %s" % mode)
  logger.info("forced_version: %s" % forced_version)
  logger.info("ffw: %s" % ffw)
  logger.info("host: %s" % host)

  try:
    fp = urllib.request.urlopen('http://%s/rpc/Shelly.GetInfo' % device)
    info = json.load(fp)
    fp.close()
  except (urllib.error.HTTPError) as err:
    try:
      cfw_type = 'stock'
      fp = urllib.request.urlopen('http://%s/Shelly.GetInfo' % device)
      info = json.load(fp)
      fp.close()
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
    if mode == 'stock':
      model = info['stock_model'] if 'stock_model' in info else shelly_model(type, mode)
      lfw = stock_release_info['data'][model]['version'].split('/v')[1].split('@')[0]
      dlurl = stock_release_info['data'][model]['url']
    else:
      model = info['model'] if 'model' in info else shelly_model(type, mode)
      for var in homekit_release_info:
        if 'beta' in cfw:
          break
        else:
          continue
      lfw = var[1]['version']
      if forced_version == False:
        dlurl = var[1]['urls'][model]
      else:
        dlurl="http://rojer.me/files/shelly/%s/shelly-homekit-%s.zip" % (ffw, model)
  else:
    cfw = info['fw'].split('/v')[1].split('@')[0]
    type = info['type']
    if mode == 'homekit':
      model = shelly_model(type, mode)
      for var in homekit_release_info:
        if 'beta' in cfw:
          break
        else:
          continue
      lfw = var[1]['version']
      if forced_version == False:
        try:
          dlurl = var[1]['urls'][model]
        except:
          dlurl = None
      else:
        dlurl="http://rojer.me/files/shelly/%s/shelly-homekit-%s.zip" % (ffw, model)
    else:
      model = type
      lfw = stock_release_info['data'][model]['version'].split('/v')[1].split('@')[0]
      dlurl = stock_release_info['data'][model]['url']

  if not dlurl:
    lfw = RED + "Not available" + NC

  print("\n" + WHITE + "Host: " + NC + "%s" % host)
  print(WHITE + "Model: " + NC + "%s" % model)

  if cfw_type == 'homekit':
    print(WHITE + "Current: " + NC + "HomeKit %s" % cfw)
  else:
    print(WHITE + "Current: " + NC + "Official %s" % cfw)
  if mode == 'homekit':
    print(WHITE + "Latest: " + NC + "HomeKit " + YELLOW + "%s\033[0m" % lfw)
  else:
    print(WHITE + "Latest: " + NC + "Official " + YELLOW + "%s\033[0m" % lfw)

  if action != 'list':
    # echo "DURL: $dlurl" # only needed when debugging
    if forced_version == True and dlurl:
      lfw = ffw
      perform_flash = True
    elif (cfw_type == 'stock' and mode == 'homekit' and dlurl) or (cfw_type == 'homekit' and mode == 'stock' and dlurl):
      perform_flash = True
    elif (version.parse(cfw) < version.parse(lfw)) and ((cfw_type == 'homekit' and mode == 'homekit') or (cfw_type == 'stock' and mode == 'stock') or mode == "keep"):
        perform_flash = True
    elif version.parse(cfw) == version.parse(lfw) and 'beta' in cfw:
      perform_flash = True
    else:
      perform_flash = False

    if perform_flash == True and dry_run == False and silent_run == False:
      if input("Do you wish to flash %s to firmware version %s (y/n) ?" % (host, lfw)) == "y":
        flash = True
      else:
        flash = False
    elif perform_flash == True and dry_run == False and silent_run == True:
      flash = True
    elif perform_flash == True and dry_run == True:
      if mode == 'stock' and cfw_type == 'homekit':
        keyword="converted to Official firmware"
      elif cfw_type == 'homekit' or mode == 'stock':
        keyword="upgraded from %s to version %s" % (cfw, lfw)
      else:
        keyword = "converted to HomeKit firmware"
      print("Would have been $keyword...")
    elif not dlurl:
      if ffw:
        keyword = "Version %s is not available yet..." % ffw
      else:
        keyword = "Is not supported yet..."
      print(keyword)
      return 0
    else:
      print("Does not need updating...")
      return 0

    if flash == True:
      write_flash(device, lfw, dlurl, cfw_type, mode)
    elif dry_run == False:
      print("Skipping Flash...")


def device_scan(args, action, do_all, dry_run, silent_run, mode, exclude, forced_version, ffw):
  device = args
  logger.info("\ndevice: %s" % device)
  logger.info("action: %s" % action)
  logger.info("do_all: %s" % do_all)
  logger.info("dry_run: %s" % dry_run)
  logger.info("silent_run: %s" % silent_run)
  logger.info("mode: %s" % mode)
  logger.info("exclude: %s" % exclude)
  logger.info("forced_version: %s" % forced_version)
  logger.info("ffw: %s" % ffw)

  if do_all == False:
    print(WHITE + "Probing Shelly device for info..." + NC)
    if  not ".local" in device:
      device = device + ".local"
    probe_info(device, action, dry_run, silent_run, mode, forced_version, ffw)
  else:
    print(WHITE + "Scanning for Shelly devices..." + NC)
    zeroconf = Zeroconf()
    listener = MyListener()
    browser = ServiceBrowser(zeroconf, "_http._tcp.local.", listener)
    sleep(2)
    zeroconf.close()

    logger.info('device_test: %s' % device_list)
    logger.info('\nproperties: %s' % p_list)

    if exclude == True:
      dl = device
      for d in dl:
        device_list.remove(d)

    device_list.sort()
    for device in device_list:
      probe_info(device + '.local', action, dry_run, silent_run, mode, forced_version, ffw)


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
    exit(2)

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
        print("Invalid option")
        usage()
        exit(2)
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
      if logger.getEffectiveLevel() >= 30:
        logger.setLevel(logging.INFO)
      else:
        logger.setLevel(logging.DEBUG)
    elif opt == '-h':
      usage()
      exit(0)

  logger.info("ARG: %s" % argv)
  logger.info("opts: %s" % opts)
  logger.info("args: %s" % args)
  logger.info("action: %s" % action)
  logger.info("do_all: %s" % do_all)
  logger.info("dry_run: %s" % dry_run)
  logger.info("silent_run: %s" % silent_run)
  logger.info("forced_version: %s" % forced_version)
  logger.info("exclude: %s" % exclude)
  logger.info("mode: %s" % mode)

  if ((not opts and not args) and do_all == False) or (not args and do_all == False) or \
     (args and do_all == True and exclude == False) or (not args and do_all == True and exclude == True):
    usage()
    exit(1)

  try:
    fp = urllib.request.urlopen("https://api.shelly.cloud/files/firmware")
    global stock_release_info
    stock_release_info = json.load(fp)
    fp.close()
  except:
    logger.warning("Failed to lookup version information")
    exit(1)
  try:
    fp = urllib.request.urlopen("https://rojer.me/files/shelly/update.json")
    global homekit_release_info
    homekit_release_info = json.load(fp)
    fp.close()
  except:
    logger.warning("Failed to lookup version information")
    exit(1)

  logger.debug("\n" + WHITE + "stock_release_info:" + NC + " %s" % stock_release_info)
  logger.debug("\n" + WHITE + "homekit_release_info:" + NC + " %s" % homekit_release_info)

  if args and exclude == False:
    for device in args:
      device_scan(device, action, do_all, dry_run, silent_run, mode, exclude, forced_version, ffw)

  if do_all == True and exclude == False:
    device_scan("", action, do_all, dry_run, silent_run, mode, exclude, forced_version, ffw)

  if do_all == True and exclude == True:
    device_scan(args, action, do_all, dry_run, silent_run, mode, exclude, forced_version, ffw)




if __name__ == '__main__':
  app(argv)

__all__ = ['app']
