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
#  usage: flash-shelly.py [-h] [-m {homekit,keep,revert}] [-t {homekit,stock,all}] [-a] [-q] [-l] [-e [EXCLUDE ...]] [-n] [-y] [-V VERSION] [--variant VARIANT] [--local-file LOCAL_FILE] [-c HAP_SETUP_CODE]
#                         [--ip-type {dhcp,static}] [--ip IPV4_IP] [--gw IPV4_GW] [--mask IPV4_MASK] [--dns IPV4_DNS] [-v {0,1,2,3,4,5}] [--log-file LOG_FILENAME]
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
#    -i {1,2,3}, --info-level {1,2,3}
#                          Control how much detail is output in the list 1=minimal, 2=basic, 3=all.
#    -ft {homekit,stock,all}, --fw-type {homekit,stock,all}
#                          Limit scan to current firmware type.
#    -mt MODEL_TYPE, --model-type MODEL_TYPE
#                          Limit scan to model type (dimmer, rgbw2, shelly1, etc).
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
#    --local-file LOCAL_FILE
#                          Use local file to flash.
#    -c HAP_SETUP_CODE, --hap-setup-code HAP_SETUP_CODE
#                          Configure HomeKit setup code, after flashing.
#    --ip-type {dhcp,static}
#                          Configure network IP type (Static or DHCP)
#    --ip IPV4_IP          set IP address
#    --gw IPV4_GW          set Gateway IP address
#    --mask IPV4_MASK      set Subnet mask address
#    --dns IPV4_DNS        set DNS IP address
#    -v {0,1,2,3,4,5}, --verbose {0,1,2,3,4,5}
#                          Enable verbose logging 0=critical, 1=error, 2=warning, 3=info, 4=debug, 5=trace.
#    --log-file LOG_FILENAME
#                          Create output log file with chosen filename.
#    --reboot              Preform a reboot of the device.

import atexit
import argparse
import datetime
import functools
import http.server
import ipaddress
import json
import logging
import os
import platform
import queue
import re
import socket
import subprocess
import sys
import threading
import time
import zipfile

class MFileHandler(logging.FileHandler):
  """Handler that controls the writing of the newline character"""
  special_code = '[!n]'
  def emit(self, record) -> None:
    if self.special_code in record.msg:
      record.msg = record.msg.replace( self.special_code, '' )
      self.terminator = ''
    else:
      self.terminator = '\n'
    return super().emit(record)

class MStreamHandler(logging.StreamHandler):
  """Handler that controls the writing of the newline character"""
  special_code = '[!n]'
  def emit(self, record) -> None:
    if self.special_code in record.msg:
      record.msg = record.msg.replace( self.special_code, '' )
      self.terminator = ''
    else:
      self.terminator = '\n'
    return super().emit(record)

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

webserver_port = 8381
http_server_started = False
server = None
thread = None
total_devices = 0
upgradeable_devices = 0
flashed_devices = 0
failed_flashed_devices = 0
arch = platform.system()
stock_info_url = "https://api.shelly.cloud/files/firmware"
stock_release_info = None
tried_to_get_remote_homekit = False
homekit_info_url = "https://rojer.me/files/shelly/update.json"
homekit_release_info = None
tried_to_get_remote_stock = False

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


class HTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def log_request(self, code):
        pass

class StoppableHTTPServer(http.server.HTTPServer):
  def run(self):
    try:
      self.serve_forever()
    except Exception:
      pass
    finally:
      self.server_close()


class MyListener:
  def __init__(self):
    self.queue = queue.Queue()

  def add_service(self, zeroconf, type, device):
    info = zeroconf.get_service_info(type, device)
    device = device.replace('._http._tcp.local.', '')
    if info:
      logger.trace(f"[Device Scan] found device: {device} added, IP address: {socket.inet_ntoa(info.addresses[0])}")
      self.queue.put(Device(device, socket.inet_ntoa(info.addresses[0])))

  def remove_service(self, *args, **kwargs):
    pass

  def update_service(self, *args, **kwargs):
    pass


