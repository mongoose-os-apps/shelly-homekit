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

#include "shelly_hap_switch.h"

#include "mgos.h"

#include "shelly_hap_chars.h"

#define IID_BASE_SWITCH 0x100
#define IID_STEP_SWITCH 4
#define IID_BASE_OUTLET 0x200
#define IID_STEP_OUTLET 5
#define IID_BASE_LOCK 0x300
#define IID_STEP_LOCK 4

namespace shelly {

HAPSwitch::HAPSwitch(Input *in, Output *out, PowerMeter *out_pm,
                     const struct mgos_config_sw *cfg)
    : in_(in), out_(out), out_pm_(out_pm), cfg_(cfg) {
  if (in_ != nullptr) {
    in_->SetHandler(std::bind(&HAPSwitch::InputEventHandler, this, _1, _2));
  }
}

HAPSwitch::~HAPSwitch() {
}

const HAPService *HAPSwitch::GetHAPService() const {
  return &svc_;
}

StatusOr<std::string> HAPSwitch::GetInfo() const {
  std::string res = mgos::JSONPrintfString(
      "{id: %d, name: %Q, type: %d, in_mode: %d, initial: %d, "
      "state: %B, auto_off: %B, auto_off_delay: %.3f",
      cfg_->id, (cfg_->name ? cfg_->name : ""), cfg_->svc_type, cfg_->in_mode,
      cfg_->initial_state, out_->GetState(), cfg_->auto_off,
      cfg_->auto_off_delay);
  if (out_pm_ != nullptr) {
    auto power = out_pm_->GetPowerW();
    auto energy = out_pm_->GetEnergyWH();
    if (power.ok()) {
      res.append(mgos::JSONPrintfString(", apower: %.3f", power.ValueOrDie()));
    }
    if (energy.ok()) {
      res.append(
          mgos::JSONPrintfString(", aenergy: %.3f", energy.ValueOrDie()));
    }
  }
  res.append("}");
  return res;
}

void HAPSwitch::InputEventHandler(Input::Event ev, bool state) {
  (void) ev;
  (void) state;
}

}  // namespace shelly
