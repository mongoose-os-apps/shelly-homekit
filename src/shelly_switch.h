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

#include <memory>
#include <vector>

#include "mgos_sys_config.h"
#include "mgos_timers.h"

#include "shelly_common.h"
#include "shelly_component.h"
#include "shelly_hap_chars.h"
#include "shelly_hap_service.h"
#include "shelly_input.h"
#include "shelly_output.h"
#include "shelly_pm.h"

namespace shelly {

// Common base for Switch, Outlet and Lock services.
class ShellySwitch : public Component, public hap::Service {
 public:
  enum class InMode {
    kMomentary = 0,
    kToggle = 1,
    kEdge = 2,
    kDetached = 3,
  };

  enum class InitialState {
    kOff = 0,
    kOn = 1,
    kLast = 2,
    kInput = 3,
  };

  ShellySwitch(int id, Input *in, Output *out, PowerMeter *out_pm,
               struct mgos_config_sw *cfg, HAPAccessoryServerRef *server,
               const HAPAccessory *accessory);
  virtual ~ShellySwitch();

  // Component interface impl.
  Type type() const override;
  StatusOr<std::string> GetInfo() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;

  virtual Status Init() override;

  void SetState(bool new_state, const char *source);

 protected:
  void InputEventHandler(Input::Event ev, bool state);

  void SetStateInternal(bool new_state, const char *source, bool is_auto_off);

  static void AutoOffTimerCB(void *ctx);

  void SaveState();

  Input *const in_;
  Output *const out_;
  PowerMeter *const out_pm_;
  struct mgos_config_sw *cfg_;
  HAPAccessoryServerRef *const server_;
  const HAPAccessory *const accessory_;

  Input::HandlerID handler_id_ = Input::kInvalidHandlerID;
  HAPCharacteristic *state_notify_char_ = nullptr;
  HAPCharacteristic *tgt_state_notify_char_ = nullptr;

  mgos_timer_id auto_off_timer_id_ = MGOS_INVALID_TIMER_ID;

  ShellySwitch(const ShellySwitch &other) = delete;
};

}  // namespace shelly