class Device:
  def __init__(self, host=None, wifi_ip=None, fw_type=None, device_url=None, info=None, variant=None, version=None):
    self.host = host
    self.friendly_host = host.replace('.local','')
    self.fw_type = fw_type
    self.device_url = device_url
    self.local_file = None
    self.info = info
    self.variant = variant
    self.version = version
    self.wifi_ip = wifi_ip
    self.flash_label = "Latest:"
    self.force_flash = False

  def is_host_reachable(self, host, is_flashing=False):
    # check if host is reachable
    hostcheck = re.search("\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}", host)
    self.host = f'{host}.local' if '.local' not in host and not hostcheck else host
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(3)
    if not self.wifi_ip:
      try:
        # use manual IP supplied
        ipaddress.IPv4Address(host)
        test_host = host
        self.host = host
      except ipaddress.AddressValueError as err:
        test_host = self.host
    else:
      test_host = self.wifi_ip
    try:
      sock.connect((test_host, 80))
      logger.debug(f"Device: {test_host} is Online")
      host_is_reachable = True
    except socket.error:
      if not is_flashing:
        logger.error(f"")
        logger.error(f"{RED}Could not connect to host: {self.host}{NC}")
      host_is_reachable = False
    sock.close()
    if host_is_reachable and not self.wifi_ip:
      # resolve IP from manual hostname
      self.wifi_ip = socket.gethostbyname(test_host)
    return host_is_reachable

  def get_device_info(self, is_flashing=False):
    self.device_url = None
    info = None
    if self.is_host_reachable(self.host, is_flashing):
      try:
        homekit_fwcheck = requests.get(f'http://{self.wifi_ip}/rpc/Shelly.GetInfo', timeout=3)
        stock_fwcheck = requests.get(f'http://{self.wifi_ip}/settings', timeout=10)
        if homekit_fwcheck.status_code == 200:
          fp = homekit_fwcheck
          self.fw_type = "homekit"
          self.device_url = f'http://{self.wifi_ip}/rpc/Shelly.GetInfo'
          info = json.loads(fp.content)
        elif stock_fwcheck.status_code == 200:
          fp = stock_fwcheck
          self.fw_type = "stock"
          self.device_url = f'http://{self.wifi_ip}/settings'
          info = json.loads(fp.content)
          info['status'] = {}
          if 'settings' in self.device_url:
            fp2 = requests.get(self.device_url.replace('settings', 'status'), timeout=3)
            if fp2.status_code == 200:
              status = json.loads(fp2.content)
              info['status'] = status
      except Exception:
        pass
    logger.trace(f"Device URL: {self.device_url}")
    if not info:
      logger.debug(f"Could not get info from device: {self.host}")
    self.info = info
    return info

  def parse_stock_version(self, version):
    # stock can be '20201124-092159/v1.9.0@57ac4ad8', we need '1.9.0'
    # stock can be '20210107-122133/1.9_GU10_RGBW@07531e29', we need '1.9_GU10_RGBW'
    # stock can be '20201014-165335/1244-production-Shelly1L@6a254598', we need '0.0.0'
    # stock can be '20210226-091047/v1.10.0-rc2-89-g623b41ec0-master', we need '1.10.0-rc2'
    logger.trace(f"version: {version}")
    v = re.search("/.*?(?P<ver>[0-9]+\.[0-9]+[0-9a-z_\.]*\-?[0-9a-z]*)@?\-?(?P<build>[a-z0-9\-]*)", version, re.IGNORECASE)
    debug_info = v.groupdict() if v is not None else v
    logger.trace(f"stock version group: {debug_info}")
    parsed_version = v.group('ver') if v is not None else '0.0.0'
    parsed_build = v.group('build') if v is not None else '0'
    return parsed_version

  def set_local_file(self, local_file):
    self.local_file = local_file

  def set_force_version(self, version):
    self.flash_fw_version = version
    self.force_flash = True

  def get_current_version(self, is_flashing=False): # used when flashing between formware versions.
    info = self.get_device_info(is_flashing)
    if not info:
      return None
    if self.fw_type == 'homekit':
      version = info['version']
    elif self.fw_type == 'stock':
      version = self.parse_stock_version(info['fw'])
    return version

  def get_uptime(self, is_flashing=False):
    info = self.get_device_info(is_flashing)
    if not info:
      logger.trace(f'get_uptime: -1')
      return -1
    if self.fw_type == 'homekit':
      uptime = info.get('uptime', -1)
    elif self.fw_type == 'stock':
      uptime = info.get('status', {}).get('uptime', -1)
    logger.trace(f'get_uptime: {uptime}')
    return uptime

  def shelly_model(self, type):
    options = {'SHPLG-1' : ['ShellyPlug', 'shelly-plug'],
               'SHPLG-S' : ['ShellyPlugS', 'shelly-plug-s'],
               'SHPLG-U1' : ['ShellyPlugUS', 'shelly-plug-u1'],
               'SHPLG2-1' : ['ShellyPlug', 'shelly-plug2'],
               'SHSW-1' : ['Shelly1', 'switch1'],
               'SHSW-PM' : ['Shelly1PM', 'switch1pm'],
               'SHSW-L' : ['Shelly1L', 'switch1l'],
               'SHSW-21' : ['Shelly2', 'switch'],
               'SHSW-25' : ['Shelly25', 'switch25'],
               'SHAIR-1' : ['ShellyAir', 'air'],
               'SHSW-44' : ['Shelly4Pro', 'dinrelay4'],
               'SHUNI-1' : ['ShellyUni', 'uni'],
               'SHEM' : ['ShellyEM', 'shellyem'],
               'SHEM-3' : ['Shelly3EM','shellyem3'],
               'SHSEN-1' : ['ShellySensor', 'smart-sensor'],
               'SHGS-1' : ['ShellyGas', 'gas-sensor'],
               'SHSM-01' : ['ShellySmoke', 'smoke-sensor'],
               'SHHT-1' : ['ShellyHT', 'ht-sensor'],
               'SHWT-1' : ['ShellyFlood', 'water-sensor'],
               'SHDW-1' : ['ShellyDoorWindow', 'doorwindow-sensor'],
               'SHDW-2' : ['ShellyDoorWindow2', 'doorwindow-sensor2'],
               'SHSPOT-1' : ['ShellySpot', 'spot'],
               'SHCL-255' : ['ShellyColor', 'color'],
               'SHBLB-1' : ['ShellyBulb', 'bulb'],
               'SHCB-1' : ['ShellyColorBulb', 'color-bulb'],
               'SHRGBW2' : ['ShellyRGBW2', 'rgbw2'],
               'SHRGBWW-01' : ['ShellyRGBWW', 'rgbww'],
               'SH2LED-1' : ['ShellyLED','led1'],
               'SHDM-1' : ['ShellyDimmer', 'dimmer'],
               'SHDM-2' : ['ShellyDimmer2', 'dimmer-l51'],
               'SHDIMW-1' : ['ShellyDimmerW','dimmerw1'],
               'SHVIN-1' : ['ShellyVintage', 'bulb6w'],
               'SHBDUO-1' : ['ShellyDuo', 'bulbduo'],
               'SHBTN-1' : ['ShellyButton1', 'wifi-button'],
               'SHBTN-2' : ['ShellyButton2', 'wifi-button2'],
               'SHIX3-1' : ['ShellyI3', 'ix3'],
    }
    return options.get(type, type)

  def parse_local_file(self):
    self.flash_label = "Local:"
    if os.path.exists(self.local_file) and self.local_file.endswith('.zip'):
      local_host = socket.gethostname()
      s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
      s.connect((self.wifi_ip, 80))
      local_ip = s.getsockname()[0]
      logger.debug(f"Host IP: {local_ip}")
      s.close()
      with zipfile.ZipFile(self.local_file, "r") as zfile:
        for name in zfile.namelist():
          if name.endswith('manifest.json'):
            logger.debug(f"zipfile: {name}")
            mfile = zfile.read(name)
            manifest_file = json.loads(mfile)
            logger.debug(f"manifest: {json.dumps(manifest_file, indent = 4)}")
            break
      manifest_version = manifest_file['version']
      manifest_name = manifest_file['name']
      if manifest_version == '1.0':
        self.set_force_version(self.parse_stock_version(manifest_file['build_id']))
        self.flash_fw_type_str = "Stock"
      else:
        self.set_force_version(manifest_version)
        self.flash_fw_type_str = "HomeKit"
      if self.fw_type == 'homekit':
        self.dlurl = 'local'
      else:
        self.dlurl = f'http://{local_ip}:{webserver_port}/{self.local_file}'
      if self.fw_type == 'stock' and self.stock_model == 'SHRGBW2' and self.color_mode is not None:
        m_model = f"{self.app}-{self.color_mode}"
      else:
        m_model = self.app
      if m_model != manifest_name:
        self.flash_fw_version = '0.0.0'
        self.dlurl = None
      return True
    else:
      logger.debug(f"File does not exist")
      self.flash_fw_version = '0.0.0'
      self.dlurl = None
      return False

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
    if not self.version:
      stock_model_info = release_info['data'][self.stock_model] if self.stock_model in release_info['data'] else None
      if self.variant == 'beta':
        self.flash_fw_version = self.parse_stock_version(stock_model_info['beta_ver']) if 'beta_ver' in stock_model_info else self.parse_stock_version(stock_model_info['version'])
        self.dlurl = stock_model_info['beta_url'] if 'beta_ver' in stock_model_info else stock_model_info['url']
      else:
        try:
          self.flash_fw_version = self.parse_stock_version(stock_model_info['version']) if 'version' in stock_model_info else self.parse_stock_version(stock_model_info['beta_url'])
          self.dlurl = stock_model_info['url']
        except Exception:
          self.flash_fw_version = '0.0.0'
          self.dlurl = None
    else:
      self.flash_label = "Manual:"
      self.flash_fw_version = self.version
      self.dlurl = f'http://archive.shelly-tools.de/version/v{self.version}/{self.stock_model}.zip'
    if self.stock_model  == 'SHRGBW2':
      if self.dlurl and not self.version and self.color_mode and not self.local_file:
        self.dlurl = self.dlurl.replace('.zip',f'-{self.color_mode}.zip')

