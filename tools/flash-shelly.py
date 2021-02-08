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
#  usage: flash-shelly.py [-h] [-m {homekit,keep,revert}] [-t {homekit,stock,all}] [-a] [-q] [-l] [-e [EXCLUDE ...]] [-n] [-y] [-V VERSION] [--variant VARIANT] [-c HAP_SETUP_CODE] [--ip-type {dhcp,static}]
#                         [--ip IPV4_IP] [--gw IPV4_GW] [--mask IPV4_MASK] [--dns IPV4_DNS] [-v {0,1,2,3,4,5}] [--log-file LOG_FILENAME]
#                         [hosts ...]
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
#    -t {homekit,stock,all}, --type {homekit,stock,all}
#                          Limit scan to current firmware type.
#    -a, --all             Run against all the devices on the network.
#    -q, --quiet           Only include upgradeable shelly devices.
#    -l, --list            List info of shelly device.
#    -e [EXCLUDE ...], --exclude [EXCLUDE ...]
#                          Exclude hosts from found devices.
#    -n, --assume-no       Do a dummy run through.
#    -y, --assume-yes      Do not ask any confirmation to perform the flash.
#    -V VERSION, --version VERSION
#                          Force a particular version.
#    --variant VARIANT     Prerelease variant name.
#    -c HAP_SETUP_CODE, --hap-setup-code HAP_SETUP_CODE
#                          Configure HomeKit setup code, after flashing.
#    --ip-type {dhcp,static}
#                          Configure network IP type (Static or DHCP)
#    --ip IPV4_IP          IP address
#    --gw IPV4_GW          Gateway IP address
#    --mask IPV4_MASK      Subnet mask address
#    --dns IPV4_DNS        DNS IP address
#    -v {0,1,2,3,4,5}, --verbose {0,1,2,3,4,5}
#                          Enable verbose logging 0=critical, 1=error, 2=warning, 3=info, 4=debug, 5=trace.
#    --log-file LOG_FILENAME
#                          Create output log file with chosen filename.


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
logger = logging.getLogger(__name__)
logger.setLevel(logging.TRACE)
log_level = {'0' : logging.CRITICAL,
             '1' : logging.ERROR,
             '2' : logging.WARNING,
             '3' : logging.INFO,
             '4' : logging.DEBUG,
             '5' : logging.TRACE}

upgradeable_devices = 0
flashed_devices = 0
failed_flashed_devices = 0
arch = platform.system()

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
    logger.trace(f"[Device Scan] found device: {device}")
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
    logger.debug(f"Valid Hostname: {hostname} {result}")
    return result

  def is_host_online(self, host):
    try:
      self.wifi_ip = socket.gethostbyname(host)
      logger.debug(f"Hostname: {host} is Online")
      return True
    except:
      logger.error(f"\n{RED}Could not resolve host: {host}{NC}")
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
    else:
      logger.debug(f"Could not get info from device: {self.host}")
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
               'SHSEN-1' : 'ShellySensor1',
               'switch1' : 'Shelly1',
               'switch1pm' : 'Shelly1PM',
               'switch2' : 'Shelly2',
               'switch25' : 'Shelly25',
               'shelly-plug-s' : 'ShellyPlugS',
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
      self.dlurl = f'http://archive.shelly-tools.de/version/v{self.version}/{self.stock_model}.zip'
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
    logger.trace(response.text)


def parse_version(vs):
  # 1.9.2_1L
  # 1.9.3-rc3 / 2.7.0-beta1 / 2.7.0-latest
  v = re.search("^(?P<major>\d+).(?P<minor>\d+).(?P<patch>\d+)(?:_(?P<model>[a-zA-Z0-9]*))?(?:-(?P<prerelease>[a-zA-Z]*)(?P<prerelease_seq>\d*))?$", vs)
  logger.debug(f'group:{v.groupdict()}')
  major = int(v.group('major'))
  minor = int(v.group('minor'))
  patch = int(v.group('patch'))
  variant = v.group('model') if v.group('model') else v.group('prerelease')
  varSeq = int(v.group('prerelease_seq')) if v.group('prerelease_seq') and v.group('prerelease_seq').isdigit() else 0
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

