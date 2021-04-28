#!/usr/bin/env python3
"""
Copyright (c) 2021 Shelly-HomeKit Contributors
All rights reserved

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

This script will probe for any shelly device on the network and it will
attempt to update them to the latest firmware version available.
This script will not flash any firmware to a device that is not already on a
version of this firmware, if you are looking to flash your device from stock
or any other firmware please follow instructions here:
https://github.com/mongoose-os-apps/shelly-homekit/wiki

usage: flash-shelly.py [-h] [--app-version] [-f] [-l] [-m {homekit,keep,revert}] [-i {1,2,3}] [-ft {homekit,stock,all}] [-mt MODEL_TYPE_FILTER] [-dn DEVICE_NAME_FILTER] [-a] [-q] [-e [EXCLUDE ...]] [-n] [-y] [-V VERSION]
                       [--variant VARIANT] [--local-file LOCAL_FILE] [-c HAP_SETUP_CODE] [--ip-type {dhcp,static}] [--ip IPV4_IP] [--gw IPV4_GW] [--mask IPV4_MASK] [--dns IPV4_DNS] [-v {0,1,2,3,4,5}] [--timeout TIMEOUT]
                       [--log-file LOG_FILENAME] [--reboot] [--user USER] [--password PASSWORD]
                       [hosts ...]

Shelly HomeKit flashing script utility

positional arguments:
  hosts

optional arguments:
  -h, --help            show this help message and exit
  --app-version         Shows app version and exists.
  -f, --flash           Flash firmware to shelly device(s).
  -l, --list            List info of shelly device(s).
  -m {homekit,keep,revert}, --mode {homekit,keep,revert}
                        Script mode homekit=homekit firmware, revert=stock firmware, keep=use current firmware type
  -i {1,2,3}, --info-level {1,2,3}
                        Control how much detail is output in the list 1=minimal, 2=basic, 3=all.
  -ft {homekit,stock,all}, --fw-type {homekit,stock,all}
                        Limit scan to current firmware type.
  -mt MODEL_TYPE_FILTER, --model-type MODEL_TYPE_FILTER
                        Limit scan to model type (dimmer, rgbw2, shelly1, etc).
  -dn DEVICE_NAME_FILTER, --device-name DEVICE_NAME_FILTER
                        Limit scan to include term in device name..
  -a, --all             Run against all the devices on the network.
  -q, --quiet           Only include upgradeable shelly devices.
  -e [EXCLUDE ...], --exclude [EXCLUDE ...]
                        Exclude hosts from found devices.
  -n, --assume-no       Do a dummy run through.
  -y, --assume-yes      Do not ask any confirmation to perform the flash.
  -V VERSION, --version VERSION
                        Force a particular version.
  --variant VARIANT     Pre-release variant name.
  --local-file LOCAL_FILE
                        Use local file to flash.
  -c HAP_SETUP_CODE, --hap-setup-code HAP_SETUP_CODE
                        Configure HomeKit setup code, after flashing.
  --ip-type {dhcp,static}
                        Configure network IP type (Static or DHCP)
  --ip IPV4_IP          set IP address
  --gw IPV4_GW          set Gateway IP address
  --mask IPV4_MASK      set Subnet mask address
  --dns IPV4_DNS        set DNS IP address
  -v {0,1,2,3,4,5}, --verbose {0,1,2,3,4,5}
                        Enable verbose logging 0=critical, 1=error, 2=warning, 3=info, 4=debug, 5=trace.
  --timeout TIMEOUT     Scan: Time of seconds to wait after last detected device before quitting. Manual: Time of seconds to keeping to connect.
  --log-file LOG_FILENAME
                        Create output log file with chosen filename.
  --reboot              Preform a reboot of the device.
  --user USER           Enter username for device security (default = admin).
  --password PASSWORD   Enter password for device security.
"""

import argparse
import atexit
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
import traceback
import zipfile

logging.TRACE = 5
logging.addLevelName(logging.TRACE, 'TRACE')
logging.Logger.trace = functools.partialmethod(logging.Logger.log, logging.TRACE)
logging.trace = functools.partial(logging.log, logging.TRACE)
logger = logging.getLogger(__name__)
logger.setLevel(logging.TRACE)
log_level = {0: logging.CRITICAL,
             1: logging.ERROR,
             2: logging.WARNING,
             3: logging.INFO,
             4: logging.DEBUG,
             5: logging.TRACE}


def upgrade_pip():
  logger.info("Updating pip...")
  subprocess.run([sys.executable, '-m', 'pip', 'install', '--upgrade', 'pip'])


def install_import(library):
    logger.info(f"Installing {library}...")
    upgrade_pip()
    subprocess.run([sys.executable, '-m', 'pip', 'install', library])


try:
  import zeroconf
except ImportError:
  install_import('zeroconf')
  import zeroconf
try:
  import requests
  from requests.auth import HTTPDigestAuth
except ImportError:
  install_import('requests')
  import requests
  from requests.auth import HTTPDigestAuth
try:
  import yaml
except ImportError:
  install_import('pyyaml')
  import yaml

app_ver = '2.8.0'
security_file = 'flash-shelly.auth.yaml'
webserver_port = 8381
http_server_started = False
server = None
thread = None
total_devices = 0
upgradeable_devices = 0
flashed_devices = 0
failed_flashed_devices = 0
arch = platform.system()
stock_info_url = 'https://api.shelly.cloud/files/firmware'
stock_release_info = None
tried_to_get_remote_homekit = False
homekit_info_url = 'https://rojer.me/files/shelly/update.json'
homekit_release_info = None
tried_to_get_remote_stock = False
flash_question = None
requires_upgrade = None
requires_mode_change = None
not_supported = None

WHITE = '\033[1m'
RED = '\033[1;91m'
GREEN = '\033[1;92m'
YELLOW = '\033[1;93m'
BLUE = '\033[1;94m'
PURPLE = '\033[1;95m'
NC = '\033[0m'


class MFileHandler(logging.FileHandler):
  # Handler that controls the writing of the newline character
  special_code = '[!n]'
  terminator = None

  def emit(self, record) -> None:
    if self.special_code in record.msg:
      record.msg = record.msg.replace(self.special_code, '')
      self.terminator = ''
    else:
      self.terminator = '\n'
    return super().emit(record)


class MStreamHandler(logging.StreamHandler):
  # Handler that controls the writing of the newline character
  special_code = '[!n]'
  terminator = None

  def emit(self, record) -> None:
    if self.special_code in record.msg:
      record.msg = record.msg.replace(self.special_code, '')
      self.terminator = ''
    else:
      self.terminator = '\n'
    return super().emit(record)


class HTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
  def log_request(self, code='-', size='-'):
    pass


class StoppableHTTPServer(http.server.HTTPServer):
  def run(self):
    try:
      self.serve_forever()
    except Exception:
      pass
    finally:
      self.server_close()


class ServiceListener:
  def __init__(self):
    self.queue = queue.Queue()

  def add_service(self, zc, service_type, device):
    host = device.replace(f'{service_type}', 'local')
    info = zc.get_service_info(service_type, device)
    if info:
      properties = info.properties
      properties = {y.decode('UTF-8'): properties.get(y).decode('UTF-8') for y in properties.keys()}
      logger.trace(f"[Device Scan] found device: {host} added, IP address: {socket.inet_ntoa(info.addresses[0])}")
      logger.trace(f"[Device Scan] info: {info}")
      logger.trace(f"[Device Scan] properties: {properties}")
      logger.trace("")
      (username, password) = main.get_security_data(host)
      self.queue.put(Device(host, username, password, socket.inet_ntoa(info.addresses[0])))

  @staticmethod
  def remove_service(zc, service_type, device):
    host = device.replace(f'.{service_type}', '')
    info = zc.get_service_info(service_type, device)
    logger.trace(f"[Device Scan] device removed: {host}")
    logger.trace(f"[Device Scan] info: {info}")
    logger.trace('')

  @staticmethod
  def update_service(zc, service_type, device):
    host = device.replace(f'.{service_type}', '')
    info = zc.get_service_info(service_type, device)
    logger.trace(f"[Device Scan] device updated: {host}")
    logger.trace(f"[Device Scan] info: {info}")
    logger.trace('')


