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
#  usage: flash-shelly.py [-h] [-m {homekit,keep,revert}] [-a] [-q] [-l] [-e [EXCLUDE ...]] [-n] [-y] [-V VERSION] [-c HAP_SETUP_CODE] [--variant VARIANT] [-v {0,1}] [hosts ...]
#
#  Shelly HomeKit flashing script utility
#
#  positional arguments:
#    hosts
#
#  optional arguments:
#    -h, --help            show this help message and exit
#    -m {homekit,keep,revert}, --mode {homekit,keep,revert}
#                          Script mode.
#    -a, --all             Run against all the devices on the network.
#    -q, --quiet           Only include upgradeable shelly devices.
#    -l, --list            List info of shelly device.
#    -e [EXCLUDE ...], --exclude [EXCLUDE ...]
#                          Exclude hosts from found devices.
#    -n, --assume-no       Do a dummy run through.
#    -y, --assume-yes      Do not ask any confirmation to perform the flash.
#    -V VERSION, --version VERSION
#                          Force a particular version.
#    -c HAP_SETUP_CODE, --hap-setup-code HAP_SETUP_CODE
#                          Configure HomeKit setup code, after flashing.
#    --variant VARIANT     Prerelease variant name.
#    -v {0,1}, --verbose {0,1}
#                          Enable verbose logging level.


import argparse
import functools
import json
import logging
import platform
import queue
import re
import socket
import subprocess
import sys
import time

logging.TRACE = 5
logging.addLevelName(logging.TRACE, 'TRACE')
logging.Logger.trace = functools.partialmethod(logging.Logger.log, logging.TRACE)
logging.trace = functools.partial(logging.log, logging.TRACE)
logging.basicConfig(format='%(message)s')
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

upgradeable_devices = 0
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

def upgrade_pip():
  logger.info("Updating pip...")
  pipe = subprocess.check_output([sys.executable, '-m', 'pip', 'install', '--upgrade', 'pip'])

try:
  import zeroconf
except ImportError:
  logger.info("Installing zeroconf...")
  upgrade_pip()
  pipe = subprocess.check_output([sys.executable, '-m', 'pip', 'install', 'zeroconf'])
  import zeroconf
try:
  import requests
except ImportError:
  logger.info("Installing requests...")
  upgrade_pip()
  pipe = subprocess.check_output([sys.executable, '-m', 'pip', 'install', 'requests'])
  import requests

class MyListener:
  def __init__(self):
    self.queue = queue.Queue()

  def add_service(self, zeroconf, type, device):
    device = device.replace('._http._tcp.local.', '')
    self.queue.put(Device(device))

  def remove_service(self, *args, **kwargs):
    pass

  def update_service(self, *args, **kwargs):
    pass


