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

This script will probe for any shelly device on the network, and it will
attempt to update them to the latest firmware version available.
This script will not flash any firmware to a device that is not already on a
version of this firmware, if you are looking to flash your device from stock
or any other firmware please follow instructions here:
https://github.com/mongoose-os-apps/shelly-homekit/wiki

usage: flash-shelly.py [-h] [--app-version] [-f] [-l] [-m {homekit,keep,revert}] [-i {1,2,3}] [-ft {homekit,stock,all}] [-mt MODEL_TYPE_FILTER] [-dn DEVICE_NAME_FILTER] [-a] [-q] [-e [EXCLUDE ...]] [-n]
                       [-y] [-V VERSION] [--force] [--variant VARIANT] [--local-file LOCAL_FILE] [-c HAP_SETUP_CODE] [--ip-type {dhcp,static}] [--ip IPV4_IP] [--gw IPV4_GW] [--mask IPV4_MASK]
                       [--dns IPV4_DNS] [-v {0,1,2,3,4,5}] [--timeout TIMEOUT] [--log-file LOG_FILENAME] [--reboot] [--user USER] [--password PASSWORD] [--config CONFIG] [--save-config SAVE_CONFIG]
                       [--save-defaults]
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
                        Limit scan to model type (dimmer, rgbw2, shelly1, etc.).
  -dn DEVICE_NAME_FILTER, --device-name DEVICE_NAME_FILTER
                        Limit scan to include term in device name.
  -a, --all             Run against all the devices on the network.
  -q, --quiet           Only include upgradeable shelly devices.
  -e [EXCLUDE ...], --exclude [EXCLUDE ...]
                        Exclude hosts from found devices.
  -n, --assume-no       Do a dummy run through.
  -y, --assume-yes      Do not ask any confirmation to perform the flash.
  -V VERSION, --version VERSION
                        Flash a particular version.
  --force               Force a flash.
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
  --timeout TIMEOUT     Scan: Time in seconds to wait after last detected device before quitting. Manual: Time in seconds to keep trying to connect.
  --log-file LOG_FILENAME
                        Create output log file with chosen filename.
  --reboot              Preform a reboot of the device.
  --user USER           Enter username for device security (default = admin).
  --password PASSWORD   Enter password for device security.
  --config CONFIG       Load options from config file.
  --save-config SAVE_CONFIG
                        Save current options to config file.
  --save-defaults       Save current options as new defaults.
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

arch = platform.system()
app_ver = '3.0.0'  # beta7
config_file = '.cfg.yaml'
defaults_config_file = 'flash-shelly.cfg.yaml'
security_file = 'flash-shelly.auth.yaml'
stock_info_url = 'https://api.shelly.cloud/files/firmware'
homekit_info_url = 'https://rojer.me/files/shelly/update.json'

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


class ServiceListener:  # handle device(s) found by DNS scanner.
  def __init__(self):
    self.queue = queue.Queue()

  def add_to_queue(self, host, username, password, wifi_ip, fw_type, auth):
    if fw_type == 'homekit':
      self.queue.put(HomeKitDevice(host, username, password, wifi_ip, fw_type, auth))
    elif fw_type == 'stock':
      self.queue.put(StockDevice(host, username, password, wifi_ip, fw_type, auth))

  def add_service(self, zc, service_type, device):
    host = device.replace(f'{service_type}', 'local')
    info = zc.get_service_info(service_type, device)
    auth = False
    if info:
      fw_type = None
      wifi_ip = socket.inet_ntoa(info.addresses[0])
      properties = info.properties
      properties = {y.decode('UTF-8'): properties.get(y).decode('UTF-8') for y in properties.keys()}
      logger.debug(f"[Device Scan] found device: {host}, IP address: {wifi_ip}")
      logger.trace(f"[Device Scan] info: {info}")
      logger.trace(f"[Device Scan] properties: {properties}")
      if properties.get('arch') in ('esp8266', 'esp32'):  # this detects if esp device.
        if properties.get('auth_en') and properties.get('fw_type') == 'homekit':  # this detects if homekit device (requires fw >= 2.9.1).
          auth = bool(int(properties.get('auth_en')))
          fw_type = properties.get('fw_type')
        elif properties.get('fw_version') == '1.0' and properties.get('id').startswith('shelly'):  # this detects stock device.
          fw_type = 'stock'
        else:  # add fallback to legacy detection, # TODO remove when 2.9.x when more mainstream.
          device = Detection(host, 'admin', '', wifi_ip)
          if device and device.fw_type is not None:
            fw_type = device.fw_type
            auth = device.auth
      if fw_type:
        (username, password) = main.get_security_data(host)
        self.add_to_queue(host, username, password, wifi_ip, fw_type, auth)
        logger.debug(f"[Device Scan] added: {host}, IP address: {wifi_ip}, FW Type: {fw_type}, Security Enabled: {auth} to queue")
      logger.debug("")

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


class Detection:
  def __init__(self, host, username=None, password=None, wifi_ip=None, fw_type=None, auth=False, error_message=True):
    self.host = host
    self.wifi_ip = wifi_ip
    self.username = username
    self.password = password
    self.fw_type = fw_type
    self.auth = auth
    if self.fw_type is None:  # run detector for manual hosts.
      self.is_shelly(error_message)

  def is_shelly(self, error_message):
    if self.is_host_reachable(self.host, error_message):
      for url in [f'http://{self.wifi_ip}/settings', f'http://{self.wifi_ip}/rpc/Shelly.GetInfoExt']:
        try:
          response = requests.get(url)
        except ConnectionError:
          return False
        logger.trace(f"RESPONSE: {response}")
        if response is not None and response.status_code in (200, 401):
          if 'GetInfoExt' in url:
            self.fw_type = "homekit"
          elif 'settings' in url:
            self.fw_type = "stock"
          if response.status_code == 401:
            self.auth = True
          break
      if self.fw_type is not None:
        return self.fw_type
      logger.debug(f"")
      logger.debug(f"{WHITE}Host:{NC} {self.host} {RED}is not a Shelly device.{NC}")
    return False

  def is_host_reachable(self, host, error_message=True):  # check if host is reachable
    self.host = main.host_check(host)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(1)
    host_is_reachable = False
    n = 1
    while n <= (main.timeout/4):  # do loop to keep retrying for timeout
      try:
        # use manual IP supplied
        ipaddress.IPv4Address(host)
        test_host = host
        self.host = host
      except ipaddress.AddressValueError:
        # if not IP use hostname supplied.
        test_host = self.host
      try:  # do actual online check.
        sock.connect((test_host, 80))
        self.wifi_ip = socket.gethostbyname(test_host)  # resolve IP from manual hostname
        logger.debug(f"Device: {test_host} is Online")
        host_is_reachable = True
      except socket.error:
        host_is_reachable = False
      sock.close()
      n += 1
      if host_is_reachable:
        break
    if not host_is_reachable and error_message:
      logger.error(f"")
      logger.error(f"{RED}Could not connect to host: {self.host}{NC}")
    return host_is_reachable


