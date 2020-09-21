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

#include "shelly_hap_window_covering.hpp"

#include "mgos.h"

namespace shelly {
namespace hap {

WindowCovering::WindowCovering(int id, Input *in0, Input *in1, Output *out0,
                               Output *out1, PowerMeter *pm0, PowerMeter *pm1,
                               struct mgos_config_wc *cfg)
    : Component(id),
      Service((SHELLY_HAP_IID_BASE_WINDOW_COVERING +
               (SHELLY_HAP_IID_STEP_WINDOW_COVERING * (id - 1))),
              &kHAPServiceType_WindowCovering,
              kHAPServiceDebugDescription_WindowCovering),
      inputs_({in0, in1}),
      outputs_({out0, out1}),
      pms_({pm0, pm1}),
      cfg_(cfg) {
}

WindowCovering::~WindowCovering() {
  for (size_t i = 0; i < inputs_.size(); i++) {
    auto *in = inputs_[i];
    if (in == nullptr) continue;
    in->RemoveHandler(input_handlers_[i]);
  }
}

Status WindowCovering::Init() {
  uint16_t iid = svc_.iid + 1;
  // Name
  AddNameChar(iid++, cfg_->name);
  // Target Position
  AddChar(new UInt8Characteristic(
      iid++, &kHAPCharacteristicType_TargetPosition, 0, 100, 1,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        *value = tgt_pos_;
        return kHAPError_None;
      },
      true /* supports_notification */,
      std::bind(&WindowCovering::HandleTargetPositionWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_TargetPosition));
  // Current Position
  AddChar(new UInt8Characteristic(
      iid++, &kHAPCharacteristicType_CurrentPosition, 0, 100, 1,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        *value = cur_pos_;
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_CurrentPosition));
  // Position State
  AddChar(new UInt8Characteristic(
      iid++, &kHAPCharacteristicType_PositionState, 0, 2, 1,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        *value = 2; /* Stopped. TODO */
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_PositionState));
  // Hold Position
  AddChar(new BoolCharacteristic(
      iid++, &kHAPCharacteristicType_HoldPosition, nullptr /* read_handler */,
      false /* supports_notification */,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicWriteRequest *,
             bool value) {
        if (value) {
          // Stop moving, TODO
        }
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_HoldPosition));
  // Obstruction Detected
  AddChar(new BoolCharacteristic(
      iid++, &kHAPCharacteristicType_ObstructionDetected,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicReadRequest *,
             bool *value) {
        *value = false; /* TODO */
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_ObstructionDetected));

  for (size_t i = 0; i < inputs_.size(); i++) {
    auto *in = inputs_[i];
    if (in != nullptr) {
      auto handler_id = in->AddHandler(
          std::bind(&WindowCovering::HandleInputEvent, this, i, _1, _2));
      input_handlers_.push_back(handler_id);
    } else {
      input_handlers_.push_back(Input::kInvalidHandlerID);
    }
  }
  return Status::OK();
}

StatusOr<std::string> WindowCovering::GetInfo() const {
  return mgos::JSONPrintStringf("{id: %d, type: %d, name: %Q}", id(), type(),
                                cfg_->name);
}

Status WindowCovering::SetConfig(const std::string &config_json,
                                 bool *restart_required) {
  return Status::OK();
}

void WindowCovering::HandleInputEvent(int index, Input::Event ev, bool state) {
}

HAPError WindowCovering::HandleTargetPositionWrite(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicWriteRequest *request, uint8_t value) {
  LOG(LL_INFO, ("%d: Target position: %d", id(), value));
  tgt_pos_ = value;
  return kHAPError_None;
}

}  // namespace hap
}  // namespace shelly