class HomeKitDevice(Device):
  def get_info(self):
    if not self.info:
      return False
    self.fw_type_str = 'HomeKit'
    self.fw_version = self.info['version']
    self.model = self.info['model'] if 'model' in self.info else self.shelly_model(self.info['app'])[0]
    self.stock_model = self.info['stock_model'] if 'stock_model' in self.info else None
    self.app = self.info['app'] if 'app' in self.info else self.shelly_model(self.stock_model)[1]
    self.device_id = self.info['device_id'] if 'device_id' in self.info else None
    self.device_name = self.info['name'] if 'name' in self.info else None
    self.color_mode = self.info['color_mode'] if 'color_mode' in self.info else None
    return True

  def update_to_homekit(self, release_info=None):
    logger.debug("Mode: HomeKit To HomeKit")
    self.update_homekit(release_info)

  def update_to_stock(self, release_info=None):
    logger.debug("Mode: HomeKit To Stock")
    self.update_stock(release_info)

  def flash_firmware(self):
    if self.local_file:
      logger.debug(f"Local file: {self.local_file}")
      myfile = open(self.local_file,'rb')
      logger.info("Now Flashing...")
      files = {'file': ('shelly-flash.zip', myfile)}
    else:
      logger.info("Downloading Firmware...")
      logger.debug(f"Remote Download URL: {self.dlurl}")
      myfile = requests.get(self.dlurl)
      logger.info("Now Flashing...")
      files = {'file': ('shelly-flash.zip', myfile.content)}
    logger.debug(f"requests.post('http://{self.wifi_ip}/update', files=files")
    response = requests.post(f'http://{self.wifi_ip}/update', files=files)
    logger.debug(response.text)

  def preform_reboot(self):
    logger.info(f"Rebooting...")
    logger.debug(f"requests.post(url={f'http://{self.wifi_ip}/rpc/SyS.Reboot'}")
    response = requests.get(url=f'http://{self.wifi_ip}/rpc/SyS.Reboot')
    logger.trace(response.text)

class StockDevice(Device):
  def get_info(self):
    if not self.info:
      return False
    self.fw_type_str = 'Stock'
    self.fw_version = self.parse_stock_version(self.info['fw'])  # current firmware version
    self.model = self.shelly_model(self.info['device']['type'])[0]
    self.stock_model = self.info['device']['type']
    self.app = self.shelly_model(self.stock_model)[1]
    self.device_id = self.info['mqtt']['id'] if 'id' in self.info['mqtt'] else self.friendly_host
    self.device_name = self.info['name'] if 'name' in self.info else None
    self.color_mode = self.info['mode'] if 'mode' in self.info else None
    return True

  def update_to_homekit(self, release_info=None):
    logger.debug("Mode: Stock To HomeKit")
    self.update_homekit(release_info)

  def update_to_stock(self, release_info=None):
    logger.debug("Mode: Stock To Stock")
    self.update_stock(release_info)

  def flash_firmware(self):
    logger.info("Now Flashing...")
    dlurl = self.dlurl.replace('https', 'http')
    if self.local_file:
      logger.debug(f"Local file: {self.local_file}")
      logger.debug(f"Local Download URL: {dlurl}")
    else:
      logger.debug(f"Remote Download URL: {dlurl}")
    if self.fw_version == '0.0.0':
      logger.debug(f"http://{self.wifi_ip}/ota?update=true")
      response = requests.get(f'http://{self.wifi_ip}/ota?update=true')
    else:
      logger.debug(f"http://{self.wifi_ip}/ota?url={dlurl}")
      try:
        response = requests.get(f'http://{self.wifi_ip}/ota?url={dlurl}')
      except Exception:
        logger.info(f"flash failed")
    logger.trace(response.text)

  def preform_reboot(self):
    logger.info(f"Rebooting...")
    logger.debug(f"requests.post(url={f'http://{self.wifi_ip}/reboot'}")
    response = requests.get(url=f'http://{self.wifi_ip}/reboot')
    logger.trace(response.text)


