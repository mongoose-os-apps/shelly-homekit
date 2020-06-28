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
 *limitations under the License.
 */

#include "shelly_sw_service_internal.h"

#include "mgos.h"

const HAPCharacteristic *shelly_sw_name_char(uint16_t iid) {
  HAPStringCharacteristic *c =
      (HAPStringCharacteristic *) calloc(1, sizeof(*c));
  if (c == nullptr) return nullptr;
  *c = (const HAPStringCharacteristic){
      .format = kHAPCharacteristicFormat_String,
      .iid = iid,
      .characteristicType = &kHAPCharacteristicType_Name,
      .debugDescription = kHAPCharacteristicDebugDescription_Name,
      .manufacturerDescription = nullptr,
      .properties =
          {
              .readable = true,
              .writable = false,
              .supportsEventNotification = false,
              .hidden = false,
              .requiresAdminPermissions = false,
              .readRequiresAdminPermissions = false,
              .writeRequiresAdminPermissions = false,
              .requiresTimedWrite = false,
              .supportsAuthorizationData = false,
              .ip =
                  {
                      .controlPoint = false,
                      .supportsWriteResponse = false,
                  },
              .ble =
                  {
                      .supportsBroadcastNotification = false,
                      .supportsDisconnectedNotification = false,
                      .readableWithoutSecurity = false,
                      .writableWithoutSecurity = false,
                  },
          },
      .constraints = {.maxLength = 64},
      .callbacks =
          {
              .handleRead = HAPHandleNameRead,
              .handleWrite = nullptr,
              .handleSubscribe = nullptr,
              .handleUnsubscribe = nullptr,
          },
  };
  return c;
};

HAPError shelly_sw_handle_on_read(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicReadRequest *request, bool *value,
    void *context) {
  struct shelly_sw_service_ctx *ctx = find_ctx(request->service);
  const struct mgos_config_sw *cfg = ctx->cfg;
  *value = ctx->info.state;
  LOG(LL_INFO, ("%s: READ -> %d", cfg->name, ctx->info.state));
  ctx->hap_server = server;
  ctx->hap_accessory = request->accessory;
  (void) context;
  return kHAPError_None;
}

HAPError shelly_sw_handle_on_write(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicWriteRequest *request, bool value,
    void *context) {
  struct shelly_sw_service_ctx *ctx = find_ctx(request->service);
  ctx->hap_server = server;
  ctx->hap_accessory = request->accessory;
  shelly_sw_set_state_ctx(ctx, value, "HAP");
  (void) context;
  return kHAPError_None;
}

const HAPCharacteristic *shelly_sw_on_char(uint16_t iid) {
  HAPBoolCharacteristic *c = (HAPBoolCharacteristic *) calloc(1, sizeof(*c));
  if (c == nullptr) return nullptr;
  *c = (const HAPBoolCharacteristic){
      .format = kHAPCharacteristicFormat_Bool,
      .iid = iid,
      .characteristicType = &kHAPCharacteristicType_On,
      .debugDescription = kHAPCharacteristicDebugDescription_On,
      .manufacturerDescription = nullptr,
      .properties =
          {
              .readable = true,
              .writable = true,
              .supportsEventNotification = true,
              .hidden = false,
              .requiresAdminPermissions = false,
              .readRequiresAdminPermissions = false,
              .writeRequiresAdminPermissions = false,
              .requiresTimedWrite = false,
              .supportsAuthorizationData = false,
              .ip =
                  {
                      .controlPoint = false,
                      .supportsWriteResponse = false,
                  },
              .ble =
                  {
                      .supportsBroadcastNotification = true,
                      .supportsDisconnectedNotification = true,
                      .readableWithoutSecurity = false,
                      .writableWithoutSecurity = false,
                  },
          },
      .callbacks =
          {
              .handleRead = shelly_sw_handle_on_read,
              .handleWrite = shelly_sw_handle_on_write,
              .handleSubscribe = nullptr,
              .handleUnsubscribe = nullptr,
          },
  };
  return c;
};

HAPError shelly_sw_handle_lock_cur_state_read(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicReadRequest *request, uint8_t *value,
    void *context) {
  struct shelly_sw_service_ctx *ctx = find_ctx(request->service);
  *value = (ctx->info.state ? 0 : 1);
  ctx->hap_server = server;
  ctx->hap_accessory = request->accessory;
  (void) context;
  return kHAPError_None;
}

