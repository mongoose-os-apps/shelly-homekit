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

class Characteristic {
 public:
  Characteristic();
  virtual ~Characteristic();

  virtual HAPCharacteristic *GetBase() = 0;

 protected:
  static Characteristic *FindInstance(const HAPCharacteristic *base);

 private:
  static std::vector<Characteristic *> instances_;
};

class StringCharacteristic : public Characteristic {
 public:
  StringCharacteristic(uint16_t iid, const HAPUUID *type, uint16_t max_length,
                       const std::string &initial_value,
                       const char *debug_description = nullptr);
  virtual ~StringCharacteristic();

  HAPCharacteristic *GetBase() override;

  void SetValue(const std::string &value);
  const std::string &GetValue() const;

 private:
  static HAPError HandleReadCB(
      HAPAccessoryServerRef *server,
      const HAPStringCharacteristicReadRequest *request, char *value,
      size_t maxValueBytes, void *context);

  HAPStringCharacteristic base_;

  std::string value_;

  StringCharacteristic(const StringCharacteristic &other) = delete;
};

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
      : read_handler_(read_handler), write_handler_(write_handler) {
    std::memset(&base_, 0, sizeof(base_));
    base_.format = format;
    base_.iid = iid;
    base_.characteristicType = type;
    base_.debugDescription = debug_description;
    base_.properties.readable = true;
    base_.properties.supportsEventNotification = supports_notification;
    base_.callbacks.handleRead = ScalarCharacteristic::HandleReadCB;
    if (write_handler) {
      base_.properties.writable = true;
      /* ???
      base_.properties.ble.supportsBroadcastNotification = true;
      base_.properties.ble.supportsDisconnectedNotification = true;
      */
      base_.callbacks.handleWrite = ScalarCharacteristic::HandleWriteCB;
    }
  }

  virtual ~ScalarCharacteristic() {
  }

  HAPCharacteristic *GetBase() override {
    return &base_;
  }

 protected:
  HAPBaseClass base_;

 private:
  static HAPError HandleReadCB(HAPAccessoryServerRef *server,
                               const HAPReadRequestType *request,
                               ValType *value, void *context) {
    ScalarCharacteristic *c = (ScalarCharacteristic *) FindInstance(
        (const HAPCharacteristic *) request->characteristic);
    (void) context;
    return c->read_handler_(server, request, value);
  }
  static HAPError HandleWriteCB(HAPAccessoryServerRef *server,
                                const HAPWriteRequestType *request,
                                ValType value, void *context) {
    ScalarCharacteristic *c = (ScalarCharacteristic *) FindInstance(
        (const HAPCharacteristic *) request->characteristic);
    (void) context;
    return c->write_handler_(server, request, value);
  }

  const ReadHandler read_handler_;
  const WriteHandler write_handler_;

  ScalarCharacteristic(const ScalarCharacteristic &other) = delete;
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
    base_.constraints.minimumValue = min;
    base_.constraints.maximumValue = max;
    base_.constraints.stepValue = step;
  }
  virtual ~UInt8Characteristic() {
  }
};

}  // namespace hap
}  // namespace shelly

#ifdef __clang__
#pragma clang diagnostic pop
#endif