class Main():
  def __init__(self, hosts, action, log_filename, dry_run, quiet_run, silent_run, mode, flashmode, info_level, fw_type, model_type, exclude, version, variant,
               hap_setup_code, local_file, network_type, ipv4_ip, ipv4_mask, ipv4_gw, ipv4_dns):
    self.hosts = hosts
    self.action = action
    self.log_filename = log_filename
    self.dry_run = dry_run
    self.quiet_run = quiet_run
    self.silent_run = silent_run
    self.mode = mode
    self.flashmode = flashmode
    self.info_level = info_level
    self.fw_type = fw_type
    self.model_type = model_type
    self.exclude = exclude
    self.version = version
    self.variant = variant
    self.hap_setup_code = hap_setup_code
    self.local_file = local_file
    self.network_type = network_type
    self.ipv4_ip = ipv4_ip
    self.ipv4_mask = ipv4_mask
    self.ipv4_gw = ipv4_gw
    self.ipv4_dns = ipv4_dns

  def get_release_info(self, info_type):
    release_info = False
    release_info_url = homekit_info_url if info_type == 'homekit' else stock_info_url
    try:
      fp = requests.get(release_info_url, timeout=3)
      logger.debug(f"{info_type} release_info status code: {fp.status_code}")
      if fp.status_code == 200:
        release_info = json.loads(fp.content)
    except requests.exceptions.RequestException as err:
      logger.critical(f"{RED}CRITICAL:{NC} {err}")
    logger.trace(f"{info_type} release_info: {json.dumps(release_info, indent = 4)}")
    if not release_info:
      logger.error("")
      logger.error(f"{RED}Failed to lookup online stock firmware information{NC}")
      logger.error("For more information please point your web browser to:")
      logger.error("https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#script-fails-to-run")
    return release_info

  def parse_version(self, vs):
    # 1.9.2_1L
    # 1.9.3-rc3 / 2.7.0-beta1 / 2.7.0-latest / 1.9.5-DM2_autocheck
    # 1.9_GU10_RGBW
    logger.trace(f"vs: {vs}")
    try:
      v = re.search("^(?P<major>\d+).(?P<minor>\d+)(?:.(?P<patch>\d+))?(?:_(?P<model>[a-zA-Z0-9_]*))?(?:-(?P<prerelease>[a-zA-Z_]*)?(?P<prerelease_seq>\d*))?$", vs)
      debug_info = v.groupdict() if v is not None else v
      logger.trace(f"group: {debug_info}")
      major = int(v.group('major'))
      minor = int(v.group('minor'))
      patch = int(v.group('patch')) if v.group('patch') is not None else 0
      variant = v.group('prerelease')
      varSeq = int(v.group('prerelease_seq')) if v.group('prerelease_seq') and v.group('prerelease_seq').isdigit() else 0
    except Exception:
      major = 0
      minor = 0
      patch = 0
      variant = ''
      varSeq = 0

    return (major, minor, patch, variant, varSeq)

  def is_newer(self, v1, v2):
    vi1 = self.parse_version(v1)
    vi2 = self.parse_version(v2)
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

  def write_network_type(self, device_info):
    logger.debug(f"{PURPLE}[Write Network Type]{NC}")
    wifi_ip = device_info.wifi_ip
    if device_info.fw_type == 'homekit':
      if self.network_type == 'static':
        message = f"Configuring static IP to {self.ipv4_ip}..."
        value={'config': {'wifi': {'sta': {'ip': self.ipv4_ip, 'netmask': self.ipv4_mask, 'gw': self.ipv4_gw, 'nameserver': self.ipv4_dns}}}}
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
      if self.network_type == 'static':
        message = f"Configuring static IP to {self.ipv4_ip}..."
        config_set_url = f'http://{wifi_ip}/settings/sta?ipv4_method=static&ip={self.ipv4_ip}&netmask={ipv4_mask}&gateway={ipv4_gw}&dns={ipv4_dns}'
      else:
        message = f"Configuring IP to use DHCP..."
        config_set_url = f'http://{wifi_ip}/settings/sta?ipv4_method=dhcp'
      logger.info(message)
      logger.debug(f"requests.post(url={config_set_url}")
      response = requests.post(url=config_set_url)
      logger.trace(response.text)
      logger.info(f"Saved...")

  def write_hap_setup_code(self, wifi_ip):
    logger.info("Configuring HomeKit setup code...")
    value={'code': self.hap_setup_code}
    logger.debug(f"requests.post(url='http://{wifi_ip}/rpc/HAP.Setup', json={value}")
    response = requests.post(url=f'http://{wifi_ip}/rpc/HAP.Setup', json={'code': self.hap_setup_code})
    logger.trace(response.text)
    if response.text.startswith('null'):
      logger.info(f"Done.")

  def wait_for_reboot(self, device_info, preboot_uptime=-1, reboot_only=False):
    logger.debug(f"{PURPLE}[Wait For Reboot]{NC}")
    logger.info(f"waiting for {device_info.friendly_host} to reboot[!n]")
    logger.trace("")
    get_current_version = None
    time.sleep(1) # wait for time check to fall behind
    current_uptime = device_info.get_uptime(True)
    n = 1
    if reboot_only == False:
      while current_uptime > preboot_uptime and n < 60 or get_current_version == None:
        logger.info(f".[!n]")
        if n == 20:
          logger.info("")
          logger.info(f"still waiting for {device_info.friendly_host} to reboot[!n]")
        elif n == 40:
          logger.info("")
          logger.info(f"we'll wait just a little longer for {device_info.friendly_host} to reboot[!n]")
        time.sleep(1) # wait 1 second before retrying.
        current_uptime = device_info.get_uptime(True)
        get_current_version = device_info.get_current_version(is_flashing=True)
        logger.debug("")
        logger.debug(f"get_current_version: {get_current_version}")
        n += 1
        logger.trace(f"loop number: {n}")
    else:
      while device_info.get_uptime(True) < 3:
        time.sleep(1) # wait 1 second before retrying.
    logger.info("")
    return get_current_version

  def write_flash(self, device_info):
    logger.debug(f"{PURPLE}[Write Flash]{NC}")
    flashed = False
    uptime = device_info.get_uptime(True)
    device_info.flash_firmware()
    reboot_check = self.parse_version(self.wait_for_reboot(device_info, uptime))
    flashfw = self.parse_version(device_info.flash_fw_version)
    if reboot_check == flashfw:
      global flashed_devices
      flashed_devices +=1
      logger.critical(f"{GREEN}Successfully flashed {device_info.friendly_host} to {device_info.flash_fw_version}{NC}")
    else:
      if device_info.stock_model == 'SHRGBW2':
        logger.info("")
        logger.info("To finalise flash process you will need to switch 'Modes' in the device WebUI,")
        logger.info(f"{WHITE}WARNING!!{NC} If you are using this device in conjunction with Homebridge")
        logger.info(f"{WHITE}STOP!!{NC} homebridge before performing next steps.")
        logger.info(f"Goto http://{device_info.host} in your web browser")
        logger.info("Goto settings section")
        logger.info("Goto 'Device Type' and switch modes")
        logger.info("Once mode has been changed, you can switch it back to your preferred mode")
        logger.info(f"Restart homebridge.")
      elif reboot_check == '0.0.0':
        logger.info(f"{RED}Flash may have failed, please manually check version{NC}")
      else:
        global failed_flashed_devices
        failed_flashed_devices +=1
        logger.info(f"{RED}Failed to flash {device_info.friendly_host} to {device_info.flash_fw_version}{NC}")
      logger.debug(f"Current: {reboot_check}")
      logger.debug(f"flash_fw_version: {flashfw}")

  def reboot_device(self, device_info):
    logger.debug(f"{PURPLE}[Reboot Device]{NC}")
    device_info.preform_reboot()
    self.wait_for_reboot(device_info, reboot_only=True)
    logger.info(f"Device has rebooted...")

  def parse_info(self, device_info, requires_upgrade):
    logger.debug(f"")
    logger.debug(f"{PURPLE}[Parse Info]{NC}")
    logger.trace(f"device_info: {device_info}")

    global total_devices
    total_devices += 1

    perform_flash = False
    flash = False
    durl_request = False
    host = device_info.host
    friendly_host = device_info.friendly_host
    device_name = device_info.device_name
    device_id = device_info.device_id
    model = device_info.model
    stock_model = device_info.stock_model
    color_mode = device_info.color_mode
    current_fw_version = device_info.fw_version
    current_fw_type = device_info.fw_type
    current_fw_type_str = device_info.fw_type_str
    flash_fw_version = device_info.flash_fw_version
    flash_fw_type_str = device_info.flash_fw_type_str
    force_version = device_info.version
    force_flash = device_info.force_flash
    dlurl = device_info.dlurl
    flash_label = device_info.flash_label
    wifi_ip = device_info.wifi_ip
    wifi_ssid = device_info.info.get('wifi_ssid', None) if device_info.fw_type_str == 'HomeKit' else device_info.info.get('status', {}).get('wifi_sta', {}).get('ssid', None)
    wifi_rssi = device_info.info.get('wifi_rssi', None) if device_info.fw_type_str == 'HomeKit' else device_info.info.get('status', {}).get('wifi_sta', {}).get('rssi', None)
    sys_temp = device_info.info.get('sys_temp', None)
    uptime = datetime.timedelta(seconds=device_info.info.get('uptime', 0)) if current_fw_type_str == 'HomeKit' else datetime.timedelta(seconds=device_info.info.get('status', {}).get('uptime',0))
    hap_ip_conns_pending = device_info.info.get('hap_ip_conns_pending', None)
    hap_ip_conns_active = device_info.info.get('hap_ip_conns_active', None)
    hap_ip_conns_max = device_info.info.get('hap_ip_conns_max', None)

    logger.debug(f"flash mode: {self.flashmode}")
    logger.debug(f"requires_upgrade: {requires_upgrade}")
    logger.debug(f"host: {host}")
    logger.debug(f"device_name: {device_name}")
    logger.debug(f"device_id: {device_id}")
    logger.debug(f"model: {model}")
    logger.debug(f"stock_model: {stock_model}")
    logger.debug(f"color_mode: {color_mode}")
    logger.debug(f"current_fw_version: {current_fw_type_str} {current_fw_version}")
    logger.debug(f"flash_fw_version: {flash_fw_type_str} {flash_fw_version}")
    logger.debug(f"force_flash: {force_flash}")
    logger.debug(f"force_version: {force_version}")
    logger.debug(f"dlurl: {dlurl}")
    logger.debug(f"wifi_ip: {wifi_ip}")
    logger.debug(f"wifi_ssid: {wifi_ssid}")
    logger.debug(f"wifi_rssi: {wifi_rssi}")
    logger.debug(f"sys_temp: {sys_temp}")
    logger.debug(f"uptime: {uptime}")
    logger.debug(f"hap_ip_conns_pending: {hap_ip_conns_pending}")
    logger.debug(f"hap_ip_conns_active: {hap_ip_conns_active}")
    logger.debug(f"hap_ip_conns_max: {hap_ip_conns_max}")

    if force_version:
      device_info.set_force_version(force_version)
      flash_fw_version = force_version

    if dlurl and dlurl != 'local':
      durl_request = requests.head(dlurl)
      logger.debug(f"durl_request: {durl_request}")
    if flash_fw_version == 'novariant':
      flash_fw_type_str = f"{RED}{flash_fw_type_str}{NC}"
      latest_fw_label = f"{RED}No {device_info.variant} available{NC}"
      flash_fw_version = '0.0.0'
      dlurl = None
    elif not dlurl and device_info.local_file:
      flash_fw_type_str = f"{RED}{flash_fw_type_str}{NC}"
      latest_fw_label = f"{RED}Invailid file{NC}"
      flash_fw_version = '0.0.0'
      dlurl = None
    elif not dlurl or (durl_request is not False and (durl_request.status_code != 200 or durl_request.headers.get('Content-Type', '') != 'application/zip')):
      flash_fw_type_str = f"{RED}{flash_fw_type_str}{NC}"
      latest_fw_label = f"{RED}Not available{NC}"
      flash_fw_version = '0.0.0'
      dlurl = None
    else:
      latest_fw_label = flash_fw_version

    flashfw_newer = self.is_newer(flash_fw_version, current_fw_version)
    if (not self.quiet_run or (self.quiet_run and (flashfw_newer or (force_flash and flash_fw_version != '0.0.0') or (force_version and dlurl and flash_fw_version != current_fw_version)))) and requires_upgrade != 'Done':
      logger.info(f"")
      logger.info(f"{WHITE}Host: {NC}http://{host}")
      if int(self.info_level) > 1 or device_name != friendly_host:
        logger.info(f"{WHITE}Device Name: {NC}{device_name}")
      if int(self.info_level) > 1:
        logger.info(f"{WHITE}Model: {NC}{model}")
      if int(self.info_level) >= 3:
        logger.info(f"{WHITE}Device ID: {NC}{device_id}")
        logger.info(f"{WHITE}SSID: {NC}{wifi_ssid}")
        logger.info(f"{WHITE}IP: {NC}{wifi_ip}")
        logger.info(f"{WHITE}RSSI: {NC}{wifi_rssi}")
        if sys_temp is not None:
          logger.info(f"{WHITE}Sys Temp: {NC}{sys_temp}˚c{NC}")
        if str(uptime) != '0:00:00':
          logger.info(f"{WHITE}Up Time: {NC}{uptime}{NC}")
        if hap_ip_conns_max is not None:
          if int(hap_ip_conns_pending) > 0:
            hap_ip_conns_pending = f"{RED}{hap_ip_conns_pending}{NC}"
          logger.info(f"{WHITE}HAP Connections: {NC}{hap_ip_conns_pending} / {hap_ip_conns_active} / {hap_ip_conns_max}{NC}")
      if current_fw_type == self.flashmode and (current_fw_version == flash_fw_version or flash_fw_version == '0.0.0'):
        logger.info(f"{WHITE}Firmware: {NC}{current_fw_type_str} {current_fw_version} {GREEN}\u2714{NC}")
      elif current_fw_type == self.flashmode and flashfw_newer:
        logger.info(f"{WHITE}Firmware: {NC}{current_fw_type_str} {current_fw_version} \u279c {YELLOW}{latest_fw_label}{NC}")
      elif current_fw_type != self.flashmode and current_fw_version != flash_fw_version:
        logger.info(f"{WHITE}Firmware: {NC}{current_fw_type_str} {current_fw_version} \u279c {YELLOW}{flash_fw_type_str} {latest_fw_label}{NC}")
      elif current_fw_type == self.flashmode and current_fw_version != flash_fw_version:
        logger.info(f"{WHITE}Firmware: {NC}{current_fw_type_str} {current_fw_version}")
      else:
        print("I DON'T THINK THIS IS NEEDED")
        logger.info(f"{WHITE}Firmware: {NC}{current_fw_type_str} {current_fw_version} {flash_fw_type_str} {latest_fw_label}{NC}")

    if dlurl and ((force_version and flash_fw_version != current_fw_version) or force_flash or requires_upgrade == True or (current_fw_type != self.flashmode) or (current_fw_type == self.flashmode and flashfw_newer)):
      global upgradeable_devices
      upgradeable_devices += 1

    if self.action == 'flash':
      message = "Would have been"
      keyword = None
      if dlurl:
        if self.exclude and friendly_host in self.exclude:
          logger.info("Skipping as device has been excluded...")
          logger.info("")
          return 0
        elif requires_upgrade == True:
          perform_flash = True
          if self.flashmode == 'stock':
            keyword = f"upgraded to version {flash_fw_version}"
          elif self.flashmode == 'homekit':
            message = "This device needs to be"
            keyword = "upgraded to latest stock firmware version, before you can flash to HomeKit"
        elif force_flash or (force_version and flash_fw_version != current_fw_version):
          perform_flash = True
          keyword = f"flashed to {flash_fw_type_str} version {flash_fw_version}"
        elif current_fw_type != self.flashmode:
          perform_flash = True
          keyword = f"converted to {flash_fw_type_str} firmware"
        elif current_fw_type == self.flashmode and flashfw_newer:
          perform_flash = True
          keyword = f"upgraded from {current_fw_version} to version {flash_fw_version}"
      elif not dlurl:
        if force_version:
          keyword = f"Version {force_version} is not available..."
        elif device_info.local_file:
          keyword = "Incorrect Zip File for device..."
        if keyword is not None and not self.quiet_run:
          logger.info(f"{keyword}")
        return 0

      logger.debug(f"perform_flash: {perform_flash}")
      if perform_flash == True and self.dry_run == False and self.silent_run == False:
        if requires_upgrade == True:
          flash_message = f"{message} {keyword}"
        elif requires_upgrade == 'Done':
          flash_message = f"Do you wish to contintue to flash {friendly_host} to HomeKit firmware version {flash_fw_version}"
        else:
          flash_message = f"Do you wish to flash {friendly_host} to {flash_fw_type_str} firmware version {flash_fw_version}"
        if input(f"{flash_message} (y/n) ? ") in ('y', 'Y'):
          flash = True
        else:
          flash = False
          logger.info("Skipping Flash...")
      elif perform_flash == True and self.dry_run == False and self.silent_run == True:
        flash = True
      elif perform_flash == True and self.dry_run == True:
        logger.info(f"{message} {keyword}...")
      if flash == True:
        self.write_flash(device_info)
      if device_info.fw_type == 'homekit' and self.hap_setup_code:
        self.write_hap_setup_code(device_info.wifi_ip)
      if self.network_type:
        if self.network_type == 'static':
          message = f"Do you wish to set your IP address to {self.ipv4_ip}"
        else:
          message = f"Do you wish to set your IP address to use DHCP"
        if input(f"{message} (y/n) ? ") in ('y', 'Y'):
          set_ip = True
        else:
          set_ip = False
        if set_ip or self.silent_run:
          self.write_network_type(device_info)
    elif self.action == 'reboot':
      reboot = False
      if self.dry_run == False and self.silent_run == False:
        if input(f"Do you wish to reboot {friendly_host} (y/n) ? ") in ('y', 'Y'):
          reboot = True
      elif self.silent_run == True:
        reboot = True
      elif self.dry_run == True:
        logger.info(f"Would have been rebooted...")
      if reboot == True:
        self.reboot_device(device_info)

  def probe_device(self, device):
    logger.debug("")
    logger.debug(f"{PURPLE}[Probe Device]{NC}")
    logger.trace(f"device_info: {json.dumps(device, indent = 4)}")

    global stock_release_info, homekit_release_info, tried_to_get_remote_homekit, tried_to_get_remote_stock, http_server_started, webserver_port, server, thread
    requires_upgrade = False
    got_info = False

    if self.mode == 'keep':
      self.flashmode = device['fw_type']
    else:
      self.flashmode = self.mode
    if device['fw_type'] == 'homekit':
      deviceinfo = HomeKitDevice(device['host'], device['wifi_ip'], device['fw_type'], device['device_url'], device['info'], self.variant, self.version)
    elif device['fw_type'] == 'stock':
      deviceinfo = StockDevice(device['host'], device['wifi_ip'], device['fw_type'], device['device_url'], device['info'], self.variant, self.version)
    if not deviceinfo.get_info():
      logger.warning("")
      logger.warning(f"{RED}Failed to lookup local information of {device['host']}{NC}")
    else:
      if self.local_file:
        deviceinfo.set_local_file(self.local_file)
      if deviceinfo.fw_type == 'stock' and self.local_file:
        loop = 1
        while not http_server_started:
          try:
            logger.info(f"{WHITE}Starting local webserver on port {webserver_port}{NC}")
            if server is None:
              server = StoppableHTTPServer(("", webserver_port), HTTPRequestHandler)
            http_server_started = True
          except OSError as err:
            logger.critical(f"{WHITE}Failed to start server {err} port {webserver_port}{NC}")
            webserver_port += 1
            loop += 1
            if loop == 10:
              sys.exit(1)
        if thread is None:
          thread = threading.Thread(None, server.run)
          thread.start()
      if self.local_file and deviceinfo.parse_local_file():
        if deviceinfo.fw_type == 'stock':
          if deviceinfo.flash_fw_type_str == 'HomeKit':
            if not stock_release_info and not tried_to_get_remote_stock:
              stock_release_info = self.get_release_info('stock')
              tried_to_get_remote_stock = True
            if stock_release_info:
              deviceinfo.update_to_stock(stock_release_info)
              if (deviceinfo.fw_version == '0.0.0' or self.is_newer(deviceinfo.flash_fw_version, deviceinfo.fw_version)):
                requires_upgrade = True
                got_info = True
              else:
                deviceinfo.parse_local_file()
                got_info = True
          elif deviceinfo.flash_fw_type_str == 'Stock':
            got_info = True
        else:
          got_info = True
      else:
        if deviceinfo.fw_type == 'stock' and self.flashmode == 'homekit':
          if not stock_release_info and not tried_to_get_remote_stock:
            stock_release_info = self.get_release_info('stock')
            tried_to_get_remote_stock = True
          if not homekit_release_info and not tried_to_get_remote_homekit and not self.local_file:
            homekit_release_info = self.get_release_info('homekit')
            tried_to_get_remote_homekit = True
          if stock_release_info and homekit_release_info:
            deviceinfo.update_to_stock(stock_release_info)
            if (deviceinfo.fw_version == '0.0.0' or self.is_newer(deviceinfo.flash_fw_version, deviceinfo.fw_version)):
              requires_upgrade = True
              got_info = True
            else:
              deviceinfo.update_to_homekit(homekit_release_info)
              got_info = True
        elif self.flashmode == 'homekit':
          if not homekit_release_info and not tried_to_get_remote_homekit and not self.local_file:
            homekit_release_info = self.get_release_info('homekit')
            tried_to_get_remote_homekit = True
          if homekit_release_info:
            deviceinfo.update_to_homekit(homekit_release_info)
            got_info = True
        elif self.flashmode == 'stock':
          if not stock_release_info and not tried_to_get_remote_stock:
            stock_release_info = self.get_release_info('stock')
            tried_to_get_remote_stock = True
          if stock_release_info:
            deviceinfo.update_to_stock(stock_release_info)
            got_info = True
      if got_info and deviceinfo.fw_type == "homekit" and float(f"{self.parse_version(deviceinfo.info['version'])[0]}.{self.parse_version(deviceinfo.info['version'])[1]}") < 2.1:
        logger.error(f"{WHITE}Host: {NC}{deviceinfo.host}")
        logger.error(f"Version {deviceinfo.info['version']} is to old for this script,")
        logger.error(f"please update via the device webUI.")
        logger.error("")
      elif got_info:
        self.parse_info(deviceinfo, requires_upgrade)
        if requires_upgrade:
          time.sleep(10) # need to allow time for previous flash reboot to fully boot.
          requires_upgrade = 'Done'
          deviceinfo.get_info()
          if deviceinfo.flash_fw_version != '0.0.0' and not self.is_newer(deviceinfo.flash_fw_version, deviceinfo.fw_version):
            if self.local_file:
              deviceinfo.parse_local_file()
            else:
              deviceinfo.update_to_homekit(homekit_release_info)
            self.parse_info(deviceinfo, requires_upgrade)

  def stop_scan(self):
    while True:
      try:
        self.listener.queue.get_nowait()
      except queue.Empty:
        self.zc.close()
        break

  def results(self):
    logger.info(f"")
    if self.action == 'flash':
      logger.info(f"{GREEN}Devices found: {total_devices} Upgradeable: {upgradeable_devices} Flashed: {flashed_devices} Failed: {failed_flashed_devices}{NC}")
    else:
      logger.info(f"{GREEN}Devices found: {total_devices} Upgradeable: {upgradeable_devices}{NC}")
    if self.log_filename:
      logger.info(f"Log file created: {args.log_filename}")

  def device_scan(self):
    global total_devices
    if self.hosts:
      for host in self.hosts:
        logger.debug(f"")
        logger.debug(f"{PURPLE}[Device Scan] manual{NC}")
        deviceinfo = Device(host)
        deviceinfo.get_device_info()
        if deviceinfo.fw_type is not None:
          device = {'host': deviceinfo.host, 'wifi_ip': deviceinfo.wifi_ip, 'fw_type': deviceinfo.fw_type, 'device_url': deviceinfo.device_url, 'info': deviceinfo.info}
          self.probe_device(device)
    else:
      logger.debug(f"{PURPLE}[Device Scan] automatic scan{NC}")
      logger.info(f"{WHITE}Scanning for Shelly devices...{NC}")
      self.zc = zeroconf.Zeroconf()
      self.listener = MyListener()
      browser = zeroconf.ServiceBrowser(self.zc, '_http._tcp.local.', self.listener)
      total_devices = 0
      while True:
        try:
          deviceinfo = self.listener.queue.get(timeout=20)
        except queue.Empty:
          break
        logger.debug(f"")
        logger.debug(f"{PURPLE}[Device Scan] action queue entry{NC}")
        deviceinfo.get_device_info()
        if deviceinfo.fw_type is not None:
          fw_model = deviceinfo.info.get('model') if 'homekit' == deviceinfo.fw_type else deviceinfo.shelly_model(deviceinfo.info.get('device').get('type'))[0]
          if (deviceinfo.fw_type in self.fw_type or self.fw_type == 'all') and (self.model_type is not None and self.model_type.lower() in fw_model.lower() or self.model_type == 'all'):
            device = {'host': deviceinfo.host, 'wifi_ip': deviceinfo.wifi_ip, 'fw_type': deviceinfo.fw_type, 'device_url': deviceinfo.device_url, 'info' : deviceinfo.info}
            self.probe_device(device)