const HAPCharacteristic *shelly_sw_lock_cur_state(uint16_t iid) {
  HAPUInt8Characteristic *c = (HAPUInt8Characteristic *) calloc(1, sizeof(*c));
  if (c == nullptr) return nullptr;
  *c = (const HAPUInt8Characteristic){
      .format = kHAPCharacteristicFormat_UInt8,
      .iid = iid,
      .characteristicType = &kHAPCharacteristicType_LockCurrentState,
      .debugDescription = kHAPCharacteristicDebugDescription_LockCurrentState,
      .manufacturerDescription = nullptr,
      .properties =
          {
              .readable = true,
              .writable = false,
              .supportsEventNotification = true,
              .hidden = false,
              .requiresAdminPermissions = false,
              .readRequiresAdminPermissions = false,
              .writeRequiresAdminPermissions = false,
              .requiresTimedWrite = false,
              .supportsAuthorizationData = false,
              .ip =
                  {
                      .controlPoint = false,
                      .supportsWriteResponse = false,
                  },
              .ble =
                  {
                      .supportsBroadcastNotification = true,
                      .supportsDisconnectedNotification = true,
                      .readableWithoutSecurity = false,
                      .writableWithoutSecurity = false,
                  },
          },
      .units = kHAPCharacteristicUnits_None,
      .constraints =
          {
              .minimumValue = 0,
              .maximumValue = 3,
              .stepValue = 1,
              .validValues = nullptr,
              .validValuesRanges = nullptr,
          },
      .callbacks =
          {
              .handleRead = shelly_sw_handle_lock_cur_state_read,
              .handleWrite = nullptr,
              .handleSubscribe = nullptr,
              .handleUnsubscribe = nullptr,
          },
  };
  return c;
};

HAPError shelly_sw_handle_lock_tgt_state_write(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicWriteRequest *request, uint8_t value,
    void *context) {
  struct shelly_sw_service_ctx *ctx = find_ctx(request->service);
  ctx->hap_server = server;
  ctx->hap_accessory = request->accessory;
  HAPAccessoryServerRaiseEvent(ctx->hap_server,
                               ctx->hap_service->characteristics[2],
                               ctx->hap_service, ctx->hap_accessory);
  shelly_sw_set_state_ctx(ctx, (value == 0), "HAP");
  (void) context;
  return kHAPError_None;
}

const HAPCharacteristic *shelly_sw_lock_tgt_state(uint16_t iid) {
  HAPUInt8Characteristic *c = (HAPUInt8Characteristic *) calloc(1, sizeof(*c));
  if (c == nullptr) return nullptr;
  *c = (const HAPUInt8Characteristic){
      .format = kHAPCharacteristicFormat_UInt8,
      .iid = iid,
      .characteristicType = &kHAPCharacteristicType_LockTargetState,
      .debugDescription = kHAPCharacteristicDebugDescription_LockTargetState,
      .manufacturerDescription = nullptr,
      .properties =
          {
              .readable = true,
              .writable = true,
              .supportsEventNotification = true,
              .hidden = false,
              .requiresAdminPermissions = false,
              .readRequiresAdminPermissions = false,
              .writeRequiresAdminPermissions = false,
              .requiresTimedWrite = false,
              .supportsAuthorizationData = false,
              .ip =
                  {
                      .controlPoint = false,
                      .supportsWriteResponse = false,
                  },
              .ble =
                  {
                      .supportsBroadcastNotification = true,
                      .supportsDisconnectedNotification = true,
                      .readableWithoutSecurity = false,
                      .writableWithoutSecurity = false,
                  },
          },
      .units = kHAPCharacteristicUnits_None,
      .constraints =
          {
              .minimumValue = 0,
              .maximumValue = 1,
              .stepValue = 1,
              .validValues = nullptr,
              .validValuesRanges = nullptr,
          },
      .callbacks =
          {
              .handleRead = shelly_sw_handle_lock_cur_state_read,
              .handleWrite = shelly_sw_handle_lock_tgt_state_write,
              .handleSubscribe = nullptr,
              .handleUnsubscribe = nullptr,
          },
  };
  return c;
};

HAPError shelly_sw_handle_on_read_in_use(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicReadRequest *request, bool *value,
    void *context) {
  *value = true;
  (void) server;
  (void) request;
  (void) context;
  return kHAPError_None;
}

const HAPCharacteristic *shelly_sw_in_use_char(uint16_t iid) {
  HAPBoolCharacteristic *c = (HAPBoolCharacteristic *) calloc(1, sizeof(*c));
  if (c == nullptr) return nullptr;
  *c = (const HAPBoolCharacteristic){
      .format = kHAPCharacteristicFormat_Bool,
      .iid = iid,
      .characteristicType = &kHAPCharacteristicType_OutletInUse,
      .debugDescription = kHAPCharacteristicDebugDescription_OutletInUse,
      .manufacturerDescription = nullptr,
      .properties =
          {
              .readable = true,
              .writable = false,
              .supportsEventNotification = false,
              .hidden = false,
              .requiresAdminPermissions = false,
              .readRequiresAdminPermissions = false,
              .writeRequiresAdminPermissions = false,
              .requiresTimedWrite = false,
              .supportsAuthorizationData = false,
              .ip =
                  {
                      .controlPoint = false,
                      .supportsWriteResponse = false,
                  },
              .ble =
                  {
                      .supportsBroadcastNotification = false,
                      .supportsDisconnectedNotification = false,
                      .readableWithoutSecurity = false,
                      .writableWithoutSecurity = false,
                  },
          },
      .callbacks =
          {
              .handleRead = shelly_sw_handle_on_read_in_use,
              .handleWrite = nullptr,
              .handleSubscribe = nullptr,
              .handleUnsubscribe = nullptr,
          },
  };
  return c;
};
