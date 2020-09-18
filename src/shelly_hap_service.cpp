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

#include "shelly_hap_service.hpp"

#include "shelly_common.hpp"

namespace shelly {
namespace hap {

Service::Service() : svc_({}) {
}

Service::Service(uint16_t iid, const HAPUUID *type,
                 const char *debug_description, bool hidden)
    : svc_({}) {
  svc_.iid = iid;
  svc_.serviceType = type;
  svc_.debugDescription = debug_description;
  svc_.properties.hidden = hidden;
}

Service::~Service() {
}

uint16_t Service::iid() const {
  return svc_.iid;
}

bool Service::primary() const {
  return svc_.properties.primaryService;
}

void Service::set_primary(bool is_primary) {
  svc_.properties.primaryService = is_primary;
}

const Accessory *Service::parent() const {
  return parent_;
}

void Service::set_parent(const Accessory *parent) {
  parent_ = parent;
}

void Service::AddChar(Characteristic *c) {
  c->set_parent(this);
  chars_.emplace_back(c);
  if (!hap_chars_.empty()) hap_chars_.pop_back();
  hap_chars_.push_back(c->GetHAPCharacteristic());
  hap_chars_.push_back(nullptr);
  svc_.characteristics = hap_chars_.data();
}

void Service::AddNameChar(uint16_t iid, const std::string &name) {
  auto *c =
      new StringCharacteristic(iid, &kHAPCharacteristicType_Name, 64, name,
                               kHAPCharacteristicDebugDescription_Name);
  svc_.name = c->GetValue().c_str();
  AddChar(c);
}

void Service::AddLink(uint16_t iid) {
  if (iid == 0) return;
  if (links_.size() > 0) links_.pop_back();
  links_.push_back(iid);
  links_.push_back(0);
  svc_.linkedServices = links_.data();
}

const HAPService *Service::GetHAPService() const {
  if (svc_.iid == 0) return nullptr;
  return &svc_;
}

ServiceLabelService::ServiceLabelService(uint8_t ns) {
  svc_.iid = SHELLY_HAP_IID_BASE_SERVICE_LABEL;
  svc_.serviceType = &kHAPServiceType_ServiceLabel;
  svc_.debugDescription = kHAPServiceDebugDescription_ServiceLabel;
  AddChar(new UInt8Characteristic(
      svc_.iid + 1, &kHAPCharacteristicType_ServiceLabelNamespace, 0, 1, 1,
      [ns](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
           uint8_t *value) {
        *value = ns;
        return kHAPError_None;
      },
      false /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_ServiceLabelNamespace));
}

ServiceLabelService::~ServiceLabelService() {
}

}  // namespace hap
}  // namespace shelly