class Device:
  def __init__(self, host, username=None, password=None, wifi_ip=None, info=None, no_error_message=False):
    self.host = host
    self.friendly_host = host.replace('.local', '')
    self.username = username
    self.password = password
    self.wifi_ip = wifi_ip
    self.info = info if info is not None else {}
    self.variant = main.variant
    self.version = main.version
    self.local_file = main.local_file
    self.fw_version = None
    self.fw_type_str = None
    self.flash_fw_version = '0.0.0'
    self.flash_fw_type_str = None
    self.download_url = None
    self.already_processed = False
    self.get_device_info(False, no_error_message)

  def is_host_reachable(self, host, no_error_message=False):
    # check if host is reachable
    self.host = main.host_check(host)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(3)
    if not self.wifi_ip:
      try:
        # use manual IP supplied
        ipaddress.IPv4Address(host)
        test_host = host
        self.host = host
      except ipaddress.AddressValueError:
        test_host = self.host
    else:
      test_host = self.wifi_ip
    try:
      sock.connect((test_host, 80))
      logger.debug(f"Device: {test_host} is Online")
      host_is_reachable = True
    except socket.error:
      if not no_error_message:
        logger.error(f"")
        logger.error(f"{RED}Could not connect to host: {self.host}{NC}")
      host_is_reachable = False
    sock.close()
    if host_is_reachable and not self.wifi_ip:
      # resolve IP from manual hostname
      self.wifi_ip = socket.gethostbyname(test_host)
    return host_is_reachable

  def get_device_info(self, force_update=False, no_error_message=False):
    info = {}
    device_url = None
    status = None
    fw_type = None
    fw_info = None
    if (not self.info or force_update) and self.is_host_reachable(self.host, no_error_message):
      try:
        status_check = requests.get(f'http://{self.wifi_ip}/status', auth=(self.username, self.password), timeout=10)
        if status_check.status_code == 200:
          fw_type = "stock"
          status = json.loads(status_check.content)
          if status.get('status', '') != '':
            self.info = {}
            return self.info
          fw_info = requests.get(f'http://{self.wifi_ip}/settings', auth=(self.username, self.password), timeout=3)
          device_url = f'http://{self.wifi_ip}/settings'
        else:
          fw_type = "homekit"
          fw_info = requests.get(f'http://{self.wifi_ip}/rpc/Shelly.GetInfoExt', auth=HTTPDigestAuth(self.username, self.password), timeout=3)
          device_url = f'http://{self.wifi_ip}/rpc/Shelly.GetInfoExt'
          if fw_info.status_code in (401, 404):
            logger.debug("Invalid password or security not enabled.")
            fw_info = requests.get(f'http://{self.wifi_ip}/rpc/Shelly.GetInfo', timeout=3)
            device_url = f'http://{self.wifi_ip}/rpc/Shelly.GetInfo'
      except Exception:
        pass
      logger.trace(f"status code: {fw_info.status_code}")
      if fw_info.status_code == 401:
        self.info = 401
        return 401
      if fw_info is not None and fw_info.status_code == 200:
        info = json.loads(fw_info.content)
        info['fw_type'] = fw_type
        info['device_name'] = info.get('name')
        if status is not None:
          info['status'] = status
      logger.trace(f"Device URL: {device_url}")
      if not info:
        logger.debug(f"Could not get info from device: {self.host}")
      self.info = info
    return self.info

  def is_homekit(self):
    return self.info.get('fw_type') == 'homekit'

  def is_stock(self):
    return self.info.get('fw_type') == 'stock'

  @staticmethod
  def parse_stock_version(version):
    """
    stock can be '20201124-092159/v1.9.0@57ac4ad8', we need '1.9.0'
    stock can be '20201124-092159/v1.9.5-DM2_autocheck', we need '1.9.5-DM2'
    stock can be '20210107-122133/1.9_GU10_RGBW@07531e29', we need '1.9'
    stock can be '20201014-165335/1244-production-Shelly1L@6a254598', we need '0.0.0'
    stock can be '20210226-091047/v1.10.0-rc2-89-g623b41ec0-master', we need '1.10.0-rc2'
    stock can be '20210318-141537/v1.10.0-geba262d', we need '1.10.0'
    """
    v = re.search(r'/.*?(?P<ver>([0-9]+\.)([0-9]+)(\.[0-9]+)?(-[a-z0-9]*)?)(?P<rest>([@\-_])[a-z0-9\-]*)', version, re.IGNORECASE)
    debug_info = v.groupdict() if v is not None else v
    logger.trace(f"parse stock version: {version}  group: {debug_info}")
    parsed_version = v.group('ver') if v is not None else '0.0.0'
    return parsed_version

  def get_current_version(self, no_error_message=False):  # used when flashing between firmware versions.
    version = None
    if not self.get_device_info(True, no_error_message):
      return None
    if self.is_homekit():
      version = self.info.get('version', '0.0.0')
    elif self.is_stock():
      version = self.parse_stock_version(self.info.get('fw', '0.0.0'))
    return version

  def get_uptime(self, no_error_message=False):
    uptime = -1
    if not self.get_device_info(True, no_error_message):
      logger.trace(f"get_uptime: -1")
      return -1
    if self.is_homekit():
      uptime = self.info.get('uptime', -1)
    elif self.is_stock():
      uptime = self.info.get('status', {}).get('uptime', -1)
    logger.trace(f"get_uptime: {uptime}")
    time.sleep(1)  # wait for time check to fall behind
    return uptime

  @staticmethod
  def shelly_model(stock_fw_model):
    options = {'SHPLG-1': ['ShellyPlug', 'shelly-plug'],
               'SHPLG-S': ['ShellyPlugS', 'shelly-plug-s'],
               'SHPLG-U1': ['ShellyPlugUS', 'shelly-plug-u1'],
               'SHPLG2-1': ['ShellyPlug', 'shelly-plug2'],
               'SHSW-1': ['Shelly1', 'switch1'],
               'SHSW-PM': ['Shelly1PM', 'switch1pm'],
               'SHSW-L': ['Shelly1L', 'switch1l'],
               'SHSW-21': ['Shelly2', 'switch'],
               'SHSW-25': ['Shelly25', 'switch25'],
               'SHAIR-1': ['ShellyAir', 'air'],
               'SHSW-44': ['Shelly4Pro', 'dinrelay4'],
               'SHUNI-1': ['ShellyUni', 'uni'],
               'SHEM': ['ShellyEM', 'shellyem'],
               'SHEM-3': ['Shelly3EM', 'shellyem3'],
               'SHSEN-1': ['ShellySensor', 'smart-sensor'],
               'SHGS-1': ['ShellyGas', 'gas-sensor'],
               'SHSM-01': ['ShellySmoke', 'smoke-sensor'],
               'SHHT-1': ['ShellyHT', 'ht-sensor'],
               'SHWT-1': ['ShellyFlood', 'water-sensor'],
               'SHDW-1': ['ShellyDoorWindow', 'doorwindow-sensor'],
               'SHDW-2': ['ShellyDoorWindow2', 'doorwindow-sensor2'],
               'SHMOS-01': ['ShellyMotion', 'motion-sensor'],
               'SHSPOT-1': ['ShellySpot', 'spot'],
               'SHCL-255': ['ShellyColor', 'color'],
               'SHBLB-1': ['ShellyBulb', 'bulb'],
               'SHCB-1': ['ShellyColorBulb', 'color-bulb'],
               'SHRGBW2': ['ShellyRGBW2', 'rgbw2'],
               'SHRGBW2-color': ['ShellyRGBW2', 'rgbw2-color'],
               'SHRGBW2-white': ['ShellyRGBW2', 'rgbw2-white'],
               'SHRGBWW-01': ['ShellyRGBWW', 'rgbww'],
               'SH2LED-1': ['ShellyLED', 'led1'],
               'SHDM-1': ['ShellyDimmer', 'dimmer'],
               'SHDM-2': ['ShellyDimmer2', 'dimmer-l51'],
               'SHDIMW-1': ['ShellyDimmerW', 'dimmerw1'],
               'SHVIN-1': ['ShellyVintage', 'bulb6w'],
               'SHBDUO-1': ['ShellyDuo', 'bulbduo'],
               'SHBTN-1': ['ShellyButton1', 'wifi-button'],
               'SHBTN-2': ['ShellyButton2', 'wifi-button2'],
               'SHIX3-1': ['ShellyI3', 'ix3']
               }
    return options.get(stock_fw_model, [stock_fw_model, stock_fw_model])

  def parse_local_file(self):
    if os.path.exists(self.local_file) and self.local_file.endswith('.zip'):
      s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
      s.connect((self.wifi_ip, 80))
      local_ip = s.getsockname()[0]
      logger.debug(f"Host IP: {local_ip}")
      s.close()
      manifest_file = None
      with zipfile.ZipFile(self.local_file, "r") as zfile:
        for name in zfile.namelist():
          if name.endswith('manifest.json'):
            logger.debug(f"zipfile: {name}")
            mfile = zfile.read(name)
            manifest_file = json.loads(mfile)
            logger.debug(f"manifest: {json.dumps(manifest_file, indent=2)}")
            break
      if manifest_file is not None:
        manifest_version = manifest_file.get('version', '0.0.0')
        manifest_name = manifest_file.get('name')
        if manifest_version == '1.0':
          self.version = self.parse_stock_version(manifest_file.get('build_id', '0.0.0'))
          self.flash_fw_type_str = "Stock"
        else:
          self.version = manifest_version
          self.flash_fw_type_str = "HomeKit"
        logger.debug(f"Mode: {self.fw_type_str} To {self.flash_fw_type_str}")
        self.flash_fw_version = self.version
        if self.is_homekit():
          self.download_url = 'local'
        else:
          self.download_url = f'http://{local_ip}:{webserver_port}/{self.local_file}'
        if self.is_stock() and self.info.get('stock_fw_model') == 'SHRGBW2' and self.info.get('color_mode'):
          m_model = f"{self.info.get('app')}-{self.info.get('color_mode')}"
        else:
          m_model = self.info.get('app')
        if 'rgbw2' in m_model:
          m_model = m_model.split('-')[0]
        if 'rgbw2' in manifest_name:
          manifest_name = manifest_name.split('-')[0]
        if m_model != manifest_name:
          self.flash_fw_version = '0.0.0'
          self.download_url = None
        return True
      else:
        logger.info(f"Invalid file.")
        self.flash_fw_version = '0.0.0'
        self.download_url = None
        return False
    else:
      logger.info(f"File does not exist.")
      self.flash_fw_version = '0.0.0'
      self.download_url = None
      return False

  def update_homekit(self, release_info=None):
    global not_supported
    self.flash_fw_type_str = 'HomeKit'
    logger.debug(f"Mode: {self.fw_type_str} To {self.flash_fw_type_str}")
    if self.version:
      self.flash_fw_version = self.version
      self.download_url = f"http://rojer.me/files/shelly/{self.version}/shelly-homekit-{self.info.get('model')}.zip"
    elif release_info is None:
      self.flash_fw_version = 'revert'
      self.download_url = f"http://rojer.me/files/shelly/stock/{self.info.get('stock_fw_model')}.zip"
    else:
      for i in release_info:
        if self.variant and self.variant not in i[1].get('version', '0.0.0'):
          self.flash_fw_version = 'no_variant'
          self.download_url = None
          return
        if self.variant:
          re_search = r'-*'
        else:
          re_search = i[0]
        if re.search(re_search, self.fw_version):
          self.flash_fw_version = i[1].get('version', '0.0.0')
          self.download_url = i[1].get('urls', {}).get(self.info.get('model'))
          break
      not_supported = True

  def update_stock(self, release_info=None):
    self.flash_fw_type_str = 'Stock'
    stock_fw_model = self.info.get('stock_fw_model')
    color_mode = self.info.get('color_mode')
    logger.debug(f"Mode: {self.fw_type_str} To {self.flash_fw_type_str}")
    if not self.version:
      if stock_fw_model == 'SHRGBW2-color':  # we need to use real stock model here
        stock_fw_model = 'SHRGBW2'
      stock_model_info = release_info.get('data', {}).get(stock_fw_model)
      if self.variant == 'beta':
        self.flash_fw_version = self.parse_stock_version(stock_model_info.get('beta_ver', '0.0.0'))
        self.download_url = stock_model_info.get('beta_url')
      else:
        self.flash_fw_version = self.parse_stock_version(stock_model_info.get('version', '0.0.0'))
        self.download_url = stock_model_info.get('url')
    else:
      self.flash_fw_version = self.version
      self.download_url = f'http://archive.shelly-tools.de/version/v{self.version}/{stock_fw_model}.zip'
    if stock_fw_model == 'SHRGBW2':
      if self.download_url and not self.version and color_mode and not self.local_file:
        self.download_url = self.download_url.replace('.zip', f'-{color_mode}.zip')