class Device(Detection):
  def __init__(self, host, username=None, password=None, wifi_ip=None, fw_type=None, auth=False, error_message=True):
    super().__init__(host, username, password, wifi_ip, fw_type, auth, error_message)
    self.friendly_host = self.host.replace('.local', '')
    self.info = {}
    self.variant = main.variant
    self.version = main.version
    self.local_file = main.local_file
    self.flash_fw_version = '0.0.0'
    self.flash_fw_type_str = None
    self.download_url = None
    self.already_processed = False
    self.not_supported = False
    self.no_variant = False

  def load_device_info(self, force_update=False, error_message=True):
    fw_info = None
    status = None
    authentication = ''
    if force_update:
      self.is_shelly(error_message)
    try:  # we need an exception as device could be rebooting.
      if self.fw_type == 'stock':
        # latest stock firmware return 'updating' `/status` status dict when updating.
        if self.auth:
          authentication = (self.username, self.password)
        status_check = requests.get(f'http://{self.wifi_ip}/status', auth=authentication, timeout=3)
        if status_check.status_code == 200:
          status = json.loads(status_check.content)
          if status.get('status', '') != '':  # we use this to see if a device is updating, this should return '' unless updating.
            self.info = {}
            return self.info
        device_url = f'http://{self.wifi_ip}/settings'
      else:
        if self.auth:
          authentication = HTTPDigestAuth(self.username, self.password)
        device_url = f'http://{self.wifi_ip}/rpc/Shelly.GetInfoExt'
      fw_info = requests.get(device_url, auth=authentication, timeout=3)
      logger.trace(f"Device URL: {device_url}")
      logger.trace(f"status code: {fw_info.status_code}")
      logger.trace(f"security: {self.auth}")
      if fw_info.status_code == 401:  # Unauthorized
        logger.debug("Invalid user or password.")
        self.info = 401
        return 401, ''
    except Exception:
      pass  # ignore exception as device could be rebooting.
    return fw_info, status

  def get_device_info(self, force_update=False, error_message=True, scanner='Manual'):
    if not self.info or force_update:# only get information if required or force update.
      info = {}
      (fw_info, status) = self.load_device_info(force_update, error_message)
      if fw_info is not None:
        if fw_info == 401:
          logger.info("")
          main.security_help(self, scanner)
        elif fw_info.status_code == 200 and fw_info.content is not None:
          info = json.loads(fw_info.content)
          info['device_name'] = info.get('name')
          if status is not None:
            info['status'] = status
          logger.trace(f"Device Info: {json.dumps(info, indent=2)}")
          if (self.is_stock() and info.get('login').get('enabled')) or (self.is_homekit() and info.get('auth_en', 'false')):
            # secure connection was successful, save credentials to security file.
            main.save_security(self)
      if not info:
        logger.debug(f"{RED}Failed to lookup local information of {self.host}{NC}")
      self.info = info
    return self.info

  def is_homekit(self):
    return self.fw_type == 'homekit'

  def is_stock(self):
    return self.fw_type == 'stock'

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
    return v.group('ver') if v is not None else '0.0.0'

  def get_current_version(self, error_message=True):  # used when flashing between firmware versions.
    version = None
    if not self.get_device_info(True, error_message):
      return None
    if self.is_homekit():
      version = self.info.get('version', '0.0.0')
    elif self.is_stock():
      version = self.parse_stock_version(self.info.get('fw', '0.0.0'))
    return version

  def get_uptime(self, error_message=True):
    uptime = -1
    if not self.get_device_info(True, error_message):
      logger.trace("get_uptime: -1")
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
               'SHPLG-U1': ['ShellyPlugUS', 'shelly-plug-us1'],
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
               'SHIX3-1': ['ShellyI3', 'ix3'],
               'SNSW-001X16EU': ['ShellyPlus1', 'Plus1'],
               'SNSW-001P16EU': ['ShellyPlus1PM', 'Plus1PM'],
               'SNSN-0024X': ['ShellyPlusI4', 'PlusI4']
               }
    return options.get(stock_fw_model, [stock_fw_model, stock_fw_model])

  def parse_local_file(self):
    if not os.path.exists(self.local_file) or not self.local_file.endswith('.zip'):
      return self._local_file_no_data("File does not exist.")
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
    if manifest_file is None:
      return self._local_file_no_data("Invalid file.")
    manifest_version = manifest_file.get('version', '0.0.0')
    manifest_name = manifest_file.get('name')
    if manifest_version == '1.0':
      self.version = self.parse_stock_version(manifest_file.get('build_id', '0.0.0'))
      self.flash_fw_type_str = "Stock"
      main.flash_mode = 'stock'
    else:
      self.version = manifest_version
      self.flash_fw_type_str = "HomeKit"
      main.flash_mode = 'homekit'
    logger.debug(f"Mode: {self.info.get('fw_type_str')} To {self.flash_fw_type_str}")
    self.flash_fw_version = self.version
    if self.is_homekit():
      self.download_url = 'local'
    else:
      self.download_url = f'http://{local_ip}:{main.webserver_port}/{self.local_file}'
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

  def _local_file_no_data(self, arg0):
    logger.info(f'{arg0}')
    self.flash_fw_version = '0.0.0'
    self.download_url = None
    return False

  @staticmethod
  def get_release_info(info_type):  # handle loading of release info for both stock and homekit.
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

  def parse_homekit_release_info(self):
    self.flash_fw_type_str = 'HomeKit'
    logger.debug(f"Mode: {self.info.get('fw_type_str')} To {self.flash_fw_type_str}")
    if self.version:
      self.flash_fw_version = self.version
      self.download_url = f"http://rojer.me/files/shelly/{self.version}/shelly-homekit-{self.info.get('model')}.zip"
    elif main.revert_to_stock is True:
      self.flash_fw_version = 'revert'
      self.download_url = f"http://rojer.me/files/shelly/stock/{self.info.get('stock_fw_model')}.zip"
    else:
      if not main.homekit_release_info:
        main.homekit_release_info = self.get_release_info('homekit')
      if main.homekit_release_info:
        for i in main.homekit_release_info:
          if self.variant and self.variant not in i[1].get('version', '0.0.0'):
            self.no_variant = True
            return
          re_search = r'-*' if self.variant else i[0]
          if re.search(re_search, self.info.get('fw_version')):
            self.flash_fw_version = i[1].get('version', '0.0.0')
            self.download_url = i[1].get('urls', {}).get(self.info.get('model'))
            break
        if self.download_url is None:
          self.not_supported = True
          self.flash_fw_version = '0.0.0'

  def parse_stock_release_info(self):
    self.flash_fw_type_str = 'Stock'
    stock_fw_model = self.info.get('stock_fw_model')
    color_mode = self.info.get('color_mode')
    logger.debug(f"Mode: {self.info.get('fw_type_str')} To {self.flash_fw_type_str}")
    if not self.version or main.flash_mode == 'homekit':  # allow checking of the latest stock version, when force version to homekit
      if stock_fw_model == 'SHRGBW2-color':  # we need to use real stock model here
        stock_fw_model = 'SHRGBW2'
      if not main.stock_release_info:
        main.stock_release_info = self.get_release_info('stock')
      if main.stock_release_info:
        stock_model_info = main.stock_release_info.get('data', {}).get(stock_fw_model)
        if self.variant == 'beta':
          self.flash_fw_version = self.parse_stock_version(stock_model_info.get('beta_ver', '0.0.0'))
          self.download_url = stock_model_info.get('beta_url')
        else:
          self.flash_fw_version = self.parse_stock_version(stock_model_info.get('version', '0.0.0'))
          self.download_url = stock_model_info.get('url')
    else:
      self.flash_fw_version = self.version
      self.download_url = f'http://archive.shelly-tools.de/version/v{self.version}/{stock_fw_model}.zip'
    if stock_fw_model == 'SHRGBW2' and self.download_url and not self.version and color_mode and not self.local_file:
      self.download_url = self.download_url.replace('.zip', f'-{color_mode}.zip')

  def write_hap_setup_code(self):  # handle saving HomeKit setup code.
    logger.info(f"Configuring HomeKit setup code: {main.hap_setup_code}")
    value = {'code': main.hap_setup_code}
    logger.trace(f"security: {self.info.get('auth_en')}")
    logger.debug(f"requests.post(url='http://{self.wifi_ip}/rpc/HAP.Setup', auth=HTTPDigestAuth('{main.username}', '{main.password}'), json={value})")
    response = requests.post(url=f'http://{self.wifi_ip}/rpc/HAP.Setup', auth=HTTPDigestAuth(main.username, main.password), json={'code': main.hap_setup_code})
    if response.status_code == 200:
      logger.trace(response.text)
      logger.info("HAP code successfully configured.")