def write_network_type(device_info, network_type, ipv4_ip='', ipv4_mask='', ipv4_gw='', ipv4_dns=''):
  wifi_ip = device_info.wifi_ip
  if device_info.fw_type == 'homekit':
    if network_type == 'static':
      message = f"Configuring static IP to {ipv4_ip}..."
      value={'config': {'wifi': {'sta': {'ip': ipv4_ip, 'netmask': ipv4_mask, 'gw': ipv4_gw, 'nameserver': ipv4_dns}}}}
    else:
      message = f"Configuring IP to use DHCP..."
      value={'config': {'wifi': {'sta': {'ip': ''}}}}
    config_set_url = f'http://{wifi_ip}/rpc/Config.Set'
    logger.info(message)
    logger.debug(f"requests.post(url={config_set_url}, json={value}")
    response = requests.post(url=config_set_url, json=value)
    logger.trace(response.text)
    if response.text.find('"saved": true') > 0:
      logger.info(f"Saved, Rebooting...")
      logger.debug(f"requests.post(url={f'http://{wifi_ip}/rpc/SyS.Reboot'}")
      response = requests.get(url=f'http://{wifi_ip}/rpc/SyS.Reboot')
      logger.trace(response.text)
  else:
    if network_type == 'static':
      message = f"Configuring static IP to {ipv4_ip}..."
      config_set_url = f'http://{wifi_ip}/settings/sta?ipv4_method=static&ip={ipv4_ip}&netmask={ipv4_mask}&gateway={ipv4_gw}&dns={ipv4_dns}'
    else:
      message = f"Configuring IP to use DHCP..."
      config_set_url = f'http://{wifi_ip}/settings/sta?ipv4_method=dhcp'
    logger.info(message)
    logger.debug(f"requests.post(url={config_set_url}")
    response = requests.post(url=config_set_url)
    logger.trace(response.text)
    logger.info(f"Saved...")

def write_hap_setup_code(wifi_ip, hap_setup_code):
  logger.info("Configuring HomeKit setup code...")
  value={'code': hap_setup_code}
  logger.debug(f"requests.post(url='http://{wifi_ip}/rpc/HAP.Setup', json={value}")
  response = requests.post(url=f'http://{wifi_ip}/rpc/HAP.Setup', json={'code': hap_setup_code})
  logger.trace(response.text)
  if response.text.startswith('null'):
    logger.info(f"Done.")

def write_flash(device_info):
  logger.debug(f"{PURPLE}[Write Flash]{NC}")
  flashed = False
  device_info.flash_firmware()
  logger.info(f"waiting for {device_info.friendly_host} to reboot...")
  time.sleep(3)
  n = 1
  waittextshown = False
  info = None
  while n < 40:
    if n == 15:
      logger.info(f"still waiting for {device_info.friendly_host} to reboot...")
    elif n == 30:
      logger.info(f"we'll wait just a little longer for {device_info.friendly_host} to reboot...")
    onlinecheck = device_info.get_current_version()
    time.sleep(1)
    n += 1
    if onlinecheck == device_info.flash_fw_version:
      break
    time.sleep(2)
  if onlinecheck == device_info.flash_fw_version:
    global flashed_devices
    flashed_devices +=1
    logger.critical(f"{GREEN}Successfully flashed {device_info.friendly_host} to {device_info.flash_fw_version}{NC}")
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
      global failed_flashed_devices
      failed_flashed_devices +=1
      logger.info(f"{RED}Failed to flash {device_info.friendly_host} to {device_info.flash_fw_version}{NC}")
    logger.debug("Current: %s" % onlinecheck)