class Device:
  def __init__(self, host=None, wifi_ip=None, fw_type=None, device_url=None, info=None, variant=None, version=None):
    self.host = f'{host}.local' if '.local' not in host and not host[0:3].isdigit() else host
    self.friendly_host = host.replace('.local','')
    self.fw_type = fw_type
    self.device_url = device_url
    self.info = info
    self.variant = variant
    self.version = version
    self.wifi_ip = wifi_ip
    self.flash_label = "Latest:"

  def is_valid_hostname(self, hostname):
    if len(hostname) > 255:
        result = False
    allowed = re.compile("(?!-)[A-Z\d-]{1,63}(?<!-)$", re.IGNORECASE)
    result = all(allowed.match(x) for x in hostname.split("."))
    logger.trace(f"Valid Hostname: {hostname} {result}")
    return result

  def is_host_online(self, host):
    try:
      self.wifi_ip = socket.gethostbyname(host)
      logger.trace(f"Hostname: {host} is Online")
      return True
    except:
      logger.warning(f"{RED}Could not resolve host: {host}{NC}")
      return False

  def get_device_url(self):
    self.device_url = None
    if self.is_valid_hostname(self.host) and self.is_host_online(self.host):
      try:
        homekit_fwcheck = requests.head(f'http://{self.wifi_ip}/rpc/Shelly.GetInfo', timeout=3)
        stock_fwcheck = requests.head(f'http://{self.wifi_ip}/settings', timeout=3)
        if homekit_fwcheck.status_code == 200:
          self.fw_type = "homekit"
          self.device_url = f'http://{self.wifi_ip}/rpc/Shelly.GetInfo'
        elif stock_fwcheck.status_code == 200:
          self.fw_type = "stock"
          self.device_url = f'http://{self.wifi_ip}/settings'
      except:
        pass
    logger.trace(f"Device URL: {self.device_url}")
    return self.device_url

  def get_device_info(self):
    info = None
    if self.get_device_url():
      try:
        fp = requests.get(self.device_url, timeout=3)
        if fp.status_code == 200:
          info = json.loads(fp.content)
      except requests.exceptions.RequestException as err:
        logger.debug(f"Error: {err}")
        logger.debug(fp.status_code)
    else:
      logger.debug(f"{RED}Could not get info from device: {self.host}\n{NC}")
    self.info = info
    return info

  def parse_stock_version(self, version):
    # stock version is '20201124-092159/v1.9.0@57ac4ad8', we need '1.9.0'
    if '/v' in version:
      parsed_version = version.split('/v')[1].split('@')[0]
    else:
      parsed_version = '0.0.0'
    return parsed_version

  def get_current_version(self): # used when flashing between formware versions.
    info = self.get_device_info()
    if not info:
      return None
    if self.fw_type == 'homekit':
      version = info['version']
    elif self.fw_type == 'stock':
      version = self.parse_stock_version(info['fw'])
    return version

  def shelly_model(self, type):
    options = {'SHSW-1' : 'Shelly1',
               'SHSW-L' : 'Shelly1L',
               'SHSW-PM' : 'Shelly1PM',
               'SHSW-21' : 'Shelly2',
               'SHSW-25' : 'Shelly25',
               'SHPLG-1' : 'ShellyPlug',
               'SHPLG2-1' : 'ShellyPlug',
               'SHPLG-S' : 'ShellyPlugS',
               'SHPLG-U1' : 'ShellyPlugUS',
               'SHIX3-1' : 'ShellyI3',
               'SHBTN-1' : 'ShellyButton1',
               'SHBLB-1' : 'ShellyBulb',
               'SHVIN-1' : 'ShellyVintage',
               'SHBDUO-1' : 'ShellyDuo',
               'SHDM-1' : 'ShellyDimmer1',
               'SHDM-2' : 'ShellyDimmer2',
               'SHRGBW2' : 'ShellyRGBW2',
               'SHDW-1' : 'ShellyDoorWindow1',
               'SHDW-2' : 'ShellyDoorWindow2',
               'SHHT-1' : 'ShellyHT',
               'SHSM-01' : 'ShellySmoke',
               'SHWT-1' : 'ShellyFlood',
               'SHGS-1' : 'ShellyGas',
               'SHEM' : 'ShellyEM',
               'SHEM-3' : 'Shelly3EM',
               'switch1' : 'Shelly1',
               'switch1pm' : 'Shelly1PM',
               'switch2' : 'Shelly2',
               'switch25' : 'Shelly25',
               'shelly-plug-s' : 'ShellyPlugS',
               'dimmer1' : 'ShellyDimmer1',
               'dimmer2' : 'ShellyDimmer2',
               'rgbw2' : 'ShellyRGBW2',
    }
    return options.get(type, type)

  def update_homekit(self, release_info=None):
    self.flash_fw_type_str = 'HomeKit'
    self.flash_fw_type = 'homekit'
    if not self.version:
      for i in release_info:
        if self.variant and self.variant not in i[1]['version']:
          self.flash_fw_version = 'novariant'
          self.dlurl = None
          return
        if self.variant:
          re_search = '-*'
        else:
          re_search = i[0]
        if re.search(re_search, self.fw_version):
          self.flash_fw_version = i[1]['version']
          self.dlurl = i[1]['urls'][self.model] if self.model in i[1]['urls'] else None
          break
    else:
      self.flash_label = "Manual:"
      self.flash_fw_version = self.version
      self.dlurl = f'http://rojer.me/files/shelly/{self.version}/shelly-homekit-{self.model}.zip'

  def update_stock(self, release_info=None):
    self.flash_fw_type_str = 'Stock'
    self.flash_fw_type = 'stock'
    stock_model_info = release_info['data'][self.stock_model]
    if not self.version:
      if self.variant == 'beta':
        self.flash_fw_version = self.parse_stock_version(stock_model_info['beta_ver']) if 'beta_ver' in stock_model_info else self.parse_stock_version(stock_model_info['version'])
        self.dlurl = stock_model_info['beta_url'] if 'beta_ver' in stock_model_info else stock_model_info['url']
      else:
        self.flash_fw_version = self.parse_stock_version(stock_model_info['version'])
        self.dlurl = stock_model_info['url']
    else:
      self.flash_label = "Manual:"
      self.flash_fw_version = self.version
      self.dlurl = f'http://archive.shelly-faq.de/version/v{self.version}/{self.stock_model}.zip'
    if self.stock_model  == 'SHRGBW2':
      self.dlurl = self.dlurl.replace('.zip',f'-{self.colour_mode}.zip')