class HomeKitDevice(Device):
  def get_info(self, force_update=False, error_message=True, scanner='Manual'):
    if not self.get_device_info(force_update, error_message, scanner):
      return False
    self.info['fw_type_str'] = 'HomeKit'
    self.info['fw_version'] = self.info.get('version', '0.0.0')
    if self.info.get('stock_fw_model') is None:  # TODO remove when 2.9.x is mainstream.
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

  def write_firmware(self):
    if self.local_file:
      logger.debug(f"Local file: {self.local_file}")
      my_file = open(self.local_file, 'rb')
      logger.info(f"Now Flashing {self.flash_fw_type_str} {self.flash_fw_version}")
      files = {'file': ('shelly-flash.zip', my_file)}
    else:
      logger.info("Downloading Firmware...")
      logger.debug(f"Remote Download URL: {self.download_url}")
      my_file = requests.get(self.download_url)
      if main.revert_to_stock is True:
        logger.info("Now Reverting to stock firmware")
      else:
        logger.info(f"Now Flashing {self.flash_fw_type_str} {self.flash_fw_version}")
      files = {'file': ('shelly-flash.zip', my_file.content)}
    logger.debug(f"requests.post(url='http://{self.wifi_ip}/update', auth=HTTPDigestAuth('{self.username}', '{self.password}'), files=files)")
    response = requests.post(url=f'http://{self.wifi_ip}/update', auth=HTTPDigestAuth(self.username, self.password), files=files)
    logger.trace(response.text)
    logger.trace(f"response.status_code: {response.status_code}")
    return response

  def do_reboot(self):
    logger.info("Rebooting...")
    logger.debug(f"requests.post(url='http://{self.wifi_ip}/rpc/SyS.Reboot', auth=HTTPDigestAuth('{self.username}', '{self.password}'))")
    response = requests.get(url=f'http://{self.wifi_ip}/rpc/SyS.Reboot', auth=HTTPDigestAuth(self.username, self.password))
    logger.trace(response.text)
    return response

  def write_network_type(self):  # handle changing of Wi-Fi connection type DHCP or StaticC IP.
    logger.debug(f"{PURPLE}[Write Network Type]{NC}")
    if main.network_type == 'static':
      log_message = f"Configuring static IP to {main.ipv4_ip}..."
      value = {'config': {'wifi': {'sta': {'ip': main.ipv4_ip, 'netmask': main.ipv4_mask, 'gw': main.ipv4_gw, 'nameserver': main.ipv4_dns}}}}
    else:
      log_message = "Configuring IP to use DHCP..."
      value = {'config': {'wifi': {'sta': {'ip': ''}}}}
    config_set_url = f'http://{self.wifi_ip}/rpc/Config.Set'
    logger.info(log_message)
    logger.debug(f"requests.post(url={config_set_url}, json={value}, auth=HTTPDigestAuth('{main.username}', '{main.password}'))")
    response = requests.post(url=config_set_url, json=value, auth=HTTPDigestAuth(main.username, main.password))
    logger.trace(response.text)
    if response.status_code == 200:
      logger.trace(response.text)
      logger.info("Saved, Rebooting...")
      logger.debug(f"requests.post(url=http://{self.wifi_ip}/rpc/SyS.Reboot, auth=HTTPDigestAuth('{main.username}', '{main.password}'))")
      requests.get(url=f'http://{self.wifi_ip}/rpc/SyS.Reboot', auth=HTTPDigestAuth(main.username, main.password))


class StockDevice(Device):
  def get_info(self, force_update=False, error_message=True, scanner='Manual'):
    if not self.get_device_info(force_update, error_message, scanner):
      return False
    self.info['fw_type_str'] = 'Stock'
    self.info['fw_version'] = self.parse_stock_version(self.info.get('fw', '0.0.0'))  # current firmware version
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

  def get_color_mode(self, error_message=True):  # used when flashing between firmware versions.
    if not self.get_info(True, error_message):
      return None
    return self.info.get('mode')

  def write_firmware(self):
    logger.info(f"Now Flashing {self.flash_fw_type_str} {self.flash_fw_version}")
    download_url = self.download_url.replace('https', 'http')
    if self.local_file:
      logger.debug(f"Local file: {self.local_file}")
      logger.debug(f"Local Download URL: {download_url}")
    else:
      logger.debug(f"Remote Download URL: {download_url}")
    if self.info.get('fw_version') == '0.0.0':
      flash_url = f'http://{self.wifi_ip}/ota?update=true'
    else:
      flash_url = f'http://{self.wifi_ip}/ota?url={download_url}'
    logger.debug(f"requests.get(url={flash_url}, auth=('{self.username}', '{self.password}')")
    response = requests.get(url=flash_url, auth=(self.username, self.password))
    logger.trace(response.text)
    return response

  def do_reboot(self):
    logger.info('Rebooting...')
    logger.debug(f"requests.post(url=http://{self.wifi_ip}/reboot, auth=('{self.username}', '{self.password}'d)")
    response = requests.get(url=f'http://{self.wifi_ip}/reboot', auth=(self.username, self.password))
    logger.trace(response.text)
    return response

  def do_mode_change(self, mode_color):
    logger.info("Performing mode change...")
    logger.debug(f"requests.post(url=http://{self.wifi_ip}/settings/?mode={mode_color}, auth=('{self.username}', '{self.password}'))")
    response = requests.get(url=f'http://{self.wifi_ip}/settings/?mode={mode_color}', auth=(self.username, self.password))
    logger.trace(response.text)
    while response.status_code == 400:
      time.sleep(2)
      try:
        response = requests.get(url=f'http://{self.wifi_ip}/settings/?mode={mode_color}', auth=(self.username, self.password))
        logger.trace(f"response.status_code: {response.status_code}")
      except ConnectionResetError:
        pass
    return response

  def write_network_type(self):  # handle changing of Wi-Fi connection type DHCP or StaticC IP.
    logger.debug(f"{PURPLE}[Write Network Type]{NC}")
    if main.network_type == 'static':
      log_message = f"Configuring static IP to {main.ipv4_ip}..."
      config_set_url = f'http://{self.wifi_ip}/settings/sta?ipv4_method=static&ip={main.ipv4_ip}&netmask={main.ipv4_mask}&gateway={main.ipv4_gw}&dns={main.ipv4_dns}'
    else:
      log_message = "Configuring IP to use DHCP..."
      config_set_url = f'http://{self.wifi_ip}/settings/sta?ipv4_method=dhcp'
    logger.info(log_message)
    logger.debug(f"requests.post(url={config_set_url})")
    response = requests.post(url=config_set_url)
    if response.status_code == 200:
      logger.trace(response.text)
      logger.info("Saved...")


