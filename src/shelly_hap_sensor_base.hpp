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

#include "mgos_hap_service.hpp"
#include "mgos_sys_config.h"
#include "mgos_timers.hpp"

#include "shelly_common.hpp"
#include "shelly_component.hpp"
#include "shelly_input.hpp"

namespace shelly {
namespace hap {

class SensorBase : public Component, public mgos::hap::Service {
 public:
  enum class InMode {
    kLevel = 0,
    kPulse = 1,
    kMax,
  };

  SensorBase(int id, Input *in, struct mgos_config_in_sensor *cfg,
             uint16_t iid_base, const HAPUUID *type,
             const char *debug_description);
  virtual ~SensorBase();

  // Component interface impl.
  virtual Status Init() override;
  std::string name() const override;
  StatusOr<std::string> GetInfo() const override;
  StatusOr<std::string> GetInfoJSON() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;
  Status SetState(const std::string &state_json) override;

 protected:
  HAPError BoolStateCharRead(HAPAccessoryServerRef *,
                             const HAPBoolCharacteristicReadRequest *,
                             bool *value);
  bool state_ = false;

 private:
  void InputEventHandler(Input::Event ev, bool state);
  void SetInternalState(bool motion_detected);
  void AutoOffTimerCB();

  Input *const in_;
  struct mgos_config_in_sensor *cfg_;

  Input::HandlerID handler_id_ = Input::kInvalidHandlerID;

  double last_ev_ts_ = 0;
  mgos::ScopedTimer auto_off_timer_;

  SensorBase(const SensorBase &other) = delete;
};

}  // namespace hap
}  // namespace shelly