class HomeKitDevice(Device):
  def get_info(self):
    if not self.info:
      return False
    self.fw_type_str = 'HomeKit'
    self.fw_version = self.info['version']
    self.model = self.info['model'] if 'model' in self.info else self.shelly_model(self.info['app'])
    self.stock_model = self.info['stock_model'] if 'stock_model' in self.info else None
    self.device_id = self.info['device_id'] if 'device_id' in self.info else None
    self.device_name = self.info['name'] if 'name' in self.info else None
    self.colour_mode = self.info['colour_mode'] if 'colour_mode' in self.info else None
    return True

  def update_to_homekit(self, release_info=None):
    logger.debug('Mode: HomeKit To HomeKit')
    self.update_homekit(release_info)

  def update_to_stock(self, release_info=None):
    logger.debug('Mode: HomeKit To Stock')
    self.update_stock(release_info)

  def set_force_version(self, version):
    self.flash_fw_version = version

  def flash_firmware(self):
    logger.info("Downloading Firmware...")
    logger.debug(f"DURL: {self.dlurl}")
    myfile = requests.get(self.dlurl)
    logger.info("Now Flashing...")
    files = {'file': ('shelly-flash.zip', myfile.content)}
    logger.debug(f"requests.post(f'http://{self.wifi_ip}/update' , files=files")
    response = requests.post(f'http://{self.wifi_ip}/update' , files=files)
    logger.debug(response.text)


class StockDevice(Device):
  def get_info(self):
    if not self.info:
      return False
    self.fw_type_str = 'Stock'
    self.fw_version = self.parse_stock_version(self.info['fw'])  # current firmware version
    self.model = self.shelly_model(self.info['device']['type'])
    self.stock_model = self.info['device']['type']
    self.device_id = self.info['mqtt']['id'] if 'id' in self.info['mqtt'] else self.friendly_host
    self.device_name = self.info['name'] if 'name' in self.info else None
    self.colour_mode = self.info['mode'] if 'mode' in self.info else None
    return True

  def update_to_homekit(self, release_info=None):
    logger.debug('Mode: Stock To HomeKit')
    self.update_homekit(release_info)

  def update_to_stock(self, release_info=None):
    logger.debug('Mode: Stock To Stock')
    self.update_stock(release_info)

  def set_force_version(self, version):
    self.flash_fw_version = version

  def flash_firmware(self):
    logger.info("Now Flashing...")
    dlurl = self.dlurl.replace('https', 'http')
    logger.debug(f"curl -qsS http://{self.wifi_ip}/ota?url={dlurl}")
    response = requests.get(f'http://{self.wifi_ip}/ota?url={dlurl}')
    logger.debug(response.text)


def parse_version(vs):
  # 1.9.2_1L
  # 1.9.3-rc3 / 2.7.0-beta1
  pp = vs.split('_') if '_' in vs else vs.split('-')
  v = pp[0].split('.')
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

def is_newer(v1, v2):
  vi1 = parse_version(v1)
  vi2 = parse_version(v2)
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

