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

namespace shelly {

HAPSwitch::HAPSwitch(Input *in, Output *out, const struct mgos_config_sw *cfg)
    : in_(in), out_(out), cfg_(cfg) {
}

HAPSwitch::~HAPSwitch() {
}

const HAPService *HAPSwitch::GetHAPService() const {
  return &svc_;
}

StatusOr<std::string> HAPSwitch::GetInfo() const {
  return mgos::JSONPrintfString(
      "{id: %d, name: %Q, type: %d, in_mode: %d, initial: %d, "
      "state: %B, auto_off: %B, auto_off_delay: %.3f"
      ", apower: %.3f, aenergy: %.3f"
      "}",
      cfg_->id, (cfg_->name ? cfg_->name : ""), cfg_->svc_type, cfg_->in_mode,
      cfg_->initial_state, out_->GetState(), cfg_->auto_off,
      cfg_->auto_off_delay);
  (void) in_;
}

}  // namespace shelly