class HomeKitDevice(Device):
  def get_info(self):
    if not self.info:
      return False
    self.fw_type_str = 'HomeKit'
    self.fw_version = self.info.get('version', '0.0.0')
    if self.info.get('stock_fw_model') is None:  # TODO remove once 2.9.x is mainstream.
      self.info['stock_fw_model'] = self.info.get('stock_model')
    self.info['sys_mode_str'] = self.get_mode(self.info.get('sys_mode'))
    return True

  @staticmethod
  def get_mode(mode):
    options = {0: 'Switch',
               1: 'Roller Shutter',
               2: 'Garage Door',
               3: 'RGB',
               4: 'RGBW'
               }
    return options.get(mode, mode)

  def flash_firmware(self, revert=False):
    logger.trace(f"revert {revert}")
    if self.local_file:
      logger.debug(f"Local file: {self.local_file}")
      my_file = open(self.local_file, 'rb')
      logger.info(f"Now Flashing {self.flash_fw_type_str} {self.flash_fw_version}")
      files = {'file': ('shelly-flash.zip', my_file)}
    else:
      logger.info("Downloading Firmware...")
      logger.debug(f"Remote Download URL: {self.download_url}")
      my_file = requests.get(self.download_url)
      if revert is True:
        logger.info(f"Now Reverting to stock firmware")
      else:
        logger.info(f"Now Flashing {self.flash_fw_type_str} {self.flash_fw_version}")
      files = {'file': ('shelly-flash.zip', my_file.content)}
    logger.debug(f"requests.post(url='http://{self.wifi_ip}/update', auth=HTTPDigestAuth('{self.username}', '{self.password}'), files=files)")
    response = requests.post(url=f'http://{self.wifi_ip}/update', auth=HTTPDigestAuth(self.username, self.password), files=files)
    logger.trace(response.text)
    logger.trace(f"response.status_code: {response.status_code}")
    if response.status_code == 401:
      main.security_help(self)
    return response

  def preform_reboot(self):
    logger.info(f"Rebooting...")
    logger.debug(f"requests.post(url='http://{self.wifi_ip}/rpc/SyS.Reboot', auth=HTTPDigestAuth('{self.username}', '{self.password}'))")
    response = requests.get(url=f'http://{self.wifi_ip}/rpc/SyS.Reboot', auth=HTTPDigestAuth(self.username, self.password))
    logger.trace(response.text)
    if response.status_code == 401:
      main.security_help(self)
    return response


class StockDevice(Device):
  def get_info(self):
    if not self.info:
      return False
    self.fw_type_str = 'Stock'
    self.fw_version = self.parse_stock_version(self.info.get('fw', '0.0.0'))  # current firmware version
    stock_fw_model = self.info.get('device', {}).get('type', '')
    self.info['stock_fw_model'] = stock_fw_model
    self.info['model'] = self.shelly_model(stock_fw_model)[0]
    self.info['app'] = self.shelly_model(stock_fw_model)[1]
    self.info['device_id'] = self.info.get('mqtt', {}).get('id', self.friendly_host)
    self.info['color_mode'] = self.info.get('mode')
    self.info['wifi_ssid'] = self.info.get('status', {}).get('wifi_sta', {}).get('ssid')
    self.info['wifi_rssi'] = self.info.get('status', {}).get('wifi_sta', {}).get('rssi')
    self.info['uptime'] = self.info.get('status', {}).get('uptime', 0)
    self.info['battery'] = self.info.get('status', {}).get('bat', {}).get('value')
    return True

  def get_color_mode(self, no_error_message=False):  # used when flashing between firmware versions.
    if not self.get_device_info(True, no_error_message):
      return None
    return self.info.get('mode')

  def flash_firmware(self, revert=False):
    logger.trace(f"revert {revert}")
    logger.info(f"Now Flashing {self.flash_fw_type_str} {self.flash_fw_version}")
    download_url = self.download_url.replace('https', 'http')
    if self.local_file:
      logger.debug(f"Local file: {self.local_file}")
      logger.debug(f"Local Download URL: {download_url}")
    else:
      logger.debug(f"Remote Download URL: {download_url}")
    if self.fw_version == '0.0.0':
      flash_url = f'http://{self.wifi_ip}/ota?update=true'
    else:
      flash_url = f'http://{self.wifi_ip}/ota?url={download_url}'
    logger.debug(f"requests.get(url={flash_url}, auth=('{self.username}', '{self.password}')")
    response = requests.get(url=flash_url, auth=(self.username, self.password))
    logger.trace(response.text)
    if response.status_code == 401:
      main.security_help(self)
    return response

  def preform_reboot(self):
    logger.info(f"Rebooting...")
    logger.debug(f"requests.post(url={f'http://{self.wifi_ip}/reboot'}, auth=('{self.username}', '{self.password}'d)")
    response = requests.get(url=f'http://{self.wifi_ip}/reboot', auth=(self.username, self.password))
    logger.trace(response.text)
    if response.status_code == 401:
      main.security_help(self)
    return response

  def perform_mode_change(self, mode_color):
    logger.info("Performing mode change...")
    logger.debug(f"requests.post(url={f'http://{self.wifi_ip}/settings/?mode={mode_color}'}, auth=('{self.username}', '{self.password}'))")
    response = requests.get(url=f'http://{self.wifi_ip}/settings/?mode={mode_color}', auth=(self.username, self.password))
    logger.trace(response.text)
    if response.status_code == 401:
      main.security_help(self)
    while response.status_code == 400:
      time.sleep(2)
      try:
        response = requests.get(url=f'http://{self.wifi_ip}/settings/?mode={mode_color}', auth=(self.username, self.password))
        logger.trace(f"response.status_code: {response.status_code}")
      except ConnectionResetError:
        pass
    return response