def write_hap_setup_code(wifi_ip, hap_setup_code):
  logger.info("Configuring HomeKit setup code...")
  value={'code': hap_setup_code}
  logger.debug(f"requests.post(url='http://{wifi_ip}/rpc/HAP.Setup', json={value}")
  response = requests.post(url=f'http://{wifi_ip}/rpc/HAP.Setup', json={'code': hap_setup_code})
  logger.debug(response.text)
  if response.text.startswith('null'):
    logger.info(f"Done.")

def write_flash(device_info, hap_setup_code):
  logger.debug(f"\n{WHITE}write_flash{NC}")
  flashed = False
  device_info.flash_firmware()
  logger.info(f"waiting for {device_info.friendly_host} to reboot...")
  time.sleep(3)
  n = 1
  waittextshown = False
  info = None
  while n < 20:
    if n == 10:
      logger.info(f"still waiting for {device_info.friendly_host} to reboot...")
    onlinecheck = device_info.get_current_version()
    time.sleep(1)
    n += 1
    if onlinecheck == device_info.flash_fw_version:
      break
    time.sleep(2)
  if onlinecheck == device_info.flash_fw_version:
    logger.info(f"{GREEN}Successfully flashed {device_info.friendly_host} to {device_info.flash_fw_version}{NC}")
    if hap_setup_code:
      write_hap_setup_code(device_info.wifi_ip, hap_setup_code)
  else:
    if device_info.stock_model == 'SHRGBW2':
      logger.info("\nTo finalise flash process you will need to switch 'Modes' in the device WebUI,")
      logger.info(f"{WHITE}WARNING!!{NC} If you are using this device in conjunction with Homebridge")
      logger.info(f"{WHITE}STOP!!{NC} homebridge before performing next steps.")
      logger.info(f"Goto http://{device_info.host} in your web browser")
      logger.info("Goto settings section")
      logger.info("Goto 'Device Type' and switch modes")
      logger.info("Once mode has been changed, you can switch it back to your preferred mode")
      logger.info(f"Restart homebridge.")
    elif onlinecheck == '0.0.0':
      logger.info(f"{RED}Flash may have failed, please manually check version{NC}")
    else:
      logger.info(f"{RED}Failed to flash {device_info.friendly_host} to {device_info.flash_fw_version}{NC}")
    logger.debug("Current: %s" % onlinecheck)

