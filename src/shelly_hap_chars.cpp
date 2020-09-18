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

#include "shelly_hap_chars.hpp"

#include <cstring>

#include "shelly_hap_accessory.hpp"
#include "shelly_hap_service.hpp"

namespace shelly {
namespace hap {

// static
std::vector<Characteristic *> Characteristic::instances_;

Characteristic::Characteristic() {
  instances_.push_back(this);
  instances_.shrink_to_fit();
}

Characteristic::~Characteristic() {
  for (auto it = instances_.begin(); it != instances_.end(); it++) {
    if (*it == this) {
      instances_.erase(it);
      break;
    }
  }
}

const Service *Characteristic::parent() const {
  return parent_;
}

void Characteristic::set_parent(const Service *parent) {
  parent_ = parent;
}

void Characteristic::RaiseEvent() {
  const Service *svc = parent();
  if (svc == nullptr) return;
  const Accessory *acc = svc->parent();
  if (acc == nullptr || acc->server() == nullptr) return;
  HAPAccessoryServerRaiseEvent(acc->server(), GetHAPCharacteristic(),
                               svc->GetHAPService(), acc->GetHAPAccessory());
}

// static
Characteristic *Characteristic::FindInstance(const HAPCharacteristic *base) {
  for (auto *i : instances_) {
    if (i->GetHAPCharacteristic() == base) return i;
  }
  return nullptr;
}

StringCharacteristic::StringCharacteristic(uint16_t iid, const HAPUUID *type,
                                           uint16_t max_length,
                                           const std::string &initial_value,
                                           const char *debug_description)
    : value_(initial_value) {
  std::memset(&char_, 0, sizeof(char_));
  char_.format = kHAPCharacteristicFormat_String;
  char_.iid = iid;
  char_.characteristicType = type;
  char_.debugDescription = debug_description;
  char_.constraints.maxLength = max_length;
  char_.properties.readable = true;
  char_.callbacks.handleRead = StringCharacteristic::HandleReadCB;
}

StringCharacteristic::~StringCharacteristic() {
}

HAPCharacteristic *StringCharacteristic::GetHAPCharacteristic() {
  return &char_;
}

void StringCharacteristic::SetValue(const std::string &value) {
  value_ = value;
}

const std::string &StringCharacteristic::GetValue() const {
  return value_;
}

// static
HAPError StringCharacteristic::HandleReadCB(
    HAPAccessoryServerRef *server,
    const HAPStringCharacteristicReadRequest *request, char *value,
    size_t maxValueBytes, void *context) {
  StringCharacteristic *c = (StringCharacteristic *) FindInstance(
      (const HAPCharacteristic *) request->characteristic);
  size_t n = std::min(maxValueBytes - 1, c->value_.length());
  std::memcpy(value, c->value_.data(), n);
  value[n] = '\0';
  (void) server;
  (void) context;
  return kHAPError_None;
}

}  // namespace hap
}  // namespace shelly
