/*
 * Copyright (c) 2020 Deomid "rojer" Ryabkov
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

#include "shelly_hap_accessory.hpp"

#include "mgos.h"

#include "shelly_common.hpp"

namespace shelly {
namespace hap {

// static
std::vector<Accessory *> Accessory::instances_;

Accessory::Accessory(uint64_t aid, HAPAccessoryCategory category,
                     const std::string &name, const IdentifyCB &identify_cb)
    : name_(name), identify_cb_(identify_cb), acc_({}) {
  acc_.aid = aid;
  acc_.category = category;
  acc_.name = name_.c_str();
  acc_.manufacturer = CS_STRINGIFY_MACRO(PRODUCT_VENDOR);
  acc_.model = CS_STRINGIFY_MACRO(PRODUCT_MODEL);
  acc_.serialNumber = mgos_sys_config_get_device_sn();
  if (acc_.serialNumber == nullptr) {
    static char sn[13] = "????????????";
    mgos_expand_mac_address_placeholders(sn);
    acc_.serialNumber = sn;
  }
  acc_.firmwareVersion = mgos_sys_ro_vars_get_fw_version();
  acc_.hardwareVersion = CS_STRINGIFY_MACRO(PRODUCT_HW_REV);
  instances_.push_back(this);
  instances_.shrink_to_fit();
  acc_.callbacks.identify = &Accessory::Identify;
}

Accessory::~Accessory() {
  for (auto it = instances_.begin(); it != instances_.end(); it++) {
    if (*it == this) {
      instances_.erase(it);
      break;
    }
  }
}

void Accessory::AddService(std::unique_ptr<Service> svc) {
  AddHAPService(svc->GetHAPService());
  svcs_.emplace_back(std::move(svc));
}

void Accessory::AddHAPService(const HAPService *svc) {
  if (svc == nullptr) return;
  if (!hap_svcs_.empty()) hap_svcs_.pop_back();
  hap_svcs_.push_back(svc);
  hap_svcs_.push_back(nullptr);
  acc_.services = hap_svcs_.data();
}

const HAPAccessory *Accessory::GetHAPAccessory() const {
  if (hap_svcs_.empty()) return nullptr;
  return &acc_;
}

// static
Accessory *Accessory::FindInstance(const HAPAccessory *base) {
  for (auto *i : instances_) {
    if (i->GetHAPAccessory() == base) return i;
  }
  return nullptr;
}

// static
HAPError Accessory::Identify(HAPAccessoryServerRef *server,
                             const HAPAccessoryIdentifyRequest *request,
                             void *context) {
  Accessory *a = FindInstance(request->accessory);
  if (a->identify_cb_ == nullptr) {
    return kHAPError_Unknown;
  }
  return a->identify_cb_(request);
  (void) server;
  (void) context;
}

}  // namespace hap
}  // namespace shelly