def parse_info(device_info, action, dry_run, quiet_run, silent_run, mode, exclude, hap_setup_code, requires_upgrade):
  logger.debug(f"\n{WHITE}parse_info{NC}")
  logger.trace(f"device_info: {device_info}")

  perform_flash = False
  flash = False
  host = device_info.host
  friendly_host = device_info.friendly_host
  device_id = device_info.device_id
  device_name = device_info.device_name
  wifi_ip = device_info.wifi_ip
  current_fw_version = device_info.fw_version
  current_fw_type = device_info.fw_type
  current_fw_type_str = device_info.fw_type_str
  flash_fw_version = device_info.flash_fw_version
  flash_fw_type_str = device_info.flash_fw_type_str
  force_version = device_info.version
  model = device_info.model
  stock_model = device_info.stock_model
  colour_mode = device_info.colour_mode
  dlurl = device_info.dlurl
  flash_label = device_info.flash_label

  logger.debug(f"host: {host}")
  logger.debug(f"device_name: {device_name}")
  logger.debug(f"device_id: {device_id}")
  logger.debug(f"model: {model}")
  logger.debug(f"stock_model: {stock_model}")
  logger.debug(f"colour_mode: {colour_mode}")
  logger.debug(f"action: {action}")
  logger.debug(f"flash mode: {mode}")
  logger.debug(f"requires_upgrade: {requires_upgrade}")
  logger.debug(f"current_fw_version: {current_fw_version}")
  logger.debug(f"flash_fw_version: {flash_fw_version}")
  logger.debug(f"force_version: {force_version}\n")

  if force_version:
    device_info.set_force_version(force_version)
    flash_fw_version = force_version

  if dlurl:
    durl_request = requests.head(dlurl)
  if flash_fw_version == 'novariant':
    latest_fw_label = f"{RED}No {device_info.variant} available{NC}"
    flash_fw_version = '0.0.0'
    dlurl = None
  elif not dlurl or durl_request.status_code != 200:
    latest_fw_label = f"{RED}Not available{NC}"
    flash_fw_version = '0.0.0'
    dlurl = None
  else:
    latest_fw_label = flash_fw_version

  if not quiet_run or (quiet_run and (is_newer(flash_fw_version, current_fw_version) or force_version and dlurl and parse_version(flash_fw_version) != parse_version(current_fw_version))):
    logger.info(f"\n{WHITE}Host: {NC}http://{host}")
    logger.info(f"{WHITE}Device Name: {NC}{device_name}")
    logger.info(f"{WHITE}Device ID: {NC}{device_id}")
    logger.info(f"{WHITE}IP: {NC}{wifi_ip}")
    logger.info(f"{WHITE}Model: {NC}{model}")
    logger.info(f"{WHITE}Current: {NC}{current_fw_type_str} {current_fw_version}")
    col = YELLOW if is_newer(flash_fw_version, current_fw_version) else WHITE
    logger.info(f"{WHITE}{flash_label} {NC}{flash_fw_type_str} {col}{latest_fw_label}{NC}")
    logger.debug(f"{WHITE}D_URL: {NC}{dlurl}")

  if dlurl and ((force_version and parse_version(flash_fw_version) != parse_version(current_fw_version)) or requires_upgrade or (current_fw_type != mode) or (current_fw_type == mode and is_newer(flash_fw_version, current_fw_version))):
    global upgradeable_devices
    upgradeable_devices += 1

  if action != 'list':
    if dry_run == True:
      message = "Would have been"
      keyword = ""
    else:
      message = f"Do you wish to flash"
      keyword = f"{friendly_host} to firmware version {flash_fw_version}"
    if dlurl:
      if exclude and friendly_host in exclude:
        logger.info("Skipping as device has been excluded...\n")
        return 0
      elif force_version and parse_version(flash_fw_version) != parse_version(current_fw_version):
        perform_flash = True
        keyword = f"reflashed version {force_version}"
      elif requires_upgrade:
        perform_flash = True
        if mode == 'stock':
          keyword = f"upgraded to version {flash_fw_version}"
        elif mode == 'homekit':
          message = "This device needs to be"
          keyword = "upgraded to latest stock firmware version, before you can flash to HomeKit"
      elif current_fw_type != mode:
        perform_flash = True
        if mode == 'stock':
          keyword = "converted to Official firmware"
        elif mode == 'homekit':
          keyword = "converted to HomeKit firmware"
      elif current_fw_type == mode and is_newer(flash_fw_version, current_fw_version):
        perform_flash = True
        keyword = f"upgraded from {current_fw_version} to version {flash_fw_version}"
    elif not dlurl:
      if force_version:
        keyword = f"Version {force_version} is not available..."
      else:
        keyword = "Is not supported yet..."
      if not quiet_run:
        logger.info(f"{keyword}")
      return 0
    else:
      if not quiet_run:
        logger.info("Does not need flashing...")
      return 0

    logger.debug(f"\nperform_flash: {perform_flash}\n")
    if perform_flash == True and dry_run == False and silent_run == False:
      if requires_upgrade == True:
        flash_message = f"{message} {keyword}"
      else:
        flash_message = f"Do you wish to flash {friendly_host} to firmware version {flash_fw_version}"
      if input(f"{flash_message} (y/n) ? ") == 'y':
        flash = True
      else:
        flash = False
        logger.info("Skipping Flash...")
    elif perform_flash == True and dry_run == False and silent_run == True:
      flash = True
    elif perform_flash == True and dry_run == True:
      logger.info(f"{message} {keyword}...")
    if flash == True:
      write_flash(device_info, hap_setup_code)

