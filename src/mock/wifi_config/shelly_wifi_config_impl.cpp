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

#include "shelly_wifi_config.hpp"

#include "mgos.hpp"

namespace shelly {

static WifiConfig s_cfg;

WifiConfig GetWifiConfig() {
  return s_cfg;
}

Status SetWifiConfig(const WifiConfig &cfg) {
  std::string cs = cfg.ToJSON();
  LOG(LL_INFO, ("Set wifi config to: %s", cs.c_str()));
  s_cfg = cfg;
  return Status::OK();
}

void ResetWifiConfig() {
  s_cfg = WifiConfig();
}

WifiInfo GetWifiInfo() {
  WifiInfo empty;
  return empty;
}

void ReportClientRequest(const std::string &client_addr) {
  (void) client_addr;
}

void InitWifiConfigManager() {
}

void StartWifiConfigManager() {
}

std::string GetMACAddr(bool sta UNUSED_ARG, bool delims) {
  uint8_t mac[6];
  device_get_mac_address(mac);
  return FormatMACAddr(mac, delims);
}

}  // namespace shelly