if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Shelly HomeKit flashing script utility')
  parser.add_argument('-m', '--mode', action="store", choices=['homekit', 'keep', 'revert'], default="homekit", help="Script mode.")
  parser.add_argument('-i', '--info-level', action="store", dest='info_level', choices=['1', '2', '3'], default="2", help="Control how much detail is output in the list 1=minimal, 2=basic, 3=all.")
  parser.add_argument('-ft', '--fw-type', action="store", dest='fw_type', choices=['homekit', 'stock', 'all'], default="all", help="Limit scan to current firmware type.")
  parser.add_argument('-mt', '--model-type', action="store", dest='model_type', default='all', help="Limit scan to model type (dimmer, rgbw2, shelly1, etc).")
  parser.add_argument('-a', '--all', action="store_true", dest='do_all', default=False, help="Run against all the devices on the network.")
  parser.add_argument('-q', '--quiet', action="store_true", dest='quiet_run', default=False, help="Only include upgradeable shelly devices.")
  parser.add_argument('-l', '--list', action="store_true", default=False, help="List info of shelly device.")
  parser.add_argument('-e', '--exclude', action="store", dest="exclude", nargs='*', help="Exclude hosts from found devices.")
  parser.add_argument('-n', '--assume-no', action="store_true", dest='dry_run', default=False, help="Do a dummy run through.")
  parser.add_argument('-y', '--assume-yes', action="store_true", dest='silent_run', default=False, help="Do not ask any confirmation to perform the flash.")
  parser.add_argument('-V', '--version',type=str, action="store", dest="version", default=False, help="Force a particular version.")
  parser.add_argument('--variant', action="store", dest="variant", default=False, help="Prerelease variant name.")
  parser.add_argument('--local-file', action="store", dest="local_file", default=False, help="Use local file to flash.")
  parser.add_argument('-c', '--hap-setup-code', action="store", dest="hap_setup_code", default=False, help="Configure HomeKit setup code, after flashing.")
  parser.add_argument('--ip-type', action="store", choices=['dhcp', 'static'], dest="network_type", default=False, help="Configure network IP type (Static or DHCP)")
  parser.add_argument('--ip', action="store", dest="ipv4_ip", default=False, help="set IP address")
  parser.add_argument('--gw', action="store", dest="ipv4_gw", default=False, help="set Gateway IP address")
  parser.add_argument('--mask', action="store", dest="ipv4_mask", default=False, help="set Subnet mask address")
  parser.add_argument('--dns', action="store", dest="ipv4_dns", default=False, help="set DNS IP address")
  parser.add_argument('-v', '--verbose', action="store", dest="verbose", choices=['0', '1', '2', '3', '4', '5'], default='3', help="Enable verbose logging 0=critical, 1=error, 2=warning, 3=info, 4=debug, 5=trace.")
  parser.add_argument('--log-file', action="store", dest="log_filename", default=False, help="Create output log file with chosen filename.")
  parser.add_argument('--reboot', action="store_true", dest='reboot', default=False, help="Preform a reboot of the device.")
  parser.add_argument('hosts', type=str, nargs='*')
  args = parser.parse_args()
  if args.list:
    action = 'list'
  elif args.reboot:
    action = 'reboot'
  else:
    action = 'flash'
  args.mode = 'stock' if args.mode == 'revert' else args.mode
  args.hap_setup_code = f"{args.hap_setup_code[:3]}-{args.hap_setup_code[3:-3]}-{args.hap_setup_code[5:]}" if args.hap_setup_code and '-' not in args.hap_setup_code else args.hap_setup_code

  sh = MStreamHandler()
  sh.setFormatter(logging.Formatter('%(message)s'))
  sh.setLevel(log_level[args.verbose])
  if int(args.verbose) >= 4:
    args.info_level = '3'
  if args.log_filename:
    fh = MFileHandler(args.log_filename, mode='w', encoding='utf-8')
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
  app_version = "2.6.0"

  logger.debug(f"OS: {PURPLE}{arch}{NC}")
  logger.debug(f"app_version: {app_version}")
  logger.debug(f"manual_hosts: {args.hosts} ({len(args.hosts)})")
  logger.debug(f"action: {action}")
  logger.debug(f"mode: {args.mode}")
  logger.debug(f"info_level: {args.info_level}")
  logger.debug(f"fw_type: {args.fw_type}")
  logger.debug(f"model_type: {args.model_type}")
  logger.debug(f"do_all: {args.do_all}")
  logger.debug(f"dry_run: {args.dry_run}")
  logger.debug(f"quiet_run: {args.quiet_run}")
  logger.debug(f"silent_run: {args.silent_run}")
  logger.debug(f"version: {args.version}")
  logger.debug(f"exclude: {args.exclude}")
  logger.debug(f"local_file: {args.local_file}")
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
    if action in ('list', 'flash'):
      args.do_all = True
    else:
      message = f"{WHITE}Requires a hostname or -a | --all{NC}"
  elif args.hosts and args.do_all:
    message = f"{WHITE}Invalid option hostname or -a | --all not both.{NC}"
  elif args.list and args.reboot:
    message = f"{WHITE}Invalid option -l or --reboot not both.{NC}"
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

  main = Main(args.hosts, action, args.log_filename, args.dry_run, args.quiet_run, args.silent_run, args.mode, None, args.info_level, args.fw_type, args.model_type, args.exclude, args.version, args.variant,
              args.hap_setup_code, args.local_file, args.network_type, args.ipv4_ip, args.ipv4_mask, args.ipv4_gw, args.ipv4_dns)
  atexit.register(main.results)
  try:
    main.device_scan()
  except KeyboardInterrupt:
    main.stop_scan

  if http_server_started and server:
    logger.trace("Shutting down webserver")
    server.shutdown()
    thread.join()