def probe_device(device, action, dry_run, quiet_run, silent_run, mode, exclude, version, variant, hap_setup_code):
  logger.debug(f"\n{WHITE}probe_device{NC}")
  d_info = json.dumps(device, indent = 4)
  logger.trace(f"Device Info: {d_info}")

  requires_upgrade = False
  if mode == 'keep':
    flashmode = device['fw_type']
  else:
    flashmode = mode
  if device['fw_type'] == 'homekit':
    deviceinfo = HomeKitDevice(device['host'], device['wifi_ip'], device['fw_type'], device['device_url'], device['info'], variant, version)
  elif device['fw_type'] == 'stock':
    deviceinfo = StockDevice(device['host'], device['wifi_ip'], device['fw_type'], device['device_url'], device['info'], variant, version)
  if not deviceinfo.get_info():
    logger.warning(f"{RED}Failed to lookup local information of {device['host']}{NC}")
  else:
    if flashmode == 'homekit' and deviceinfo.fw_type == 'stock':
      deviceinfo.update_to_stock(stock_release_info)
      if (deviceinfo.fw_version == '0.0.0' or is_newer(deviceinfo.flash_fw_version, deviceinfo.fw_version)):
        requires_upgrade = True
      else:
        deviceinfo.update_to_homekit(homekit_release_info)
    elif flashmode == 'homekit':
      deviceinfo.update_to_homekit(homekit_release_info)
    elif flashmode == 'stock':
      deviceinfo.update_to_stock(stock_release_info)
    if deviceinfo.fw_type == "homekit" and float(f"{parse_version(deviceinfo.info['version'])[0]}.{parse_version(deviceinfo.info['version'])[1]}") < 2.1:
      logger.info(f"{WHITE}Host: {NC}{deviceinfo.host}")
      logger.info(f"Version {deviceinfo.info['version']} is to old for this script,")
      logger.info(f"please update via the device webUI.\n")
    else:
      parse_info(deviceinfo, action, dry_run, quiet_run, silent_run, flashmode, exclude, hap_setup_code, requires_upgrade)

def device_scan(hosts, action, dry_run, quiet_run, silent_run, mode, exclude, version, variant, hap_setup_code):
  logger.debug(f"\n{WHITE}device_scan{NC}")
  if hosts:
    for host in hosts:
      deviceinfo = Device(host)
      deviceinfo.get_device_info()
      if deviceinfo.fw_type is not None:
        device = {'host': deviceinfo.host, 'wifi_ip': deviceinfo.wifi_ip, 'fw_type': deviceinfo.fw_type, 'device_url': deviceinfo.device_url, 'info': deviceinfo.info}
        probe_device(device, action, dry_run, quiet_run, silent_run, mode, exclude, version, variant, hap_setup_code)
  else:
    logger.info(f"{WHITE}Scanning for Shelly devices...{NC}")
    zc = zeroconf.Zeroconf()
    listener = MyListener()
    browser = zeroconf.ServiceBrowser(zc, '_http._tcp.local.', listener)
    total_devices = 0
    while True:
      try:
        deviceinfo = listener.queue.get(timeout=20)
      except queue.Empty:
        logger.info(f"\n{GREEN}Devices found: {total_devices} Upgradeable: {upgradeable_devices}{NC}")
        zc.close()
        break
      deviceinfo.get_device_info()
      if deviceinfo.fw_type is not None:
        device = {'host': deviceinfo.host, 'wifi_ip': deviceinfo.wifi_ip, 'fw_type': deviceinfo.fw_type, 'device_url': deviceinfo.device_url, 'info' : deviceinfo.info}
        total_devices += 1
        probe_device(device, action, dry_run, quiet_run, silent_run, mode, exclude, version, variant, hap_setup_code)