class Main:
  def __init__(self):
    self.parser = None
    self.http_server_started = False
    self.webserver_port = 8381
    self.server = None
    self.thread = None
    self.stock_release_info = {}
    self.homekit_release_info = {}
    self.requires_upgrade = None
    self.requires_color_mode_change = None
    self.total_devices = 0
    self.upgradeable_devices = 0
    self.flashed_devices = 0
    self.failed_flashed_devices = 0
    self.flash_question = None
    self.hosts = None
    self.username = None
    self.password = None
    self.run_action = None
    self.timeout = None
    self.log_filename = None
    self.list = None
    self.do_all = None
    self.reboot = None
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
    self.revert_to_stock = False
    self.force = None
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
    self.security_data = {}
    self.tmp_flags = None
    self.defaults = {
      'device_name_filter': 'all',
      'do_all': False,
      'dry_run': False,
      'exclude': '',
      'fw_type_filter': 'all',
      'hap_setup_code': '',
      'hosts': '',
      'info_level': 2,
      'ipv4_dns': '',
      'ipv4_gw': '',
      'ipv4_ip': '',
      'ipv4_mask': '',
      'list': False,
      'local_file': '',
      'log_filename': '',
      'mode': 'homekit',
      'model_type_filter': 'all',
      'network_type': '',
      'quiet_run': False,
      'reboot': False,
      'silent_run': False,
      'timeout': 20,
      'variant': '',
      'verbose': 3,
      'version': '',
      'force': False,
      'user': 'admin'
    }

  def check_fw(self, device, version):
    # check current firmware meets minimum script requirements.
    device_version = self.parse_version(device.info.get('version', '0.0.0'))
    if device.fw_type == "homekit" and float(f"{device_version[0]}.{device_version[1]}") < version:
      logger.error(f"{WHITE}Host: {NC}{device.host}")
      logger.error(f"Version {device.info.get('version')} is to old for this script,")
      logger.error("please update via the device webUI.")
      logger.error("")
      return False
    return True

  @staticmethod
  def host_check(host):  # check manual supplied host(s) for IP or host name, if name add '.local' if required.
    host_check = re.search(r'\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}', host)
    return f'{host}.local' if '.local' not in host and not host_check else host

  @staticmethod
  def load_config(profile):  # load defaults and profiles from config file.
    logger.trace(f"load_config: {profile}")
    data = {}
    if profile == 'defaults':
      load_file = defaults_config_file
    else:
      load_file = f'{profile}{config_file}'
    if os.path.exists(load_file):
      logger.trace(f"Loading configuration {load_file}")
      with open(load_file) as fp:
        data = yaml.load(fp, Loader=yaml.FullLoader)
      logger.trace(f"config: {yaml.dump(data, indent=2)}")
    defaults_data = data if data is not None else {}
    defaults = defaults_data.get(profile)
    if defaults is not None and defaults.get('hosts'):
      defaults['hosts'] = defaults.get('hosts').split()
    return defaults

  def save_config(self, args):  # saves defaults and profiles to config file.
    logger.trace("save_config")
    if args.get('save_defaults') is True:
      profile = 'defaults'
      save_file = defaults_config_file
    else:
      profile = args.get('save_config')
      save_file = f"{args.get('save_config')}{config_file}"
    y = {}
    if not args.get('hosts'):
      args.pop('hosts')
    for k in args:
      if k in ('config', 'save_config', 'save_defaults', 'app_version', 'flash', 'user', 'password'):
        continue
      if args[k] == self.parser.get_default(k):
        continue
      if k == 'hosts' and args[k]:
        y[k] = re.sub(r"[\[\]',]", "", str(args[k]))
      else:
        y[k] = args[k]
    defaults_data = {profile: y}
    logger.trace(f"{'defaults_data'}: {yaml.dump(defaults_data, indent=2)}")
    with open(save_file, 'w') as yaml_file:
      yaml.dump(defaults_data, yaml_file)
    logger.info(f"Saved configuration {profile} to {save_file}")
    sys.exit(0)

  def load_security(self):  # load user and password from security file.
    data = None
    logger.trace("load_security")
    if os.path.exists(security_file):
      with open(security_file) as fp:
        data = yaml.load(fp, Loader=yaml.FullLoader)
      logger.trace(f"security: {yaml.dump(data, indent=2)}")
    self.security_data = data if data is not None else {}

  def get_security_data(self, host):
    logger.trace("get_security_data")
    host = self.host_check(host)
    logger.debug(f"[Security] {host}")
    if not self.password and self.security_data.get(host):
      sec_type = 'file'
      username = self.security_data.get(host).get('user')
      password = self.security_data.get(host).get('password')
    else:
      sec_type = 'commandline'
      username = self.username
      password = self.password
    logger.debug(f"[Security] username: {username} from {sec_type}")
    logger.debug(f"[Security] password: {password} from {sec_type}")
    return username, password

  def save_security(self, device):  # save user and password that was supplied from commandline to file.
    logger.trace("save_security")
    save_security = False
    current_user = self.security_data.get(device.host, {}).get('user')
    current_password = self.security_data.get(device.host, {}).get('password')
    data = {'user': device.username, 'password': device.password}
    if self.security_data.get(device.host) and not device.password:  # remove security information if password is empty.
      save_security = True
      self.security_data.pop(device.host)
      logger.debug(f"{WHITE}Security:{NC} Removing data for {device.host}[!n]")
    elif self.security_data.get(device.host) is None and device.password is not None:
      # only save security information if user and password do not exist or changed.
      if (not current_user or not current_password) or (current_user != device.username or current_password != device.password):
        save_security = True
        logger.debug(f"")
        logger.debug(f"{WHITE}Security:{NC} Saving data for {device.host}[!n]")
        self.security_data[device.host] = data
    if save_security:
      logger.trace(yaml.dump(self.security_data.get(device.host)))
      with open(security_file, 'w') as yaml_file:
        yaml.dump(self.security_data, yaml_file)
    if os.path.exists(security_file) and not self.security_data:  # delete security data if empty.
      os.remove(security_file)

  def security_help(self, device, mode='Manual'):  # show security help information.
    logger.info(f"{WHITE}Host: {NC}{device.host} {RED}is password protected{NC}")
    if self.security_data:
      if self.security_data.get(device.host) is None:  # no security info supplied in config file.
        if mode == 'Manual' and self.password:  # manual host not found in config file, but with incorrect password supplied by commandline.
          logger.info("Invalid user or password, please check supplied details are correct.")
          logger.info(f"username: {self.username}")
          logger.info(f"password: {self.password}")
        elif mode == 'Manual':  # manual host not found in config file.
          logger.info(f"{device.host} is not found in '{security_file}' config file,")
          logger.info("please add or use commandline args --user | --password")
        else:  # scanner host not found in config file.
          logger.info(f"{device.host} is not found in '{security_file}' config file,")
          logger.info(f"'{security_file}' security file is required in scanner mode.")
          logger.info(f"unless all devices use same password.{NC}")
      else:  # authentication on device, but security info found in config file is invalid.
        logger.info(f"Invalid user or password found in '{security_file}',please check supplied details are correct.")
        logger.info(f"username: {self.security_data.get(device.host).get('user')}")
        logger.info(f"password: {self.security_data.get(device.host).get('password')}{NC}")
    else:  # no security info supplied.
      logger.info(f"Please use either command line security (--user | --password) or '{security_file}'")
      logger.info(f"for '{security_file}', create a file called '{security_file}' in tools folder")
      logger.info(f"{WHITE}Example {security_file}:{NC}")
      example_dict = {"shelly-AF0183.local": {"user": "admin", "password": "abc123"}}
      logger.info(f"{YELLOW}{yaml.dump(example_dict, indent=2)}{NC}[!n]")

  @staticmethod
  def setup_logger(args):
    # setup output logging.
    sh = MStreamHandler()
    sh.setFormatter(logging.Formatter('%(message)s'))
    logger.addHandler(sh)
    sh.setLevel(log_level[args.get('verbose')])
    if args.get('verbose') >= 4:
      args['info_level'] = 3
    if args.get('log_filename'):
      args['verbose'] = 5
      args['info_level'] = 3
      fh = MFileHandler(args.get('log_filename'), mode='w', encoding='UTF-8')
      fh.setFormatter(logging.Formatter('%(asctime)s %(levelname)s %(lineno)d %(message)s'))
      fh.setLevel(log_level[args.get('verbose')])
      logger.addHandler(fh)

  def set_vars(self, args):
    # store commandline arguments as local variables.
    self.hosts = args.get('hosts')
    self.do_all = args.get('do_all')
    self.list = args.get('list')
    self.reboot = args.get('reboot')
    self.timeout = args.get('timeout')
    self.log_filename = args.get('log_filename')
    self.dry_run = args.get('dry_run')
    self.quiet_run = args.get('quiet_run')
    self.silent_run = args.get('silent_run')
    self.mode = args.get('mode')
    self.info_level = args.get('info_level')
    self.fw_type_filter = args.get('fw_type_filter')
    self.model_type_filter = args.get('model_type_filter')
    self.device_name_filter = args.get('device_name_filter')
    self.exclude = args.get('exclude')
    self.version = args.get('version')
    self.variant = args.get('variant')
    self.force = args.get('force')
    self.hap_setup_code = f"{args.get('hap_setup_code')[:3]}-{args.get('hap_setup_code')[3:-3]}-{args.get('hap_setup_code')[5:]}" if args.get('hap_setup_code') and '-' not in args.get('hap_setup_code') else args.get('hap_setup_code')
    self.local_file = args.get('local_file')
    self.network_type = args.get('network_type')
    self.ipv4_ip = args.get('ipv4_ip')
    self.ipv4_mask = args.get('ipv4_mask')
    self.ipv4_gw = args.get('ipv4_gw')
    self.ipv4_dns = args.get('ipv4_dns')
    self.username = args.get('user')
    self.password = args.get('password')

    # Windows and log file do not support acsii colours
    if self.log_filename or arch.startswith('Win'):
      global NC, WHITE, RED, GREEN, YELLOW, BLUE, PURPLE
      WHITE = ''
      RED = ''
      GREEN = ''
      YELLOW = ''
      BLUE = ''
      PURPLE = ''
      NC = ''

  def show_debug_info(self, args):
    # show debug output.
    logger.debug(f"OS: {PURPLE}{arch}{NC}")
    logger.debug(f"app_version: {app_ver}")
    logger.debug(f"do_all: {self.do_all}")
    logger.debug(f"dry_run: {args.get('dry_run')}")
    logger.debug(f"quiet_run: {args.get('quiet_run')}")
    logger.debug(f"silent_run: {args.get('silent_run')}")
    logger.debug(f"timeout: {args.get('timeout')}")
    logger.debug(f"reboot: {self.reboot}")
    logger.debug(f"manual_hosts: {self.hosts} ({len(self.hosts)})")
    logger.debug(f"user: {self.username}")
    logger.debug(f"password: {self.password}")
    logger.debug(f"action: {self.run_action}")
    logger.debug(f"mode: {self.mode}")
    logger.debug(f"info_level: {self.info_level}")
    logger.debug(f"fw_type_filter: {self.fw_type_filter}")
    logger.debug(f"model_type_filter: {self.model_type_filter}")
    logger.debug(f"device_name_filter: {self.device_name_filter}")
    logger.debug(f"version: {self.version}")
    logger.debug(f"exclude: {self.exclude}")
    logger.debug(f"local_file: {self.local_file}")
    logger.debug(f"variant: {self.variant}")
    logger.debug(f"force: {self.force}")
    logger.debug(f"verbose: {args.get('verbose')}")
    logger.debug(f"hap_setup_code: {self.hap_setup_code}")
    logger.debug(f"network_type: {self.network_type}")
    logger.debug(f"ipv4_ip: {self.ipv4_ip}")
    logger.debug(f"ipv4_mask: {self.ipv4_mask}")
    logger.debug(f"ipv4_gw: {self.ipv4_gw}")
    logger.debug(f"ipv4_dns: {self.ipv4_dns}")
    logger.debug(f"log_filename: {self.log_filename}")

  def handle_invalid_args(self):
    # handle invalid options from commandline.
    message = None
    if not self.hosts and not self.do_all:
      if self.run_action in ('list', 'flash'):
        self.do_all = True
      else:
        message = f"{WHITE}Requires a hostname or -a | --all{NC}"
    elif self.hosts and self.do_all:
      message = f"{WHITE}Invalid option hostname or -a | --all not both.{NC}"
    elif self.list and self.reboot:
      self.list = False
    elif self.network_type:
      if self.do_all:
        message = f"{WHITE}Invalid option -a | --all can not be used with --ip-type.{NC}"
      elif len(self.hosts) > 1:
        message = f"{WHITE}Invalid option only 1 host can be used with --ip-type.{NC}"
      elif self.network_type == 'static' and (not self.ipv4_ip or not self.ipv4_mask or not self.ipv4_gw or not self.ipv4_dns):
        if not self.ipv4_dns:
          message = f"{WHITE}Invalid option --dns can not be empty.{NC}"
        if not self.ipv4_gw:
          message = f"{WHITE}Invalid option --gw can not be empty.{NC}"
        if not self.ipv4_mask:
          message = f"{WHITE}Invalid option --mask can not be empty.{NC}"
        if not self.ipv4_ip:
          message = f"{WHITE}Invalid option --ip can not be empty.{NC}"
    elif self.version and len(self.version.split('.')) < 3:
      message = f"{WHITE}Incorrect version formatting i.e '1.9.0'{NC}"
    if message:
      self.parser.error(message)

  def get_arguments(self):
    # parse options from command line.
    self.parser = argparse.ArgumentParser(prog='flash-shelly.py', description='Shelly HomeKit flashing script utility')
    self.parser.add_argument('--app-version', action="store_true", default=None, help="Shows app version and exists.")
    self.parser.add_argument('-f', '--flash', action="store_true", default=None, help="Flash firmware to shelly device(s).")
    self.parser.add_argument('-l', '--list', action="store_true", default=None, help="List info of shelly device(s).")
    self.parser.add_argument('-m', '--mode', choices=['homekit', 'keep', 'revert'], default=None, help="Script mode homekit=homekit firmware, revert=stock firmware, keep=use current firmware type")
    self.parser.add_argument('-i', '--info-level', dest='info_level', type=int, choices=[1, 2, 3], default=None, help="Control how much detail is output in the list 1=minimal, 2=basic, 3=all.")
    self.parser.add_argument('-ft', '--fw-type', dest='fw_type_filter', choices=['homekit', 'stock', 'all'], default=None, help="Limit scan to current firmware type.")
    self.parser.add_argument('-mt', '--model-type', dest='model_type_filter', default=None, help="Limit scan to model type (dimmer, rgbw2, shelly1, etc.).")
    self.parser.add_argument('-dn', '--device-name', dest='device_name_filter', default=None, help="Limit scan to include term in device name.")
    self.parser.add_argument('-a', '--all', action="store_true", dest='do_all', default=None, help="Run against all the devices on the network.")
    self.parser.add_argument('-q', '--quiet', action="store_true", dest='quiet_run', default=None, help="Only include upgradeable shelly devices.")
    self.parser.add_argument('-e', '--exclude', dest="exclude", nargs='*', default=None, help="Exclude hosts from found devices.")
    self.parser.add_argument('-n', '--assume-no', action="store_true", dest='dry_run', default=None, help="Do a dummy run through.")
    self.parser.add_argument('-y', '--assume-yes', action="store_true", dest='silent_run', default=None, help="Do not ask any confirmation to perform the flash.")
    self.parser.add_argument('-V', '--version', type=str, dest="version", default=None, help="Flash a particular version.")
    self.parser.add_argument('--force', action="store_true", dest='force', default=None, help="Force a flash.")
    self.parser.add_argument('--variant', dest="variant", default=None, help="Pre-release variant name.")
    self.parser.add_argument('--local-file', dest="local_file", default=None, help="Use local file to flash.")
    self.parser.add_argument('-c', '--hap-setup-code', dest="hap_setup_code", default=None, help="Configure HomeKit setup code, after flashing.")
    self.parser.add_argument('--ip-type', choices=['dhcp', 'static'], dest="network_type", default=None, help="Configure network IP type (Static or DHCP)")
    self.parser.add_argument('--ip', dest="ipv4_ip", default=None, help="set IP address")
    self.parser.add_argument('--gw', dest="ipv4_gw", default=None, help="set Gateway IP address")
    self.parser.add_argument('--mask', dest="ipv4_mask", default=None, help="set Subnet mask address")
    self.parser.add_argument('--dns', dest="ipv4_dns", default=None, help="set DNS IP address")
    self.parser.add_argument('-v', '--verbose', dest="verbose", type=int, choices=[0, 1, 2, 3, 4, 5], default=None, help="Enable verbose logging 0=critical, 1=error, 2=warning, 3=info, 4=debug, 5=trace.")
    self.parser.add_argument('--timeout', type=int, default=None, help="Scan: Time in seconds to wait after last detected device before quitting.  Manual: Time in seconds to keep trying to connect.")
    self.parser.add_argument('--log-file', dest="log_filename", default=None, help="Create output log file with chosen filename.")
    self.parser.add_argument('--reboot', action="store_true", default=None, help="Preform a reboot of the device.")
    self.parser.add_argument('--user', default=None, help="Enter username for device security (default = admin).")
    self.parser.add_argument('--password', default=None, help="Enter password for device security.")
    self.parser.add_argument('hosts', type=str, nargs='*', default=None)
    self.parser.add_argument('--config', default=None, help="Load options from config file.")
    self.parser.add_argument('--save-config', default=None, help="Save current options to config file.")
    self.parser.add_argument('--save-defaults', action="store_true", default=None, help="Save current options as new defaults.")
    args = self.parser.parse_args()
    self.tmp_flags = vars(args)
    flags = {k: v for k, v in self.tmp_flags.items() if v is not None}  # clear out defaults, just leave commandline arguments (we need these later).
    if not flags.get('hosts'):  # remove host from defaults.
      flags.pop('hosts')  # remove host from defaults.
    args = self.defaults  # set defaults arguments.
    if flags.get('config'):
      config = self.load_config(flags.get('config'))  # load configs if requested from commandline.
      args.update(config)  # update arguments with profile additional options.
    args.update(flags)  # update arguments with commandline arguments, so they always have priority.
    if not args.get('flash') and args.get('reboot'):
      self.run_action = 'reboot'  # handle commandline argument '-r / --reboot'
    elif not args.get('flash') and args.get('list'):
      self.run_action = 'list'  # handle commandline argument '-l / --list'
    else:
      self.run_action = 'flash'  # handle commandline argument '-f / --flash', required to allow commandline override any saved defaults, and handle commandline default to '--flash'.
    return args

  def run_app(self):  # main run of the script, handles commandline arguments.
    if new_defaults := self.load_config('defaults'):
      self.defaults.update(new_defaults)  # update defaults with defaults config file if available
    args = self.get_arguments()

    self.setup_logger(args)  # setup output logging

    if args.get('save_config') or args.get('save_defaults'):  # handle commandline arguments '--save-config' and '--save-defaults'
      self.save_config(self.tmp_flags)
    if args.get('app_version'):  # handle commandline argument '--app-version'
      logger.info(f"Version: {app_ver}")
      sys.exit(0)

    self.load_security()  # load security information.
    self.set_vars(args)  # store args as local variables.
    self.show_debug_info(args)  # show debug info as debug logger.
    self.handle_invalid_args()  # handle invalid options from commandline.

    atexit.register(self.exit_app)  # handle safe exit (user break CTRL-C).

    # run correct mode manual / device scan.
    try:
      if self.hosts:
        self.manual_hosts()
      else:
        self.device_scan()
    except Exception:
      self._show_exception_message()
    except KeyboardInterrupt:
      self.stop_scan()  # catch user break CTRL-C

  @staticmethod
  def _show_exception_message():
    logger.info(f"{RED}")
    logger.info(f"flash-shelly version: {app_ver}")
    logger.info("Try to update your script, maybe the bug is already fixed!")
    exc_type, exc_value, exc_traceback = sys.exc_info()
    traceback.print_exception(exc_type, exc_value, exc_traceback, file=sys.stdout)
    logger.info(f"{NC}")

  @staticmethod
  def parse_version(vs):  # split version string into list.
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

  def is_newer(self, v1, v2):  # check to see if value1 is higher than value2, return True or False.
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
  def wait_for_reboot(device, before_reboot_uptime=-1, reboot_only=False):  # handle reboot detection when device is back up.
    logger.debug(f"{PURPLE}[Wait For Reboot]{NC}")
    logger.info(f"waiting for {device.friendly_host} to reboot[!n]")
    logger.debug("")
    time.sleep(5)
    current_version = None
    current_uptime = device.get_uptime(False)
    if not reboot_only:
      n = 1
      while current_uptime >= before_reboot_uptime and n < 90 or current_version is None:
        logger.trace(f"loop number: {n}")
        logger.debug(f"current_uptime: {current_uptime}")
        logger.debug(f"before_reboot_uptime: {before_reboot_uptime}")
        logger.info(".[!n]")
        logger.debug("")
        if n == 30:
          logger.info("")
          logger.info(f"still waiting for {device.friendly_host} to reboot[!n]")
          logger.debug("")
        elif n == 60:
          logger.info("")
          logger.info(f"we'll wait just a little longer for {device.friendly_host} to reboot[!n]")
          logger.debug("")
        current_uptime = device.get_uptime(False)
        current_version = device.get_current_version(error_message=False)
        logger.debug(f"current_version: {current_version}")
        n += 1
    else:
      while device.get_uptime(False) < 3:
        time.sleep(1)  # wait 1 second before retrying.
    logger.info("")
    return current_version

  @staticmethod
  def just_booted_check(device):  # stock devices need time after a boot, before we can flash.
    uptime = device.get_uptime(False)
    waiting_shown = False
    while uptime <= 25:  # make sure device has not just booted up.
      logger.trace("seems like we just booted, delay a few seconds")
      if not waiting_shown:
        logger.info("Waiting for device.")
        waiting_shown = True
      time.sleep(5)
      uptime = device.get_uptime(False)
    return uptime

  def flash_firmware(self, device):  # handle flash procedure.
    logger.debug(f"{PURPLE}[Write Flash]{NC}")
    uptime = self.just_booted_check(device)  # check to see if device has just booted.
    response = device.write_firmware()  # do actual flash firmware.
    logger.trace(response)
    if response and response.status_code == 200:
      message = f"{GREEN}Successfully flashed {device.friendly_host} to {device.flash_fw_type_str} {device.flash_fw_version}{NC}"
      if self.revert_to_stock is True:
        self.wait_for_reboot(device, uptime)
        reboot_check = device.is_stock()
        flash_fw = 'stock revert'
        message = f"{GREEN}Successfully reverted {device.friendly_host} to stock firmware{NC}"
      else:
        reboot_check = self.parse_version(self.wait_for_reboot(device, uptime))
        flash_fw = self.parse_version(device.flash_fw_version)
      if (reboot_check == flash_fw) or (self.revert_to_stock is True and reboot_check is True):
        if device.already_processed is False:
          self.flashed_devices += 1
        if self.requires_upgrade is True:
          self.requires_upgrade = 'Done'
        device.already_processed = True
        logger.critical(f"{message}")
      else:
        if reboot_check == '0.0.0':
          logger.info(f"{RED}Flash may have failed, please manually check version{NC}")
        else:
          self.failed_flashed_devices += 1
          logger.info(f"{RED}Failed to flash {device.friendly_host} to {device.flash_fw_type_str} {device.flash_fw_version}{NC}")
        logger.debug(f"Current: {reboot_check}")
        logger.debug(f"flash_fw_version: {flash_fw}")

  def reboot_device(self, device):  # handle reboot procedure.
    logger.debug(f"{PURPLE}[Reboot Device]{NC}")
    response = device.do_reboot()  # do actual reboot.
    if response.status_code == 200:
      self.wait_for_reboot(device, reboot_only=True)
      logger.info("Device has rebooted...")

  def color_mode_change(self, device, mode_color):  # handle colour mode change.
    logger.debug(f"{PURPLE}[Color Mode Change] change from {device.info.get('color_mode')} to {mode_color}{NC}")
    uptime = device.get_uptime(False)
    response = device.do_mode_change(mode_color)  # do actual colour mode change.
    if response.status_code == 200:
      self.wait_for_reboot(device, uptime)
      current_color_mode = device.get_color_mode()
      logger.debug(f"mode_color: {mode_color}")
      logger.debug(f"current_color_mode: {current_color_mode}")
      if current_color_mode == mode_color:
        device.already_processed = True
        logger.critical(f"{GREEN}Successfully changed {device.friendly_host} to mode: {current_color_mode}{NC}")
        self.requires_color_mode_change = 'Done'

  def parse_info(self, device, hk_ver=None):  # parse device information, and action on commandline options.
    logger.debug(f"")
    logger.debug(f"{PURPLE}[Parse Info]{NC}")

    if device.already_processed is False:
      self.total_devices += 1
      self.flash_question = None

    perform_flash = False
    download_url_request = False
    host = device.host
    wifi_ip = device.wifi_ip
    friendly_host = device.friendly_host
    device_id = device.info.get('device_id')
    model = device.info.get('model')
    stock_fw_model = device.info.get('stock_fw_model')
    color_mode = device.info.get('color_mode')
    current_fw_version = device.info.get('fw_version')
    current_fw_type = device.fw_type
    current_fw_type_str = device.info.get('fw_type_str')
    flash_fw_version = device.version or device.flash_fw_version
    flash_fw_type_str = device.flash_fw_type_str
    force_version = device.version
    force_flash = True if device.version and current_fw_version != device.version else self.force
    download_url = device.download_url
    not_supported = device.not_supported
    no_variant = device.no_variant
    revert_to_stock = self.revert_to_stock
    device_name = device.info.get('device_name')
    wifi_ssid = device.info.get('wifi_conn_ssid')
    wifi_rssi = device.info.get('wifi_conn_rssi')
    sys_temp = device.info.get('sys_temp')
    sys_mode = device.info.get('sys_mode_str')
    uptime = datetime.timedelta(seconds=device.info.get('uptime', 0))
    hap_ip_conns_pending = device.info.get('hap_ip_conns_pending')
    hap_ip_conns_active = device.info.get('hap_ip_conns_active')
    hap_ip_conns_max = device.info.get('hap_ip_conns_max')
    battery = device.info.get('battery')

    logger.debug(f"flash mode: {self.flash_mode}")
    logger.debug(f"stock_fw_model: {stock_fw_model}")
    logger.debug(f"color_mode: {color_mode}")
    logger.debug(f"current_fw_version: {current_fw_type_str} {current_fw_version}")
    logger.debug(f"flash_fw_version: {flash_fw_type_str} {flash_fw_version}")
    logger.debug(f"force_flash: {force_flash}")
    logger.debug(f"manual_version: {force_version}")
    logger.debug(f"download_url: {download_url}")
    logger.debug(f"not_supported: {not_supported}")
    logger.debug(f"no_variant: {no_variant}")
    logger.debug(f"revert_to_stock: {revert_to_stock}")

    if download_url and download_url != 'local':
      download_url_request = requests.head(download_url)
      logger.debug(f"download_url_request: {download_url_request}")
    if not_supported is True:
      flash_fw_type_str = f"{RED}{flash_fw_type_str}{NC}"
      latest_fw_label = f"{RED}is not supported{NC}"
    elif no_variant is True:
      flash_fw_type_str = f"{RED}{flash_fw_type_str}{NC}"
      latest_fw_label = f"{RED}no version with variant {device.variant} is available{NC}"
    elif revert_to_stock is True:
      flash_fw_type_str = 'Revert to'
      latest_fw_label = 'Stock'
    elif not download_url and device.local_file:
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
    if (not self.quiet_run or flash_fw_newer or force_flash and flash_fw_version != '0.0.0') and self.requires_upgrade != 'Done':
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
      elif current_fw_type == self.flash_mode:
        logger.info(f"{WHITE}Firmware: {NC}{current_fw_type_str} {current_fw_version}")

    if download_url and (force_flash or self.requires_upgrade is True or current_fw_type != self.flash_mode or flash_fw_newer) and device.already_processed is False:
      self.upgradeable_devices += 1

    if self.run_action == 'flash':
      keyword = None
      if not download_url:
        if force_version:
          keyword = f"{flash_fw_type_str} Version {force_version} is not available..."
        elif device.local_file:
          keyword = "Incorrect Zip File for device..."
        if keyword is not None and not self.quiet_run:
          logger.info(f"{keyword}")
        return 0

      if self.exclude and friendly_host in self.exclude:
        logger.info("Skipping as device has been excluded...")
        logger.info("")
        return 0

      action_message = "Would have been"
      if self.requires_upgrade is True:
        if self.flash_mode == 'homekit':
          action_message = "This device needs to be"
          keyword = "upgraded to latest stock firmware version, before you can flash to HomeKit"
        elif self.flash_mode == 'stock':
          keyword = f"upgraded to version {flash_fw_version}"
      elif self.requires_color_mode_change is True:
        action_message = "This device needs to be"
        keyword = "changed to colour mode in stock firmware, before you can flash to HomeKit"
      elif force_flash:
        perform_flash = True
        keyword = f"flashed to {flash_fw_type_str} version {flash_fw_version}"
      elif revert_to_stock is True:
        perform_flash = True
        keyword = f"reverted to stock firmware"
      elif current_fw_type != self.flash_mode:
        perform_flash = True
        keyword = f"converted to {flash_fw_type_str} firmware"
      elif flash_fw_newer:
        perform_flash = True
        keyword = f"upgraded from {current_fw_version} to version {flash_fw_version}"

      logger.debug(f"perform_flash: {perform_flash}")
      logger.debug(f"requires_upgrade: {self.requires_upgrade}")
      logger.debug(f"requires_color_mode_change: {self.requires_color_mode_change}")
      if perform_flash or self.requires_upgrade or self.requires_color_mode_change:
        if self.dry_run is False and self.silent_run is False:
          if self.requires_upgrade is True or self.requires_color_mode_change is True:
            flash_message = f"{action_message} {keyword}"
          elif self.requires_upgrade == 'Done':
            flash_message = f"Do you wish to continue to flash {friendly_host} to HomeKit firmware version {flash_fw_version}"
          elif revert_to_stock is True:
            flash_message = f"Do you wish to revert {friendly_host} to stock firmware"
          else:
            flash_message = f"Do you wish to flash {friendly_host} to {flash_fw_type_str} firmware version {flash_fw_version}"
          if self.flash_question is None:
            if input(f"{flash_message} (y/n) ? ") in ('y', 'Y'):
              self.flash_question = True
            else:
              self.flash_question = False
              logger.info("Skipping Flash...")
        elif self.dry_run is False and self.silent_run is True:
          self.flash_question = True
        elif self.dry_run is True:
          logger.info(f"{action_message} {keyword}...")
      logger.debug(f"flash_question: {self.flash_question}")
      if self.flash_question is True:
        if self.requires_color_mode_change is True:
          self.color_mode_change(device, 'color')
        else:
          self.flash_firmware(device)

    # handle set HomeKit Setup code option.
    if device.is_homekit() and self.hap_setup_code:
      set_hap_code = False
      if self.dry_run is False and self.silent_run is False:
        set_hap_code = input(f"Do you wish to set your HomeKit code to {self.hap_setup_code} (y/n) ? ") in ('y', 'Y')
      elif self.dry_run is True:
        logger.info(f"Would have set your HomeKit code to {self.hap_setup_code}")
      if set_hap_code or self.silent_run:
        device.write_hap_setup_code()

    # handle set network type option.
    if self.network_type:
      set_ip = False
      if self.network_type == 'static':
        action_message = f"to {self.ipv4_ip}"
      else:
        action_message = f"to use DHCP"
      if self.dry_run is False and self.silent_run is False:
        set_ip = input(f"Do you wish to set your IP address {action_message} (y/n) ? ") in ('y', 'Y')
      elif self.dry_run is True:
        logger.info(f"Would have set your IP address {action_message}")
      if set_ip or self.silent_run:
        device.write_network_type()

    # handle reboot option
    if self.run_action == 'reboot':
      reboot = False
      if self.dry_run is False and self.silent_run is False:
        reboot = input(f"Do you wish to reboot {friendly_host} (y/n) ? ") in ('y', 'Y')
      elif self.dry_run is True:
        logger.info(f"Would have been rebooted...")
      if reboot or self.silent_run:
        self.reboot_device(device)

  def start_webserver(self):  # handle starting of webserver
    loop = 1
    while not self.http_server_started:
      try:
        logger.info(f"{WHITE}Starting local webserver on port {self.webserver_port}{NC}")
        if self.server is None:
          self.server = StoppableHTTPServer(("", self.webserver_port), HTTPRequestHandler)
        self.http_server_started = True
      except OSError as err:
        logger.critical(f"{WHITE}Failed to start server {err} port {self.webserver_port}{NC}")
        self.webserver_port += 1
        loop += 1
        if loop == 10:
          sys.exit(1)
    if self.thread is None:
      self.thread = threading.Thread(None, self.server.run)
      self.thread.start()

  def stop_webserver(self):
    if self.http_server_started and self.server is not None:
      logger.trace("Shutting down webserver")
      self.server.shutdown()
      self.thread.join()

  def probe_device(self, device):  # get information from device, and pass on to parse_info, so it can be actioned.
    logger.debug("")
    logger.debug(f"{PURPLE}[Probe Device] {device.host}{NC}")
    if not device.get_info():  # make sure we have device info, if not exit method.
      return 0
    hk_flash_fw_version = None
    self.requires_upgrade = False
    self.requires_color_mode_change = False
    self.flash_mode = device.fw_type if self.mode == 'keep' else self.mode
    if device.is_stock() and self.local_file:
      self.start_webserver()  # start local webserver, required for flashing local files to stock device.
    if self.local_file:  # handle local file firmware.
      if device.is_homekit():
        device.parse_local_file()
      elif device.is_stock():
        device.parse_stock_release_info()
        if device.info.get('device', {}).get('type', '') == 'SHRGBW2' and device.info.get('color_mode') == 'white':  # checks RGBW2 colour mode if 'white' mark it for mode change required.
          self.requires_color_mode_change = True
        if device.info.get('fw_version') == '0.0.0' or self.is_newer(device.flash_fw_version, device.info.get('fw_version')):
          self.requires_upgrade = True
        else:
          device.parse_local_file()
    elif self.flash_mode == 'homekit':
      device.parse_homekit_release_info()
      if device.is_stock():
        if device.download_url:
          download_url_request = requests.head(device.download_url)
          logger.debug(f"download_url_request: {download_url_request}")
          hk_flash_fw_version = device.flash_fw_version
        device.parse_stock_release_info()
        if device.info.get('device', {}).get('type', '') == 'SHRGBW2' and device.info.get('color_mode') == 'white':  # checks RGBW2 colour mode if 'white' mark it for mode change required.
          self.requires_color_mode_change = True
        if device.info.get('fw_version') == '0.0.0' or self.is_newer(device.flash_fw_version, device.info.get('fw_version')):  # checks device if is on release or firmware is no latest, mark for update required.
          self.requires_upgrade = True
        else:
          device.parse_homekit_release_info()
    elif self.flash_mode == 'revert':
      self.revert_to_stock = True
      if device.is_homekit():
        device.parse_homekit_release_info()
      else:
        device.parse_stock_release_info()
        self.flash_mode = 'stock'
    elif self.flash_mode == 'stock':
      device.parse_stock_release_info()
    if device.flash_fw_version is None or not self.check_fw(device, 2.1):  # script requires version 2.1 firmware minimum.  # TODO increase to 2.9.2 when more mainstream.
      return 0
    self.parse_info(device, hk_flash_fw_version)  # main convert run, or update stock to the latest firmware if an update or color mode is required.
    if self.run_action == 'flash' and (self.requires_upgrade in {'Done', True} or self.requires_color_mode_change in {'Done', True}) and self.flash_question is True:
      if self.requires_color_mode_change:  # do another run if colour mode is still required.
        device.get_info(True)  # update device information after previous run.
        self.parse_info(device)  # colour change run.
      device.get_info(True)  # update device information after previous run.
      # if device is still stock and has been updated to the latest version, finally do convert to homekit run.
      if device.flash_fw_version != '0.0.0' and (not self.is_newer(device.flash_fw_version, device.info.get('fw_version')) or (device.is_stock() and self.flash_mode == 'homekit')):
        if self.local_file:
          device.parse_local_file()
        else:
          device.parse_homekit_release_info()
        self.parse_info(device)

  def is_fw_type(self, fw_type):
    return fw_type.lower() in self.fw_type_filter.lower() or self.fw_type_filter == 'all'

  def is_model_type(self, fw_model):
    return self.model_type_filter is not None and self.model_type_filter.lower() in fw_model.lower() or self.model_type_filter == 'all'

  def is_device_name(self, device_name):
    return device_name is not None and self.device_name_filter is not None and self.device_name_filter.lower() in device_name.lower() or self.device_name_filter == 'all'

  def manual_hosts(self):  # handle manual hosts from commandline.
    logger.debug(f"{PURPLE}[Manual Hosts]{NC}")
    logger.info(f"{WHITE}Looking for Shelly devices...{NC}")
    for host in self.hosts:
      logger.debug(f"")
      logger.debug(f"{PURPLE}[Manual Hosts] action {host}{NC}")
      (username, password) = self.get_security_data(host)
      device = Detection(host, username, password)
      if device and device.fw_type is not None:
        if device.fw_type == 'homekit':
          device = HomeKitDevice(device.host, device.username, device.password, device.wifi_ip, device.fw_type, device.auth, False)
        elif device.fw_type == 'stock':
          device = StockDevice(device.host, device.username, device.password, device.wifi_ip, device.fw_type, device.auth, False)
      else:
        device = None
      if device and device.get_info():
        self.probe_device(device)

  def device_scan(self):  # handle devices found from DNS scanner.
    logger.debug(f"{PURPLE}[Device Scan] automatic scan{NC}")
    logger.info(f"{WHITE}Scanning for Shelly devices...{NC}")
    self.zc = zeroconf.Zeroconf()
    self.listener = ServiceListener()
    zeroconf.ServiceBrowser(zc=self.zc, type_='_http._tcp.local.', listener=self.listener)
    while True:
      try:
        device = self.listener.queue.get(timeout=self.timeout)
      except queue.Empty:
        break
      logger.debug(f"")
      logger.debug(f"{PURPLE}[Device Scan] action queue entry{NC}")
      if device and device.get_info():
        fw_model = device.info.get('model') if device.is_homekit() else device.shelly_model(device.info.get('device').get('type'))[0]
        if self.is_fw_type(device.fw_type) and self.is_model_type(fw_model) and self.is_device_name(device.info.get('device_name')):
          self.probe_device(device)

  def stop_scan(self):  # stop DNS scanner.
    if self.listener is not None:
      while True:
        try:
          self.listener.queue.get_nowait()
        except queue.Empty:
          self.zc.close()
          break

  def exit_app(self):  # exit script.
    logger.info(f"")
    if self.run_action == 'flash':
      if self.failed_flashed_devices > 0:
        logger.info(f"{GREEN}Devices found: {self.total_devices} Upgradeable: {self.upgradeable_devices} Flashed: {self.flashed_devices}{NC} {RED}Failed: {self.failed_flashed_devices}{NC}")
      else:
        logger.info(f"{GREEN}Devices found: {self.total_devices} Upgradeable: {self.upgradeable_devices} Flashed: {self.flashed_devices}{NC}")
    elif self.total_devices > 0:
      logger.info(f"{GREEN}Devices found: {self.total_devices} Upgradeable: {self.upgradeable_devices}{NC}")
    if self.log_filename:
      logger.info(f"Log file created: {self.log_filename}")
    self.stop_webserver()


# start script and pass commandline arguments to 'run_app'
if __name__ == '__main__':
  main = Main()
  sys.exit(main.run_app())