def parse_info(device_info, action, dry_run, quiet_run, silent_run, mode, exclude, hap_setup_code, requires_upgrade, network_type, ipv4_ip, ipv4_mask, ipv4_gw, ipv4_dns):
  logger.debug(f"")
  logger.debug(f"{PURPLE}[Parse Info]{NC}")
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
  sys_temp = device_info.info.get('sys_temp', None)

  logger.debug(f"host: {host}")
  logger.debug(f"device_name: {device_name}")
  logger.debug(f"device_id: {device_id}")
  logger.debug(f"wifi_ip: {wifi_ip}")
  logger.debug(f"model: {model}")
  logger.debug(f"stock_model: {stock_model}")
  logger.debug(f"colour_mode: {colour_mode}")
  logger.debug(f"sys_temp: {sys_temp}")
  logger.debug(f"action: {action}")
  logger.debug(f"flash mode: {mode}")
  logger.debug(f"requires_upgrade: {requires_upgrade}")
  logger.debug(f"current_fw_version: {current_fw_type_str} {current_fw_version}")
  logger.debug(f"flash_fw_version: {flash_fw_type_str} {flash_fw_version}")
  logger.debug(f"force_version: {force_version}")
  logger.debug(f"dlurl: {dlurl}")


  if force_version:
    device_info.set_force_version(force_version)
    flash_fw_version = force_version

  if dlurl:
    durl_request = requests.head(dlurl)
    logger.debug(f"durl_request: {durl_request}")
  if flash_fw_version == 'novariant':
    latest_fw_label = f"{RED}No {device_info.variant} available{NC}"
    flash_fw_version = '0.0.0'
    dlurl = None
  elif not dlurl or durl_request.status_code != 200 or (durl_request.headers.get('Content-Type', '') != 'application/zip'):
    latest_fw_label = f"{RED}Not available{NC}"
    flash_fw_version = '0.0.0'
    dlurl = None
  else:
    latest_fw_label = flash_fw_version

  if (not quiet_run or (quiet_run and (is_newer(flash_fw_version, current_fw_version) or force_version and dlurl and parse_version(flash_fw_version) != parse_version(current_fw_version)))) and requires_upgrade != 'Done':
    logger.info(f"")
    logger.info(f"{WHITE}Host: {NC}http://{host}")
    logger.info(f"{WHITE}Device Name: {NC}{device_name}")
    logger.info(f"{WHITE}Device ID: {NC}{device_id}")
    logger.info(f"{WHITE}IP: {NC}{wifi_ip}")
    logger.info(f"{WHITE}Model: {NC}{model}")
    if sys_temp is not None:
      logger.info(f"{WHITE}Sys Temp: {NC}{sys_temp}˚c{NC}")
    logger.info(f"{WHITE}Current: {NC}{current_fw_type_str} {current_fw_version}")
    col = YELLOW if is_newer(flash_fw_version, current_fw_version) else WHITE
    logger.info(f"{WHITE}{flash_label} {NC}{flash_fw_type_str} {col}{latest_fw_label}{NC}")

  if dlurl and ((force_version and parse_version(flash_fw_version) != parse_version(current_fw_version)) or requires_upgrade == True or (current_fw_type != mode) or (current_fw_type == mode and is_newer(flash_fw_version, current_fw_version))):
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
      elif requires_upgrade == True:
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

    logger.debug(f"perform_flash: {perform_flash}")
    if perform_flash == True and dry_run == False and silent_run == False:
      if requires_upgrade == True:
        flash_message = f"{message} {keyword}"
      elif requires_upgrade == 'Done':
        flash_message = f"Do you wish to contintue to flash {friendly_host} to HomeKit firmware version {flash_fw_version}"
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
      write_flash(device_info)
    if device_info.fw_type == 'homekit' and hap_setup_code:
      write_hap_setup_code(device_info.wifi_ip, hap_setup_code)
    if network_type:
      if network_type == 'static':
        message = f"Do you wish to set your IP address to {ipv4_ip}"
      else:
        message = f"Do you wish to set your IP address to use DHCP"
      if input(f"{message} (y/n) ? ") == 'y':
        set_ip = True
      else:
        set_ip = False
      if set_ip or silent_run:
        write_network_type(device_info, network_type, ipv4_ip, ipv4_mask, ipv4_gw, ipv4_dns)


