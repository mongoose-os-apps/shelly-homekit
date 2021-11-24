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

bool WifiAPConfig::operator==(const WifiAPConfig &other) const {
  return (enable == other.enable && ssid == other.ssid && pass == other.pass);
}

bool WifiSTAConfig::operator==(const WifiSTAConfig &other) const {
  return (enable == other.enable && ssid == other.ssid && pass == other.pass &&
          ip == other.ip && netmask == other.netmask && gw == other.gw);
}

static std::string ScreenPassword(const std::string &pw) {
  std::string spw(pw);
  for (auto it = spw.begin(); it != spw.end(); it++) {
    *it = '*';
  }
  return spw;
}

std::string WifiConfig::ToJSON() const {
  std::string ap_pw = ScreenPassword(ap.pass);
  std::string sta_pw = ScreenPassword(sta.pass);
  std::string sta1_pw = ScreenPassword(sta1.pass);
  return mgos::JSONPrintStringf(
      "{ap: {enable: %B, ssid: %Q, pass: %Q}, "
      "sta: {enable: %B, ssid: %Q, pass: %Q}, "
      "sta1: {enable: %B, ssid: %Q, pass: %Q}}",
      ap.enable, ap.ssid.c_str(), ap_pw.c_str(), sta.enable, sta.ssid.c_str(),
      sta_pw.c_str(), sta1.enable, sta1.ssid.c_str(), sta1_pw.c_str());
}

}  // namespace shelly
