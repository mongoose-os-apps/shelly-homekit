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
#pragma once

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#endif

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "HAP.h"

namespace shelly {
namespace hap {

class Service;

class Characteristic {
 public:
  Characteristic(uint16_t iid, HAPCharacteristicFormat format,
                 const HAPUUID *type, const char *debug_description = nullptr);
  virtual ~Characteristic();

  const Service *parent() const;
  void set_parent(const Service *parent);

  virtual const HAPCharacteristic *GetHAPCharacteristic() {
    return hap_charactristic();
  }

  const HAPCharacteristic *hap_charactristic();

  void RaiseEvent();

 protected:
  struct HAPCharacteristicWithInstance {
    union AllHAPCHaracteristicTypes {
      HAPDataCharacteristic data;
      HAPBoolCharacteristic bool_;
      HAPUInt8Characteristic uint8;
      HAPUInt16Characteristic uint16;
      HAPUInt32Characteristic uint32;
      HAPUInt64Characteristic uint64;
      HAPIntCharacteristic int_;
      HAPFloatCharacteristic float_;
      HAPStringCharacteristic string;
      HAPTLV8Characteristic tlv8;
    } char_;
    Characteristic *inst;  // Pointer back to the instance.
  } hap_char_;

 private:
  const Service *parent_ = nullptr;

  Characteristic(const Characteristic &other) = delete;
};

class StringCharacteristic : public Characteristic {
 public:
  StringCharacteristic(uint16_t iid, const HAPUUID *type, uint16_t max_length,
                       const std::string &initial_value,
                       const char *debug_description = nullptr);
  virtual ~StringCharacteristic();

  const std::string &value() const;
  void set_value(const std::string &value);

 private:
  static HAPError HandleReadCB(
      HAPAccessoryServerRef *server,
      const HAPStringCharacteristicReadRequest *request, char *value,
      size_t maxValueBytes, void *context);

  std::string value_;
};

// Template class that can be used to create scalar-value characteristics.
template <class ValType, class HAPBaseClass, class HAPReadRequestType,
          class HAPWriteRequestType>
struct ScalarCharacteristic : public Characteristic {
 public:
  typedef std::function<HAPError(HAPAccessoryServerRef *server,
                                 const HAPReadRequestType *request,
                                 ValType *value)>
      ReadHandler;
  typedef std::function<HAPError(HAPAccessoryServerRef *server,
                                 const HAPWriteRequestType *request,
                                 ValType value)>
      WriteHandler;

  ScalarCharacteristic(HAPCharacteristicFormat format, uint16_t iid,
                       const HAPUUID *type, ReadHandler read_handler,
                       bool supports_notification,
                       WriteHandler write_handler = nullptr,
                       const char *debug_description = nullptr)
      : Characteristic(iid, format, type, debug_description),
        read_handler_(read_handler),
        write_handler_(write_handler) {
    HAPBaseClass *c = reinterpret_cast<HAPBaseClass *>(&hap_char_.char_);
    if (read_handler) {
      c->properties.readable = true;
      c->callbacks.handleRead = ScalarCharacteristic::HandleReadCB;
      c->properties.supportsEventNotification = supports_notification;
      c->properties.ble.supportsBroadcastNotification = true;
      c->properties.ble.supportsDisconnectedNotification = true;
    }
    if (write_handler) {
      c->properties.writable = true;
      c->callbacks.handleWrite = ScalarCharacteristic::HandleWriteCB;
    }
  }

  virtual ~ScalarCharacteristic() {
  }

 private:
  static HAPError HandleReadCB(HAPAccessoryServerRef *server,
                               const HAPReadRequestType *request,
                               ValType *value, void *context) {
    auto *hci = reinterpret_cast<const HAPCharacteristicWithInstance *>(
        request->characteristic);
    auto *c = static_cast<const ScalarCharacteristic *>(hci->inst);
    (void) context;
    return const_cast<ScalarCharacteristic *>(c)->read_handler_(server, request,
                                                                value);
  }
  static HAPError HandleWriteCB(HAPAccessoryServerRef *server,
                                const HAPWriteRequestType *request,
                                ValType value, void *context) {
    auto *hci = reinterpret_cast<const HAPCharacteristicWithInstance *>(
        request->characteristic);
    auto *c = static_cast<const ScalarCharacteristic *>(hci->inst);
    (void) context;
    return const_cast<ScalarCharacteristic *>(c)->write_handler_(
        server, request, value);
  }

  const ReadHandler read_handler_;
  const WriteHandler write_handler_;
};

struct BoolCharacteristic
    : public ScalarCharacteristic<bool, HAPBoolCharacteristic,
                                  HAPBoolCharacteristicReadRequest,
                                  HAPBoolCharacteristicWriteRequest> {
 public:
  BoolCharacteristic(uint16_t iid, const HAPUUID *type,
                     ReadHandler read_handler, bool supports_notification,
                     WriteHandler write_handler = nullptr,
                     const char *debug_description = nullptr)
      : ScalarCharacteristic(kHAPCharacteristicFormat_Bool, iid, type,
                             read_handler, supports_notification, write_handler,
                             debug_description) {
  }
  virtual ~BoolCharacteristic() {
  }
};

class UInt8Characteristic
    : public ScalarCharacteristic<uint8_t, HAPUInt8Characteristic,
                                  HAPUInt8CharacteristicReadRequest,
                                  HAPUInt8CharacteristicWriteRequest> {
 public:
  UInt8Characteristic(uint16_t iid, const HAPUUID *type, uint8_t min,
                      uint8_t max, uint8_t step, ReadHandler read_handler,
                      bool supports_notification,
                      WriteHandler write_handler = nullptr,
                      const char *debug_description = nullptr)
      : ScalarCharacteristic(kHAPCharacteristicFormat_UInt8, iid, type,
                             read_handler, supports_notification, write_handler,
                             debug_description) {
    HAPUInt8Characteristic *c = &hap_char_.char_.uint8;
    c->constraints.minimumValue = min;
    c->constraints.maximumValue = max;
    c->constraints.stepValue = step;
  }
  virtual ~UInt8Characteristic() {
  }
};

}  // namespace hap
}  // namespace shelly

#ifdef __clang__
#pragma clang diagnostic pop
#endif