def probe_device(device, action, dry_run, quiet_run, silent_run, mode, exclude, version, variant, hap_setup_code, network_type, ipv4_ip, ipv4_mask, ipv4_gw, ipv4_dns):
  logger.debug("")
  logger.debug(f"{PURPLE}[Probe Device]{NC}")
  d_info = json.dumps(device, indent = 4)
  logger.trace(f"device_info: {d_info}")

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
      logger.error(f"{WHITE}Host: {NC}{deviceinfo.host}")
      logger.error(f"Version {deviceinfo.info['version']} is to old for this script,")
      logger.error(f"please update via the device webUI.\n")
    else:
      parse_info(deviceinfo, action, dry_run, quiet_run, silent_run, flashmode, exclude, hap_setup_code, requires_upgrade, network_type, ipv4_ip, ipv4_mask, ipv4_gw, ipv4_dns)
      if requires_upgrade:
        requires_upgrade = 'Done'
        deviceinfo.get_info()
        if not is_newer(deviceinfo.flash_fw_version, deviceinfo.fw_version):
          deviceinfo.update_to_homekit(homekit_release_info)
          parse_info(deviceinfo, action, dry_run, quiet_run, silent_run, flashmode, exclude, hap_setup_code, requires_upgrade, network_type, ipv4_ip, ipv4_mask, ipv4_gw, ipv4_dns)


def device_scan(hosts, action, dry_run, quiet_run, silent_run, mode, type, exclude, version, variant, hap_setup_code, network_type, ipv4_ip='', ipv4_mask='', ipv4_gw='', ipv4_dns=''):
  if hosts:
    for host in hosts:
      logger.debug(f"")
      logger.debug(f"{PURPLE}[Device Scan] manual{NC}")
      deviceinfo = Device(host)
      deviceinfo.get_device_info()
      if deviceinfo.fw_type is not None:
        device = {'host': deviceinfo.host, 'wifi_ip': deviceinfo.wifi_ip, 'fw_type': deviceinfo.fw_type, 'device_url': deviceinfo.device_url, 'info': deviceinfo.info}
        probe_device(device, action, dry_run, quiet_run, silent_run, mode, exclude, version, variant, hap_setup_code, network_type, ipv4_ip, ipv4_mask, ipv4_gw, ipv4_dns)
  else:
    logger.debug(f"{PURPLE}[Device Scan] automatic scan{NC}")
    logger.info(f"{WHITE}Scanning for Shelly devices...{NC}")
    zc = zeroconf.Zeroconf()
    listener = MyListener()
    browser = zeroconf.ServiceBrowser(zc, '_http._tcp.local.', listener)
    total_devices = 0
    while True:
      try:
        deviceinfo = listener.queue.get(timeout=20)
      except queue.Empty:
        logger.info(f"")
        if action == 'flash':
          logger.info(f"{GREEN}Devices found: {total_devices} Upgradeable: {upgradeable_devices} Flashed: {flashed_devices} Failed: {failed_flashed_devices}{NC}")
        else:
          logger.info(f"{GREEN}Devices found: {total_devices} Upgradeable: {upgradeable_devices}{NC}")
        if args.log_filename:
          logger.info(f"Log file created: {args.log_filename}")
        zc.close()
        break
      logger.debug(f"")
      logger.debug(f"{PURPLE}[Device Scan] action queue entry{NC}")
      deviceinfo.get_device_info()
      if deviceinfo.fw_type is not None and (deviceinfo.fw_type in type or type == 'all'):
        device = {'host': deviceinfo.host, 'wifi_ip': deviceinfo.wifi_ip, 'fw_type': deviceinfo.fw_type, 'device_url': deviceinfo.device_url, 'info' : deviceinfo.info}
        total_devices += 1
        probe_device(device, action, dry_run, quiet_run, silent_run, mode, exclude, version, variant, hap_setup_code, network_type, ipv4_ip, ipv4_mask, ipv4_gw, ipv4_dns)

