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

#include "HAPCharacteristic.h"

#include "shelly_hap_accessory.hpp"
#include "shelly_hap_service.hpp"

namespace shelly {
namespace hap {

Characteristic::Characteristic(uint16_t iid, HAPCharacteristicFormat format,
                               const HAPUUID *type,
                               const char *debug_description)
    : hap_char_({}) {
  HAPBaseCharacteristic *c =
      reinterpret_cast<HAPBaseCharacteristic *>(&hap_char_.char_);
  c->iid = iid;
  c->format = format;
  c->characteristicType = type;
  c->debugDescription = debug_description;
  hap_char_.inst = this;
}

Characteristic::~Characteristic() {
}

const Service *Characteristic::parent() const {
  return parent_;
}

void Characteristic::set_parent(const Service *parent) {
  parent_ = parent;
}

const HAPCharacteristic *Characteristic::hap_charactristic() {
  return static_cast<const HAPCharacteristic *>(&hap_char_);
}

void Characteristic::RaiseEvent() {
  const Service *svc = parent();
  if (svc == nullptr) return;
  const Accessory *acc = svc->parent();
  if (acc == nullptr || acc->server() == nullptr) return;
  HAPAccessoryServerRaiseEvent(acc->server(), GetHAPCharacteristic(),
                               svc->GetHAPService(), acc->GetHAPAccessory());
}

StringCharacteristic::StringCharacteristic(uint16_t iid, const HAPUUID *type,
                                           uint16_t max_length,
                                           const std::string &initial_value,
                                           const char *debug_description)
    : Characteristic(iid, kHAPCharacteristicFormat_String, type,
                     debug_description),
      value_(initial_value) {
  HAPStringCharacteristic *c = &hap_char_.char_.string;
  c->constraints.maxLength = max_length;
  c->properties.readable = true;
  c->callbacks.handleRead = StringCharacteristic::HandleReadCB;
}

StringCharacteristic::~StringCharacteristic() {
}

const std::string &StringCharacteristic::value() const {
  return value_;
}

void StringCharacteristic::set_value(const std::string &value) {
  value_ = value;
}

// static
HAPError StringCharacteristic::HandleReadCB(
    HAPAccessoryServerRef *server,
    const HAPStringCharacteristicReadRequest *request, char *value,
    size_t maxValueBytes, void *context) {
  auto *hci = reinterpret_cast<const HAPCharacteristicWithInstance *>(
      request->characteristic);
  auto *c = static_cast<const StringCharacteristic *>(hci->inst);
  size_t n = std::min(maxValueBytes - 1, c->value_.length());
  std::memcpy(value, c->value_.data(), n);
  value[n] = '\0';
  (void) server;
  (void) context;
  return kHAPError_None;
}

}  // namespace hap
}  // namespace shelly
