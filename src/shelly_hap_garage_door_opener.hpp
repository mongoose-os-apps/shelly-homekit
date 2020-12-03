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

#include "mgos_hap_chars.hpp"
#include "mgos_hap_service.hpp"
#include "mgos_sys_config.h"
#include "mgos_timers.hpp"

#include "shelly_common.hpp"
#include "shelly_component.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"

namespace shelly {
namespace hap {

// Common base for Switch, Outlet and Lock services.
class GarageDoorOpener : public Component, public mgos::hap::Service {
 public:
  GarageDoorOpener(int id, Input *in_close, Input *in_open, Output *out_open,
                   Output *out_close, struct mgos_config_gdo *cfg);
  virtual ~GarageDoorOpener();

  // Component interface impl.
  Type type() const override;
  std::string name() const override;
  Status Init() override;
  StatusOr<std::string> GetInfo() const override;
  StatusOr<std::string> GetInfoJSON() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;

 private:
  // NB: Values correspond to HAP Current Door State values.
  enum class State {
    kOpen = 0,
    kClosed = 1,
    kOpening = 2,
    kClosing = 3,
    kStopped = 4,
  };

  static const char *StateStr(State state);

  void GetInputsState(int *is_closed, int *is_open) const;
  void SetCurState(State new_state);
  void SetTgtState(State new_state, const char *source);
  void ToggleState(const char *source);
  HAPError HAPTgtStateWrite(HAPAccessoryServerRef *svr,
                            const HAPUInt8CharacteristicWriteRequest *req,
                            uint8_t value);

  void RunOnce();

  Input *in_close_, *in_open_;
  Output *out_close_, *out_open_;
  struct mgos_config_gdo *cfg_;

  mgos::ScopedTimer state_timer_;

  mgos::hap::Characteristic *cur_state_char_ = nullptr;
  mgos::hap::Characteristic *tgt_state_char_ = nullptr;
  mgos::hap::Characteristic *obst_char_ = nullptr;

  State cur_state_;
  State tgt_state_;
  State pre_stopped_state_;
  int64_t begin_ = 0;
  bool obstruction_detected_ = false;
};

}  // namespace hap
}  // namespace shelly
