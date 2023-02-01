/*
 * Copyright (c) Shelly-HomeKit Contributors
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>

#include "shelly_common.hpp"

namespace shelly {

struct WifiAPConfig {
  bool enable = false;
  std::string ssid;
  std::string pass;

  bool operator==(const WifiAPConfig &other) const;
};

struct WifiSTAConfig {
  bool enable = false;
  std::string ssid;
  std::string pass;
  std::string ip;
  std::string netmask;
  std::string gw;
  std::string nameserver;

  bool operator==(const WifiSTAConfig &other) const;
};

struct WifiConfig {
  WifiAPConfig ap;
  WifiSTAConfig sta;
  WifiSTAConfig sta1;
  int sta_ps_mode = 0;

  std::string ToJSON() const;
};

WifiConfig GetWifiConfig();

Status SetWifiConfig(const WifiConfig &cfg);

void ResetWifiConfig();

struct WifiInfo {
  bool ap_running = false;
  bool sta_connecting = false;
  bool sta_connected = false;
  std::string status;
  // When connected:
  int sta_rssi = 0;
  std::string sta_ip;
  std::string sta_ssid;
};

WifiInfo GetWifiInfo();

void ReportClientRequest(const std::string &client_addr);

std::string ScreenPassword(const std::string &pw);

void InitWifiConfigManager();

void StartWifiConfigManager();

std::string GetMACAddr(bool sta = true, bool delims = true);

std::string FormatMACAddr(const uint8_t *mac, bool delims);

}  // namespace shelly