if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Shelly HomeKit flashing script utility')
  parser.add_argument('-m', '--mode', action="store", choices=['homekit', 'keep', 'revert'], default="homekit", help="Script mode.")
  parser.add_argument('-t', '--type', action="store", choices=['homekit', 'stock', 'all'], default="all", help="Limit scan to current firmware type.")
  parser.add_argument('-a', '--all', action="store_true", dest='do_all', default=False, help="Run against all the devices on the network.")
  parser.add_argument('-q', '--quiet', action="store_true", dest='quiet_run', default=False, help="Only include upgradeable shelly devices.")
  parser.add_argument('-l', '--list', action="store_true", default=False, help="List info of shelly device.")
  parser.add_argument('-e', '--exclude', action="store", dest="exclude", nargs='*', help="Exclude hosts from found devices.")
  parser.add_argument('-n', '--assume-no', action="store_true", dest='dry_run', default=False, help="Do a dummy run through.")
  parser.add_argument('-y', '--assume-yes', action="store_true", dest='silent_run', default=False, help="Do not ask any confirmation to perform the flash.")
  parser.add_argument('-V', '--version',type=str, action="store", dest="version", default=False, help="Force a particular version.")
  parser.add_argument('--variant', action="store", dest="variant", default=False, help="Prerelease variant name.")
  parser.add_argument('-c', '--hap-setup-code', action="store", dest="hap_setup_code", default=False, help="Configure HomeKit setup code, after flashing.")
  parser.add_argument('--ip-type', action="store", choices=['dhcp', 'static'], dest="network_type", default=False, help="Configure static IP")
  parser.add_argument('--ip', action="store", dest="ipv4_ip", default=False, help="set IP address")
  parser.add_argument('--gw', action="store", dest="ipv4_gw", default=False, help="set Gateway IP address")
  parser.add_argument('--mask', action="store", dest="ipv4_mask", default=False, help="set Subnet mask address")
  parser.add_argument('--dns', action="store", dest="ipv4_dns", default=False, help="set DNS IP address")
  parser.add_argument('-v', '--verbose', action="store", dest="verbose", choices=['0', '1', '2', '3', '4', '5'], default='3', help="Enable verbose logging 0=critical, 1=error, 2=warning, 3=info, 4=debug, 5=trace.")
  parser.add_argument('--log-file', action="store", dest="log_filename", default=False, help="Create output log file with chosen filename.")
  parser.add_argument('hosts', type=str, nargs='*')
  args = parser.parse_args()
  action = 'list' if args.list else 'flash'
  args.mode = 'stock' if args.mode == 'revert' else args.mode
  args.hap_setup_code = f"{args.hap_setup_code[:3]}-{args.hap_setup_code[3:-3]}-{args.hap_setup_code[5:]}" if args.hap_setup_code and '-' not in args.hap_setup_code else args.hap_setup_code

  sh = logging.StreamHandler()
  sh.setFormatter(logging.Formatter('%(message)s'))
  sh.setLevel(log_level[args.verbose])
  if args.log_filename:
    fh = logging.FileHandler(args.log_filename, mode='w', encoding='utf-8')
    fh.setFormatter(logging.Formatter('%(asctime)s %(levelname)s %(lineno)d %(message)s'))
    fh.setLevel(log_level[args.verbose])
    logger.addHandler(fh)
  logger.addHandler(sh)

  # Windows and log file do not support acsii colours
  if not args.log_filename and not arch.startswith('Win'):
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

  homekit_release_info = None
  stock_release_info = None
  app_version = "2.2.1"

  logger.debug(f"OS: {PURPLE}{arch}{NC}")
  logger.debug(f"app_version: {app_version}")
  logger.debug(f"manual_hosts: {args.hosts} ({len(args.hosts)})")
  logger.debug(f"action: {action}")
  logger.debug(f"mode: {args.mode}")
  logger.debug(f"type: {args.type}")
  logger.debug(f"do_all: {args.do_all}")
  logger.debug(f"dry_run: {args.dry_run}")
  logger.debug(f"quiet_run: {args.quiet_run}")
  logger.debug(f"silent_run: {args.silent_run}")
  logger.debug(f"version: {args.version}")
  logger.debug(f"exclude: {args.exclude}")
  logger.debug(f"variant: {args.variant}")
  logger.debug(f"verbose: {args.verbose}")
  logger.debug(f"hap_setup_code: {args.hap_setup_code}")
  logger.debug(f"network_type: {args.network_type}")
  logger.debug(f"ipv4_ip: {args.ipv4_ip}")
  logger.debug(f"ipv4_mask: {args.ipv4_mask}")
  logger.debug(f"ipv4_gw: {args.ipv4_gw}")
  logger.debug(f"ipv4_dns: {args.ipv4_dns}")
  logger.debug(f"log_filename: {args.log_filename}")

  message = None
  if not args.hosts and not args.do_all:
    message = f"{WHITE}Requires a hostname or -a | --all{NC}"
  elif args.hosts and args.do_all:
    message = f"{WHITE}Invalid option hostname or -a | --all not both.{NC}"
  elif args.network_type:
    if args.do_all:
      message = f"{WHITE}Invalid option -a | --all can not be used with --ip-type.{NC}"
    elif len(args.hosts) > 1:
      message = f"{WHITE}Invalid option only 1 host can be used with --ip-type.{NC}"
    elif args.network_type == 'static' and (not args.ipv4_ip or not args.ipv4_mask or not args.ipv4_gw or not args.ipv4_dns):
      if not args.ipv4_dns:
        message = f"{WHITE}Invalid option --dns can not be empty.{NC}"
        logger.info(message)
      if not args.ipv4_gw:
        message = f"{WHITE}Invalid option --gw can not be empty.{NC}"
        logger.info(message)
      if not args.ipv4_mask:
        message = f"{WHITE}Invalid option --mask can not be empty.{NC}"
        logger.info(message)
      if not args.ipv4_ip:
        message = f"{WHITE}Invalid option --ip can not be empty.{NC}"
  elif args.version and len(args.version.split('.')) < 3:
    message = f"{WHITE}Incorect version formatting i.e '1.9.0'{NC}"

  if message:
    logger.info(message)
    parser.print_help()
    sys.exit(1)

  try:
    fp = requests.get("https://api.shelly.cloud/files/firmware", timeout=3)
    logger.debug(f"stock_release_info status code: {fp.status_code}")
    if fp.status_code == 200:
      stock_release_info = json.loads(fp.content)
  except requests.exceptions.RequestException as err:
    logger.critical(f"{RED}CRITICAL:{NC} {err}")
  logger.trace(f"stock_release_info: {json.dumps(stock_release_info, indent = 4)}")
  try:
    fp = requests.get("https://rojer.me/files/shelly/update.json", timeout=3)
    logger.debug(f"homekit_release_info status code: {fp.status_code}")
    if fp.status_code == 200:
      homekit_release_info = json.loads(fp.content)
  except requests.exceptions.RequestException as err:
    logger.critical(f"{RED}CRITICAL:{NC} {err}")
  logger.trace(f"homekit_release_info: {json.dumps(homekit_release_info, indent = 4)}")

  if not stock_release_info or not homekit_release_info:
    logger.error(f"{RED}Failed to lookup online version information{NC}")
    logger.error("For more information please point your web browser to:")
    logger.error("https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#script-fails-to-run")
  else:
    device_scan(args.hosts, action, args.dry_run, args.quiet_run, args.silent_run, args.mode, args.type, args.exclude, args.version, args.variant, args.hap_setup_code, args.network_type, args.ipv4_ip, args.ipv4_mask, args.ipv4_gw, args.ipv4_dns)