if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Shelly HomeKit flashing script utility')
  parser.add_argument('-m', '--mode', action="store", choices=['homekit', 'keep', 'revert'], default="homekit", help="Script mode.")
  parser.add_argument('-a', '--all', action="store_true", dest='do_all', default=False, help="Run against all the devices on the network.")
  parser.add_argument('-q', '--quiet', action="store_true", dest='quiet_run', default=False, help="Only include upgradeable shelly devices.")
  parser.add_argument('-l', '--list', action="store_true", default=False, help="List info of shelly device.")
  parser.add_argument('-e', '--exclude', action="store", dest="exclude", nargs='*', help="Exclude hosts from found devices.")
  parser.add_argument('-n', '--assume-no', action="store_true", dest='dry_run', default=False, help="Do a dummy run through.")
  parser.add_argument('-y', '--assume-yes', action="store_true", dest='silent_run', default=False, help="Do not ask any confirmation to perform the flash.")
  parser.add_argument('-V', '--version',type=str, action="store", dest="version", default=False, help="Force a particular version.")
  parser.add_argument('-c', '--hap-setup-code', action="store", dest="hap_setup_code", default=False, help="Configure HomeKit setup code, after flashing.")
  parser.add_argument('--variant', action="store", dest="variant", default=False, help="Prerelease variant name.")
  parser.add_argument('-v', '--verbose', action="store", dest="verbose", choices=['0', '1'], help="Enable verbose logging level.")
  parser.add_argument('hosts', type=str, nargs='*')
  args = parser.parse_args()
  action = 'list' if args.list else 'flash'
  args.mode = 'stock' if args.mode == 'revert' else args.mode
  args.hap_setup_code = f"{args.hap_setup_code[:3]}-{args.hap_setup_code[3:-3]}-{args.hap_setup_code[5:]}" if args.hap_setup_code and '-' not in args.hap_setup_code else args.hap_setup_code
  if args.verbose and '0' in args.verbose:
    logger.setLevel(logging.DEBUG)
  elif args.verbose and '1' in args.verbose:
    logger.setLevel(logging.TRACE)

  homekit_release_info = None
  stock_release_info = None

  logger.debug(f"{WHITE}app{NC}")
  logger.debug(f"{PURPLE}OS: {arch}{NC}")
  logger.debug(f"manual_hosts: {args.hosts}")
  logger.debug(f"action: {action}")
  logger.debug(f"mode: {args.mode}")
  logger.debug(f"do_all: {args.do_all}")
  logger.debug(f"dry_run: {args.dry_run}")
  logger.debug(f"quiet_run: {args.quiet_run}")
  logger.debug(f"silent_run: {args.silent_run}")
  logger.debug(f"hap_setup_code: {args.hap_setup_code}")
  logger.debug(f"version: {args.version}")
  logger.debug(f"exclude: {args.exclude}")
  logger.debug(f"variant: {args.variant}")
  logger.debug(f"verbose: {args.verbose}")

  if not args.hosts and not args.do_all:
    logger.info(f"{WHITE}Requires a hostname or -a | --all{NC}")
    parser.print_help()
    sys.exit(1)
  elif args.hosts and args.do_all:
    logger.info(f"{WHITE}Invalid option hostname or -a | --all not both.{NC}")
    parser.print_help()
    sys.exit(1)
  if args.version and len(args.version.split('.')) < 3:
    logger.info(f"{WHITE}Incorect version formatting i.e '1.9.0'{NC}")
    parser.print_help()
    sys.exit(1)

  try:
    fp = requests.get("https://api.shelly.cloud/files/firmware", timeout=3)
    if fp.status_code == 200:
      stock_release_info = json.loads(fp.content)
  except requests.exceptions.RequestException as err:
    logger.debug(fp.status_code)
  try:
    fp = requests.get("https://rojer.me/files/shelly/update.json", timeout=3)
    if fp.status_code == 200:
      homekit_release_info = json.loads(fp.content)
  except requests.exceptions.RequestException as err:
    logger.debug(err)

  logger.trace(f"\n{WHITE}stock_release_info:{NC}{stock_release_info}")
  logger.trace(f"\n{WHITE}homekit_release_info:{NC}{homekit_release_info}")

  if not stock_release_info or not homekit_release_info:
    logger.warning(f"{RED}Failed to lookup online version information{NC}")
    logger.warning("For more information please point your web browser to:")
    logger.warning("https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#script-fails-to-run")
  else:
    device_scan(args.hosts, action, args.dry_run, args.quiet_run, args.silent_run, args.mode, args.exclude, args.version, args.variant, args.hap_setup_code)