# noinspection PyUnboundLocalVariable,PyUnresolvedReferences
class Main:
  def __init__(self):
    self.hosts = None
    self.username = None
    self.password = None
    self.run_action = None
    self.timeout = None
    self.log_filename = None
    self.dry_run = None
    self.quiet_run = None
    self.silent_run = None
    self.mode = None
    self.info_level = None
    self.fw_type_filter = None
    self.model_type_filter = None
    self.device_name_filter = None
    self.exclude = None
    self.version = None
    self.variant = None
    self.hap_setup_code = None
    self.local_file = None
    self.network_type = None
    self.ipv4_ip = None
    self.ipv4_mask = None
    self.ipv4_gw = None
    self.ipv4_dns = None
    self.flash_mode = None
    self.zc = None
    self.listener = None
    self.config_data = {}
    self.security_data = {}

  def load_security(self):
    logger.trace(f"load_security")
    data = {}
    if os.path.exists(security_file):
      with open(security_file) as fp:
        data = yaml.load(fp, Loader=yaml.FullLoader)
      logger.trace(f"security: {yaml.dump(data, indent=2)}")
    self.security_data = data

  def get_security_data(self, host):
    logger.trace(f"load_security")
    host = self.host_check(host)
    if not self.password and self.security_data.get(host):
      username = self.security_data.get(host).get('user')
      password = self.security_data.get(host).get('password')
    else:
      username = main.username
      password = main.password
    logger.debug(f"[login] {host} {self.security_data.get(host)}")
    logger.debug(f"[login] username: {username}")
    logger.debug(f"[login] password: {password}")
    return username, password

  def save_security(self, device_info):
    logger.trace(f"save_security")
    current_user = self.security_data.get(device_info.host, {}).get('user')
    current_password = self.security_data.get(device_info.host, {}).get('password')
    if not current_user or not current_password:
      save_security = True
      logger.debug(f"")
      logger.debug(f"{WHITE}Security:{NC} Saving data for {device_info.host}[!n]")
    elif current_user != device_info.username or current_password != device_info.password:
      logger.debug(f"")
      logger.debug(f"{WHITE}Security:{NC} Updating data for {device_info.host}{NC}[!n]")
      save_security = True
    if save_security:
      data = {'user': device_info.username, 'password': device_info.password}
      self.security_data[device_info.host] = data
      logger.trace(yaml.dump(self.security_data[device_info.host]))
      with open(security_file, 'w') as yaml_file:
        yaml.dump(self.security_data, yaml_file)

  def security_help(self, device_info, mode='Manual'):
    example_dict = {"shelly-AF0183.local": {"user": "admin", "password": "abc123"}}
    logger.info(f"{WHITE}Host: {NC}{device_info.host} {RED}is password protected{NC}")
    if self.security_data:
      if self.security_data.get(device_info.host) is None:
        if mode == 'Manual' and self.password:
          logger.info(f"Invalid user or password, please check supplied details are correct.")
          logger.info(f"username: {self.username}")
          logger.info(f"password: {self.password}")
        elif mode == 'Manual' and not self.password:
          logger.info(f"{device_info.host} is not found in '{security_file}' config file,")
          logger.info(f"please add or use commandline args --user | --password")
        else:
          logger.info(f"{device_info.host} is not found in '{security_file}' config file,")
          logger.info(f"'{config_file}' security file is required in scanner mode.")
          logger.info(f"unless all devices use same password.{NC}")
      else:
        logger.info(f"Invalid user or password found in '{security_file}',please check supplied details are correct.")
        logger.info(f"username: {self.security_data.get(device_info.host).get('user')}")
        logger.info(f"password: {self.security_data.get(device_info.host).get('password')}{NC}")
    else:
      logger.info(f"Please use either command line security (--user | --password) or '{security_file}'")
      logger.info(f"for '{security_file}', create a file called '{security_file}' in tools folder")
      logger.info(f"{WHITE}Example {security_file}:{NC}")
      logger.info(f"{YELLOW}{yaml.dump(example_dict, indent=2)}{NC}[!n]")

  @staticmethod
  def host_check(host):
    host_check = re.search(r'\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}', host)
    return f'{host}.local' if '.local' not in host and not host_check else host

  def run_app(self):
    parser = argparse.ArgumentParser(prog='flash-shelly.py', fromfile_prefix_chars='@', description='Shelly HomeKit flashing script utility')
    parser.add_argument('--app-version', action="store_true", help="Shows app version and exists.")
    parser.add_argument('-f', '--flash', action="store_true", help="Flash firmware to shelly device(s).")
    parser.add_argument('-l', '--list', action="store_true", help="List info of shelly device(s).")
    parser.add_argument('-m', '--mode', choices=['homekit', 'keep', 'revert'], default="homekit", help="Script mode homekit=homekit firmware, revert=stock firmware, keep=use current firmware type")
    parser.add_argument('-i', '--info-level', dest='info_level', type=int, choices=[1, 2, 3], default=2, help="Control how much detail is output in the list 1=minimal, 2=basic, 3=all.")
    parser.add_argument('-ft', '--fw-type', dest='fw_type_filter', choices=['homekit', 'stock', 'all'], default="all", help="Limit scan to current firmware type.")
    parser.add_argument('-mt', '--model-type', dest='model_type_filter', default='all', help="Limit scan to model type (dimmer, rgbw2, shelly1, etc).")
    parser.add_argument('-dn', '--device-name', dest='device_name_filter', default='all', help="Limit scan to include term in device name..")
    parser.add_argument('-a', '--all', action="store_true", dest='do_all', help="Run against all the devices on the network.")
    parser.add_argument('-q', '--quiet', action="store_true", dest='quiet_run', help="Only include upgradeable shelly devices.")
    parser.add_argument('-e', '--exclude', dest="exclude", nargs='*', default='', help="Exclude hosts from found devices.")
    parser.add_argument('-n', '--assume-no', action="store_true", dest='dry_run', help="Do a dummy run through.")
    parser.add_argument('-y', '--assume-yes', action="store_true", dest='silent_run', help="Do not ask any confirmation to perform the flash.")
    parser.add_argument('-V', '--version', type=str, dest="version", default='', help="Force a particular version.")
    parser.add_argument('--variant', dest="variant", default='', help="Pre-release variant name.")
    parser.add_argument('--local-file', dest="local_file", default='', help="Use local file to flash.")
    parser.add_argument('-c', '--hap-setup-code', dest="hap_setup_code", default='', help="Configure HomeKit setup code, after flashing.")
    parser.add_argument('--ip-type', choices=['dhcp', 'static'], dest="network_type", default='', help="Configure network IP type (Static or DHCP)")
    parser.add_argument('--ip', dest="ipv4_ip", default='', help="set IP address")
    parser.add_argument('--gw', dest="ipv4_gw", default='', help="set Gateway IP address")
    parser.add_argument('--mask', dest="ipv4_mask", default='', help="set Subnet mask address")
    parser.add_argument('--dns', dest="ipv4_dns", default='', help="set DNS IP address")
    parser.add_argument('-v', '--verbose', dest="verbose", type=int, choices=[0, 1, 2, 3, 4, 5], default=3, help="Enable verbose logging 0=critical, 1=error, 2=warning, 3=info, 4=debug, 5=trace.")
    parser.add_argument('--timeout', type=int, default=20, help="Scan: Time of seconds to wait after last detected device before quitting.  Manual: Time of seconds to keeping to connect.")
    parser.add_argument('--log-file', dest="log_filename", default='', help="Create output log file with chosen filename.")
    parser.add_argument('--reboot', action="store_true", help="Preform a reboot of the device.")
    parser.add_argument('--user', default='admin', help="Enter username for device security (default = admin).")
    parser.add_argument('--password', default='', help="Enter password for device security.")
    parser.add_argument('hosts', type=str, nargs='*', default='')
    args = parser.parse_args()

    sh = MStreamHandler()
    sh.setFormatter(logging.Formatter('%(message)s'))
    logger.addHandler(sh)
    sh.setLevel(log_level[args.verbose])

    if args.app_version:
      logger.info(f"Version: {app_ver}")
      sys.exit(0)

    self.load_security()

    if args.flash:
      action = 'flash'
    elif args.reboot:
      action = 'reboot'
    elif args.list:
      action = 'list'
    else:
      action = 'flash'
    args.hap_setup_code = f"{args.hap_setup_code[:3]}-{args.hap_setup_code[3:-3]}-{args.hap_setup_code[5:]}" if args.hap_setup_code and '-' not in args.hap_setup_code else args.hap_setup_code

    sh.setLevel(log_level[args.verbose])
    if args.verbose >= 4:
      args.info_level = 3
    if args.log_filename:
      args.verbose = 5
      args.info_level = 3
      fh = MFileHandler(args.log_filename, mode='w', encoding='UTF-8')
      fh.setFormatter(logging.Formatter('%(asctime)s %(levelname)s %(lineno)d %(message)s'))
      fh.setLevel(log_level[args.verbose])
      logger.addHandler(fh)

    # Windows and log file do not support acsii colours
    if args.log_filename or arch.startswith('Win'):
      global NC, WHITE, RED, GREEN, YELLOW, BLUE, PURPLE
      WHITE = ''
      RED = ''
      GREEN = ''
      YELLOW = ''
      BLUE = ''
      PURPLE = ''
      NC = ''

    logger.debug(f"OS: {PURPLE}{arch}{NC}")
    logger.debug(f"app_version: {app_ver}")
    logger.debug(f"manual_hosts: {args.hosts} ({len(args.hosts)})")
    logger.debug(f"user: {args.user}")
    logger.debug(f"password: {args.password}")
    logger.debug(f"action: {action}")
    logger.debug(f"mode: {args.mode}")
    logger.debug(f"timeout: {args.timeout}")
    logger.debug(f"info_level: {args.info_level}")
    logger.debug(f"fw_type_filter: {args.fw_type_filter}")
    logger.debug(f"model_type_filter: {args.model_type_filter}")
    logger.debug(f"device_name_filter: {args.device_name_filter}")
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

    self.hosts = args.hosts
    self.run_action = action
    self.timeout = args.timeout
    self.log_filename = args.log_filename
    self.dry_run = args.dry_run
    self.quiet_run = args.quiet_run
    self.silent_run = args.silent_run
    self.mode = args.mode
    self.info_level = args.info_level
    self.fw_type_filter = args.fw_type_filter
    self.model_type_filter = args.model_type_filter
    self.device_name_filter = args.device_name_filter
    self.exclude = args.exclude
    self.version = args.version
    self.variant = args.variant
    self.hap_setup_code = args.hap_setup_code
    self.local_file = args.local_file
    self.network_type = args.network_type
    self.ipv4_ip = args.ipv4_ip
    self.ipv4_mask = args.ipv4_mask
    self.ipv4_gw = args.ipv4_gw
    self.ipv4_dns = args.ipv4_dns
    self.username = args.user
    self.password = args.password

    message = None
    if not args.hosts and not args.do_all:
      if action in ('list', 'flash'):
        args.do_all = True
      else:
        message = f"{WHITE}Requires a hostname or -a | --all{NC}"
    elif args.hosts and args.do_all:
      message = f"{WHITE}Invalid option hostname or -a | --all not both.{NC}"
    elif args.list and args.reboot:
      args.list = False
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
      message = f"{WHITE}Incorrect version formatting i.e '1.9.0'{NC}"

    if message:
      logger.info(message)
      parser.print_help()
      sys.exit(1)

    atexit.register(main.exit_app)
    try:
      if self.hosts:
        self.manual_hosts()
      else:
        self.device_scan()
    except Exception:
      logger.info(f"{RED}")
      logger.info(f"flash-shelly version: {app_ver}")
      logger.info("Try to update your script, maybe the bug is already fixed!")
      exc_type, exc_value, exc_traceback = sys.exc_info()
      traceback.trace_exception(exc_type, exc_value, exc_traceback, file=sys.stdout)
      logger.info(f"{NC}")
    except KeyboardInterrupt:
      main.stop_scan()

  @staticmethod
  def get_release_info(info_type):
    release_info = False
    release_info_url = homekit_info_url if info_type == 'homekit' else stock_info_url
    try:
      fp = requests.get(release_info_url, timeout=3)
      logger.debug(f"{info_type} release_info status code: {fp.status_code}")
      if fp.status_code == 200:
        release_info = json.loads(fp.content)
    except requests.exceptions.RequestException as err:
      logger.critical(f"{RED}CRITICAL:{NC} {err}")
    logger.trace(f"{info_type} release_info: {json.dumps(release_info, indent=2)}")
    if not release_info:
      logger.error("")
      logger.error(f"{RED}Failed to lookup online firmware information{NC}")
      logger.error("For more information please point your web browser to:")
      logger.error("https://github.com/mongoose-os-apps/shelly-homekit/wiki/Flashing#script-fails-to-run")
    return release_info

  @staticmethod
  def parse_version(vs):
    # 1.9 / 1.9.2 / 1.9.3-rc3 / 1.9.5-DM2 / 2.7.0-beta1 / 2.7.0-latest
    try:
      v = re.search(r'^(?P<major>\d+).(?P<minor>\d+)(?:.(?P<patch>\d+))?(?:-(?P<pr>[a-zA-Z_]*)?(?P<pr_seq>\d*))?$', vs)
      debug_info = v.groupdict() if v is not None else v
      logger.trace(f"parse version: {vs}  group: {debug_info}")
      major = int(v.group('major'))
      minor = int(v.group('minor'))
      patch = int(v.group('patch')) if v.group('patch') is not None else 0
      variant = v.group('pr')
      var_seq = int(v.group('pr_seq')) if v.group('pr_seq') and v.group('pr_seq').isdigit() else 0
    except Exception:
      major = 0
      minor = 0
      patch = 0
      variant = ''
      var_seq = 0
    return major, minor, patch, variant, var_seq

  def is_newer(self, v1, v2):
    vi1 = self.parse_version(v1)
    vi2 = self.parse_version(v2)
    if vi1[0] != vi2[0]:
      value = vi1[0] > vi2[0]
    elif vi1[1] != vi2[1]:
      value = vi1[1] > vi2[1]
    elif vi1[2] != vi2[2]:
      value = vi1[2] > vi2[2]
    elif vi1[3] != vi2[3]:
      value = True
    elif vi1[4] != vi2[4]:
      value = vi1[4] > vi2[4]
    else:
      value = False
    logger.trace(f"is {v1} newer than {v2}: {value}")
    return value

  @staticmethod
  def wait_for_reboot(device_info, before_reboot_uptime=-1, reboot_only=False):
    logger.debug(f"{PURPLE}[Wait For Reboot]{NC}")
    logger.info(f"waiting for {device_info.friendly_host} to reboot[!n]")
    logger.debug("")
    time.sleep(5)
    current_version = None
    current_uptime = device_info.get_uptime(True)
    n = 1
    if not reboot_only:
      while current_uptime >= before_reboot_uptime and n < 90 or current_version is None:
        logger.trace(f"loop number: {n}")
        logger.debug(f"current_uptime: {current_uptime}")
        logger.debug(f"before_reboot_uptime: {before_reboot_uptime}")
        logger.info(f".[!n]")
        logger.debug("")
        if n == 30:
          logger.info("")
          logger.info(f"still waiting for {device_info.friendly_host} to reboot[!n]")
          logger.debug("")
        elif n == 60:
          logger.info("")
          logger.info(f"we'll wait just a little longer for {device_info.friendly_host} to reboot[!n]")
          logger.debug("")
        current_uptime = device_info.get_uptime(True)
        current_version = device_info.get_current_version(no_error_message=True)
        logger.debug(f"current_version: {current_version}")
        n += 1
    else:
      while device_info.get_uptime(True) < 3:
        time.sleep(1)  # wait 1 second before retrying.
    logger.info("")
    return current_version

  def write_network_type(self, device_info):
    logger.debug(f"{PURPLE}[Write Network Type]{NC}")
    wifi_ip = device_info.wifi_ip
    if device_info.is_homekit():
      if self.network_type == 'static':
        log_message = f"Configuring static IP to {self.ipv4_ip}..."
        value = {'config': {'wifi': {'sta': {'ip': self.ipv4_ip, 'netmask': self.ipv4_mask, 'gw': self.ipv4_gw, 'nameserver': self.ipv4_dns}}}}
      else:
        log_message = f"Configuring IP to use DHCP..."
        value = {'config': {'wifi': {'sta': {'ip': ''}}}}
      config_set_url = f'http://{wifi_ip}/rpc/Config.Set'
      logger.info(log_message)
      logger.debug(f"requests.post(url={config_set_url}, json={value}, auth=HTTPDigestAuth('{self.username}', '{self.password}'))")
      response = requests.post(url=config_set_url, json=value, auth=HTTPDigestAuth(self.username, self.password))
      logger.trace(response.text)
      if response.status_code == 200:
        logger.trace(response.text)
        logger.info(f"Saved, Rebooting...")
        logger.debug(f"requests.post(url={f'http://{wifi_ip}/rpc/SyS.Reboot'}, auth=HTTPDigestAuth('{self.username}', '{self.password}'))")
        requests.get(url=f'http://{wifi_ip}/rpc/SyS.Reboot', auth=HTTPDigestAuth(self.username, self.password))
      elif response.status_code == 401:
        self.security_help(device_info)
    else:
      if self.network_type == 'static':
        log_message = f"Configuring static IP to {self.ipv4_ip}..."
        config_set_url = f'http://{wifi_ip}/settings/sta?ipv4_method=static&ip={self.ipv4_ip}&netmask={self.ipv4_mask}&gateway={self.ipv4_gw}&dns={self.ipv4_dns}'
      else:
        log_message = f"Configuring IP to use DHCP..."
        config_set_url = f'http://{wifi_ip}/settings/sta?ipv4_method=dhcp'
      logger.info(log_message)
      logger.debug(f"requests.post(url={config_set_url})")
      response = requests.post(url=config_set_url)
      if response.status_code == 200:
        logger.trace(response.text)
        logger.info(f"Saved...")
      elif response.status_code == 401:
        self.security_help(device_info)

  def write_hap_setup_code(self, device_info):
    logger.info("Configuring HomeKit setup code...")
    value = {'code': self.hap_setup_code}
    logger.trace(f"security: {device_info.info.get('auth_en')}")
    logger.debug(f"requests.post(url='http://{device_info.wifi_ip}/rpc/HAP.Setup', auth=HTTPDigestAuth('{self.username}', '{self.password}'), json={value})")
    response = requests.post(url=f'http://{device_info.wifi_ip}/rpc/HAP.Setup', auth=HTTPDigestAuth(self.username, self.password), json={'code': self.hap_setup_code})
    if response.status_code == 200:
      logger.trace(response.text)
      logger.info(f"HAP code successfully configured.")
    elif response.status_code == 401:
      logger.info(f"{device_info.friendly_host} is password protected.")
      self.security_help(device_info)

  def write_flash(self, device_info, revert=False):
    logger.debug(f"{PURPLE}[Write Flash]{NC}")
    uptime = device_info.get_uptime(True)
    waiting_shown = False
    while uptime <= 25:  # make sure device has not just booted up.
      logger.trace("seems like we just booted, delay a few seconds")
      if waiting_shown is not True:
        logger.info("Waiting for device.")
        waiting_shown = True
      time.sleep(5)
      uptime = device_info.get_uptime(True)
    response = device_info.flash_firmware(revert)
    logger.trace(response)
    if response and response.status_code == 200:
      message = f"{GREEN}Successfully flashed {device_info.friendly_host} to {device_info.flash_fw_type_str} {device_info.flash_fw_version}{NC}"
      if revert is True:
        self.wait_for_reboot(device_info, uptime)
        reboot_check = device_info.is_stock()
        flash_fw = 'stock revert'
        message = f"{GREEN}Successfully reverted {device_info.friendly_host} to stock firmware{NC}"
      else:
        reboot_check = self.parse_version(self.wait_for_reboot(device_info, uptime))
        flash_fw = self.parse_version(device_info.flash_fw_version)
      if (reboot_check == flash_fw) or (revert is True and reboot_check is True):
        global flashed_devices, requires_upgrade
        if device_info.already_processed is False:
          flashed_devices += 1
        if requires_upgrade is True:
          requires_upgrade = 'Done'
        device_info.already_processed = True
        logger.critical(f"{message}")
      else:
        if reboot_check == '0.0.0':
          logger.info(f"{RED}Flash may have failed, please manually check version{NC}")
        else:
          global failed_flashed_devices
          failed_flashed_devices += 1
          logger.info(f"{RED}Failed to flash {device_info.friendly_host} to {device_info.flash_fw_type_str} {device_info.flash_fw_version}{NC}")
        logger.debug(f"Current: {reboot_check}")
        logger.debug(f"flash_fw_version: {flash_fw}")

  def reboot_device(self, device_info):
    logger.debug(f"{PURPLE}[Reboot Device]{NC}")
    response = device_info.preform_reboot()
    if response.status_code == 200:
      self.wait_for_reboot(device_info, reboot_only=True)
      logger.info(f"Device has rebooted...")

  def mode_change(self, device_info, mode_color):
    logger.debug(f"{PURPLE}[Color Mode Change] change from {device_info.info.get('color_mode')} to {mode_color}{NC}")
    uptime = device_info.get_uptime(True)
    response = device_info.perform_mode_change(mode_color)
    if response.status_code == 200:
      self.wait_for_reboot(device_info, uptime)
      current_color_mode = device_info.get_color_mode()
      logger.debug(f"mode_color: {mode_color}")
      logger.debug(f"current_color_mode: {current_color_mode}")
      if current_color_mode == mode_color:
        device_info.already_processed = True
        logger.critical(f"{GREEN}Successfully changed {device_info.friendly_host} to mode: {current_color_mode}{NC}")
        global requires_mode_change
        requires_mode_change = 'Done'

  def parse_info(self, device_info, hk_ver=None):
    logger.debug(f"")
    logger.debug(f"{PURPLE}[Parse Info]{NC}")

    global total_devices, flash_question
    if device_info.already_processed is False:
      total_devices += 1
      flash_question = None

    perform_flash = False
    do_mode_change = False
    download_url_request = False
    no_variant = False
    host = device_info.host
    wifi_ip = device_info.wifi_ip
    friendly_host = device_info.friendly_host
    device_id = device_info.info.get('device_id')
    model = device_info.info.get('model')
    stock_fw_model = device_info.info.get('stock_fw_model')
    color_mode = device_info.info.get('color_mode')
    current_fw_version = device_info.fw_version
    current_fw_type = device_info.info.get('fw_type')
    current_fw_type_str = device_info.fw_type_str
    flash_fw_version = device_info.flash_fw_version
    flash_fw_type_str = device_info.flash_fw_type_str
    force_version = device_info.version
    force_flash = True if force_version else False
    download_url = device_info.download_url
    device_name = device_info.info.get('device_name')
    wifi_ssid = device_info.info.get('wifi_ssid')
    wifi_rssi = device_info.info.get('wifi_rssi')
    sys_temp = device_info.info.get('sys_temp')
    sys_mode = device_info.info.get('sys_mode_str')
    uptime = datetime.timedelta(seconds=device_info.info.get('uptime', 0))
    hap_ip_conns_pending = device_info.info.get('hap_ip_conns_pending')
    hap_ip_conns_active = device_info.info.get('hap_ip_conns_active')
    hap_ip_conns_max = device_info.info.get('hap_ip_conns_max')
    battery = device_info.info.get('battery')

    logger.debug(f"flash mode: {self.flash_mode}")
    logger.debug(f"requires_upgrade: {requires_upgrade}")
    logger.debug(f"requires_mode_change: {requires_mode_change}")
    logger.debug(f"stock_fw_model: {stock_fw_model}")
    logger.debug(f"color_mode: {color_mode}")
    logger.debug(f"current_fw_version: {current_fw_type_str} {current_fw_version}")
    logger.debug(f"flash_fw_version: {flash_fw_type_str} {flash_fw_version}")
    logger.debug(f"force_flash: {force_flash}")
    logger.debug(f"force_version: {force_version}")
    logger.debug(f"download_url: {download_url}")
    logger.debug(f"not_supported: {not_supported}")

    if download_url and download_url != 'local':
      download_url_request = requests.head(download_url)
      logger.debug(f"download_url_request: {download_url_request}")
    if flash_fw_version == 'no_variant':
      flash_fw_type_str = f"{RED}{flash_fw_type_str}{NC}"
      latest_fw_label = f"{RED}No {device_info.variant} available{NC}"
      flash_fw_version = '0.0.0'
      download_url = None
      no_variant = True
    elif flash_fw_version == 'revert':
      flash_fw_type_str = 'Revert to'
      latest_fw_label = 'Stock'
    elif not download_url and device_info.local_file:
      flash_fw_type_str = f"{RED}{flash_fw_type_str}{NC}"
      latest_fw_label = f"{RED}Invalid file{NC}"
      flash_fw_version = '0.0.0'
      download_url = None
    elif not download_url or (download_url_request is not False and (download_url_request.status_code != 200 or download_url_request.headers.get('Content-Type', '') != 'application/zip')):
      flash_fw_type_str = f"{RED}{flash_fw_type_str}{NC}"
      latest_fw_label = f"{RED}Not available{NC}"
      flash_fw_version = '0.0.0'
      download_url = None
    elif hk_ver is not None:
      latest_fw_label = hk_ver
      flash_fw_type_str = "HomeKit"
    else:
      latest_fw_label = flash_fw_version

    flash_fw_newer = self.is_newer(flash_fw_version, current_fw_version)
    if not_supported is True and download_url is None:
      flash_fw_type_str = f"{RED}{flash_fw_type_str}{NC}"
      latest_fw_label = f"{RED}Not supported{NC}"
      flash_fw_version = '0.0.0'
      download_url = None
    if (not self.quiet_run or (self.quiet_run and (flash_fw_newer or (force_flash and flash_fw_version != '0.0.0')))) and requires_upgrade != 'Done':
      logger.info(f"")
      logger.info(f"{WHITE}Host: {NC}http://{host}")
      if self.info_level > 1 or device_name != friendly_host:
        logger.info(f"{WHITE}Device Name: {NC}{device_name}")
      if self.info_level > 1:
        logger.info(f"{WHITE}Model: {NC}{model}")
      if self.info_level >= 3:
        logger.info(f"{WHITE}Device ID: {NC}{device_id}")
        if sys_mode:
          logger.info(f"{WHITE}Mode: {NC}{sys_mode}")
        elif color_mode:
          logger.info(f"{WHITE}Mode: {NC}{color_mode.title()}")
        if wifi_ssid:
          logger.info(f"{WHITE}SSID: {NC}{wifi_ssid}")
        logger.info(f"{WHITE}IP: {NC}{wifi_ip}")
        if wifi_rssi:
          logger.info(f"{WHITE}RSSI: {NC}{wifi_rssi}")
        if sys_temp:
          logger.info(f"{WHITE}Sys Temp: {NC}{sys_temp}˚c{NC}")
        if str(uptime) != '0:00:00':
          logger.info(f"{WHITE}Up Time: {NC}{uptime}{NC}")
        if hap_ip_conns_max:
          if int(hap_ip_conns_pending) > 0:
            hap_ip_conns_pending = f"{RED}{hap_ip_conns_pending}{NC}"
          logger.info(f"{WHITE}HAP Connections: {NC}{hap_ip_conns_pending} / {hap_ip_conns_active} / {hap_ip_conns_max}{NC}")
        if battery:
          logger.info(f"{WHITE}Battery: {NC}{battery}%{NC}")
      if current_fw_type == self.flash_mode and current_fw_version == flash_fw_version:
        logger.info(f"{WHITE}Firmware: {NC}{current_fw_type_str} {current_fw_version} {GREEN}\u2714{NC}")
      elif current_fw_type == self.flash_mode and (flash_fw_newer or no_variant is True):
        logger.info(f"{WHITE}Firmware: {NC}{current_fw_type_str} {current_fw_version} \u279c {YELLOW}{latest_fw_label}{NC}")
      elif current_fw_type != self.flash_mode and current_fw_version != flash_fw_version:
        logger.info(f"{WHITE}Firmware: {NC}{current_fw_type_str} {current_fw_version} \u279c {YELLOW}{flash_fw_type_str} {latest_fw_label}{NC}")
      elif current_fw_type == self.flash_mode and current_fw_version != flash_fw_version:
        logger.info(f"{WHITE}Firmware: {NC}{current_fw_type_str} {current_fw_version}")

    if download_url and (force_flash or requires_upgrade is True or (current_fw_type != self.flash_mode) or (current_fw_type == self.flash_mode and flash_fw_newer)):
      global upgradeable_devices
      if device_info.already_processed is False:
        upgradeable_devices += 1

    if self.run_action == 'flash':
      action_message = "Would have been"
      keyword = None
      if download_url:
        if self.exclude and friendly_host in self.exclude:
          logger.info("Skipping as device has been excluded...")
          logger.info("")
          return 0
        elif requires_upgrade is True:
          perform_flash = True
          if self.flash_mode == 'stock':
            keyword = f"upgraded to version {flash_fw_version}"
          elif self.flash_mode == 'homekit':
            action_message = "This device needs to be"
            keyword = "upgraded to latest stock firmware version, before you can flash to HomeKit"
        elif requires_mode_change is True:
          do_mode_change = True
          action_message = "This device needs to be"
          keyword = "changed to colour mode in stock firmware, before you can flash to HomeKit"
        elif force_flash:
          perform_flash = True
          keyword = f"flashed to {flash_fw_type_str} version {flash_fw_version}"
        elif flash_fw_version == 'revert':
          perform_flash = True
          keyword = f"reverted to stock firmware"
        elif current_fw_type != self.flash_mode:
          perform_flash = True
          keyword = f"converted to {flash_fw_type_str} firmware"
        elif current_fw_type == self.flash_mode and flash_fw_newer:
          perform_flash = True
          keyword = f"upgraded from {current_fw_version} to version {flash_fw_version}"
      elif not download_url:
        if force_version:
          keyword = f"Version {force_version} is not available..."
        elif device_info.local_file:
          keyword = "Incorrect Zip File for device..."
        if keyword is not None and not self.quiet_run:
          logger.info(f"{keyword}")
        return 0

      logger.debug(f"perform_flash: {perform_flash}")
      logger.debug(f"do_mode_change: {do_mode_change}")
      if (perform_flash is True or do_mode_change is True) and self.dry_run is False and self.silent_run is False:
        if requires_upgrade is True:
          flash_message = f"{action_message} {keyword}"
        elif requires_upgrade == 'Done' and requires_mode_change is False:
          flash_message = f"Do you wish to continue to flash {friendly_host} to HomeKit firmware version {flash_fw_version}"
        elif requires_mode_change is True:
          flash_message = f"{action_message} {keyword}"
        elif flash_fw_version == 'revert':
          flash_message = f"Do you wish to revert {friendly_host} to stock firmware"
        else:
          flash_message = f"Do you wish to flash {friendly_host} to {flash_fw_type_str} firmware version {flash_fw_version}"
        if flash_question is None:
          if input(f"{flash_message} (y/n) ? ") in ('y', 'Y'):
            flash_question = True
          else:
            flash_question = False
            logger.info("Skipping Flash...")
      elif (perform_flash is True or do_mode_change is True) and self.dry_run is False and self.silent_run is True:
        flash_question = True
      elif (perform_flash is True or do_mode_change is True) and self.dry_run is True:
        logger.info(f"{action_message} {keyword}...")

      logger.debug(f"flash_question: {flash_question}")
      if flash_question is True:
        if do_mode_change is True:
          self.mode_change(device_info, 'color')
        elif flash_fw_version == 'revert':
          self.write_flash(device_info, True)
        else:
          self.write_flash(device_info)

    if device_info.is_homekit() and self.hap_setup_code:
      self.write_hap_setup_code(device_info)
    if self.network_type:
      if self.network_type == 'static':
        action_message = f"Do you wish to set your IP address to {self.ipv4_ip}"
      else:
        action_message = f"Do you wish to set your IP address to use DHCP"
      if input(f"{action_message} (y/n) ? ") in ('y', 'Y'):
        set_ip = True
      else:
        set_ip = False
      if set_ip or self.silent_run:
        self.write_network_type(device_info)
    if self.run_action == 'reboot':
      reboot = False
      if self.dry_run is False and self.silent_run is False:
        if input(f"Do you wish to reboot {friendly_host} (y/n) ? ") in ('y', 'Y'):
          reboot = True
      elif self.silent_run is True:
        reboot = True
      elif self.dry_run is True:
        logger.info(f"Would have been rebooted...")
      if reboot is True:
        self.reboot_device(device_info)

  def probe_device(self, device):
    logger.debug("")
    logger.debug(f"{PURPLE}[Probe Device] {device.get('host')}{NC}")
    logger.trace(f"device_info: {json.dumps(device, indent=2)}")

    global stock_release_info, homekit_release_info, tried_to_get_remote_homekit, tried_to_get_remote_stock, http_server_started, webserver_port, server, thread, flash_question, requires_upgrade, requires_mode_change
    requires_upgrade = False
    requires_mode_change = False
    got_info = False
    hk_flash_fw_version = None

    if self.mode == 'keep':
      self.flash_mode = device.get('fw_type')
    else:
      self.flash_mode = self.mode
    if device.get('fw_type') == 'homekit':
      device_info = HomeKitDevice(device.get('host'), device.get('username'), device.get('password'), device.get('wifi_ip'), device.get('info'))
    else:
      device_info = StockDevice(device.get('host'), device.get('username'), device.get('password'), device.get('wifi_ip'), device.get('info'))
    if not device_info.get_info():
      logger.warning("")
      logger.warning(f"{RED}Failed to lookup local information of {device.get('host')}{NC}")
    else:
      if device_info.is_stock() and self.local_file:
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
      if self.local_file:
        if device_info.is_homekit() and device_info.parse_local_file():
          got_info = True
        elif device_info.is_stock():
          if not stock_release_info and not tried_to_get_remote_stock:
            stock_release_info = self.get_release_info('stock')
            tried_to_get_remote_stock = True
          if stock_release_info:
            device_info.update_stock(stock_release_info)
            if device_info.info.get('device', {}).get('type', '') == 'SHRGBW2' and device_info.info.get('color_mode') == 'white':
              requires_mode_change = True
            if device_info.fw_version == '0.0.0' or self.is_newer(device_info.flash_fw_version, device_info.fw_version):
              requires_upgrade = True
              got_info = True
            elif device_info.parse_local_file():
              got_info = True
      else:
        if device_info.is_stock() and self.flash_mode == 'homekit':
          if not stock_release_info and not tried_to_get_remote_stock:
            stock_release_info = self.get_release_info('stock')
            tried_to_get_remote_stock = True
          if not homekit_release_info and not tried_to_get_remote_homekit:
            homekit_release_info = self.get_release_info('homekit')
            tried_to_get_remote_homekit = True
          if stock_release_info and homekit_release_info:
            device_info.update_homekit(homekit_release_info)
            if device_info.download_url:
              download_url_request = requests.head(device_info.download_url)
              logger.debug(f"download_url_request: {download_url_request}")
              hk_flash_fw_version = device_info.flash_fw_version
            device_info.update_stock(stock_release_info)
            if device_info.info.get('device', {}).get('type', '') == 'SHRGBW2' and device_info.info.get('color_mode') == 'white':
              requires_mode_change = True
            if device_info.fw_version == '0.0.0' or self.is_newer(device_info.flash_fw_version, device_info.fw_version):
              requires_upgrade = True
              got_info = True
            else:
              device_info.update_homekit(homekit_release_info)
              got_info = True
        elif self.flash_mode == 'revert':
            device_info.update_homekit()
            got_info = True
        elif self.flash_mode == 'homekit':
          if not homekit_release_info and not tried_to_get_remote_homekit and not self.local_file:
            homekit_release_info = self.get_release_info('homekit')
            tried_to_get_remote_homekit = True
          if homekit_release_info:
            device_info.update_homekit(homekit_release_info)
            got_info = True
        elif self.flash_mode == 'stock':
          if not stock_release_info and not tried_to_get_remote_stock:
            stock_release_info = self.get_release_info('stock')
            tried_to_get_remote_stock = True
          if stock_release_info:
            device_info.update_stock(stock_release_info)
            got_info = True
      device_version = self.parse_version(device_info.info.get('version', '0.0.0'))
      if got_info and device_info.info.get('fw_type') == "homekit" and float(f"{device_version[0]}.{device_version[1]}") < 2.1:
        logger.error(f"{WHITE}Host: {NC}{device_info.host}")
        logger.error(f"Version {device_info.info.get('version')} is to old for this script,")
        logger.error(f"please update via the device webUI.")
        logger.error("")
      elif got_info:
        self.parse_info(device_info, hk_flash_fw_version)
        if self.run_action == 'flash' and (requires_upgrade in ('Done', True) or requires_mode_change in ('Done', True)) and flash_question is True:
          if requires_mode_change is True:
            device_info.get_info()
            self.parse_info(device_info)
          device_info.get_info()
          if device_info.flash_fw_version != '0.0.0' and (not self.is_newer(device_info.flash_fw_version, device_info.fw_version) or (device_info.is_stock() and self.flash_mode == 'homekit')):
            if self.local_file:
              device_info.parse_local_file()
            else:
              device_info.update_homekit(homekit_release_info)
            self.parse_info(device_info)

  def stop_scan(self):
    if self.listener is not None:
      while True:
        try:
          self.listener.queue.get_nowait()
        except queue.Empty:
          self.zc.close()
          break

  def exit_app(self):
    logger.info(f"")
    if self.run_action == 'flash':
      if failed_flashed_devices > 0:
        logger.info(f"{GREEN}Devices found: {total_devices} Upgradeable: {upgradeable_devices} Flashed: {flashed_devices}{NC} {RED}Failed: {failed_flashed_devices}{NC}")
      else:
        logger.info(f"{GREEN}Devices found: {total_devices} Upgradeable: {upgradeable_devices} Flashed: {flashed_devices}{NC}")
    else:
      if total_devices > 0:
        logger.info(f"{GREEN}Devices found: {total_devices} Upgradeable: {upgradeable_devices}{NC}")
    if self.log_filename:
      logger.info(f"Log file created: {self.log_filename}")

  def is_fw_type(self, fw_type):
    return fw_type.lower() in self.fw_type_filter.lower() or self.fw_type_filter == 'all'

  def is_model_type(self, fw_model):
    return self.model_type_filter is not None and self.model_type_filter.lower() in fw_model.lower() or self.model_type_filter == 'all'

  def is_device_name(self, device_name):
    return device_name is not None and self.device_name_filter is not None and self.device_name_filter.lower() in device_name.lower() or self.device_name_filter == 'all'

  def manual_hosts(self):
    global total_devices
    device_info = None
    logger.debug(f"{PURPLE}[Manual Hosts]{NC}")
    logger.info(f"{WHITE}Looking for Shelly devices...{NC}")
    for host in self.hosts:
      logger.debug(f"")
      logger.debug(f"{PURPLE}[Manual Hosts] action {host}{NC}")
      (username, password) = main.get_security_data(host)
      n = 1
      while n <= self.timeout:
        device_info = Device(host, username, password, no_error_message=True)
        time.sleep(1)
        n += 1
        if device_info.info:
          break
      if n > self.timeout:
        device_info = Device(host, username, password)
      if device_info.info and device_info.info == 401:
        self.security_help(device_info)
      elif device_info.info:
        device = {'host': device_info.host, 'username': device_info.username, 'password': device_info.password, 'wifi_ip': device_info.wifi_ip, 'fw_type': device_info.info.get('fw_type'), 'info': device_info.info}
        self.probe_device(device)
    if http_server_started and server is not None:
      logger.trace("Shutting down webserver")
      server.shutdown()
      thread.join()

  def device_scan(self):
    global total_devices
    logger.debug(f"{PURPLE}[Device Scan] automatic scan{NC}")
    logger.info(f"{WHITE}Scanning for Shelly devices...{NC}")
    self.zc = zeroconf.Zeroconf()
    self.listener = ServiceListener()
    zeroconf.ServiceBrowser(zc=self.zc, type_='_http._tcp.local.', listener=self.listener)
    total_devices = 0
    while True:
      try:
        device_info = self.listener.queue.get(timeout=self.timeout)
      except queue.Empty:
        break
      logger.debug(f"")
      logger.debug(f"{PURPLE}[Device Scan] action queue entry{NC}")
      if device_info.info and device_info.info == 401:
        logger.info("")
        self.security_help(device_info, 'Scanner')
      elif device_info.info:
        fw_model = device_info.info.get('model') if device_info.is_homekit() else device_info.shelly_model(device_info.info.get('device').get('type'))[0]
        if self.is_fw_type(device_info.info.get('fw_type')) and self.is_model_type(fw_model) and self.is_device_name(device_info.info.get('device_name')):
          device = {'host': device_info.host, 'wifi_ip': device_info.wifi_ip, 'fw_type': device_info.info.get('fw_type'), 'info': device_info.info}
          self.probe_device(device)


if __name__ == '__main__':
  main = Main()
  sys.exit(main.run_app())
