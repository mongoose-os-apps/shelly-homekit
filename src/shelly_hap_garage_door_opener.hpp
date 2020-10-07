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
#include "mgos_timers.hpp"

#include "shelly_common.hpp"
#include "shelly_component.hpp"
#include "shelly_hap_chars.hpp"
#include "shelly_hap_service.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"

namespace shelly {
namespace hap {

// Common base for Switch, Outlet and Lock services.
class GarageDoorOpener : public Component, public Service {
 public:
  GarageDoorOpener(int id, Input *in, Output *out, struct mgos_config_gdo *cfg);
  virtual ~GarageDoorOpener();

  Status Init() override;

  // Component interface impl.
  Type type() const override;
  StatusOr<std::string> GetInfo() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;

 private:
  // NB: Values correspond to HAP Current Door State values.
  enum class State {
    kOpen = 0,
    kClosed = 1,
    kOpening = 2,
    kClosing = 3,
  };

  static const char *StateStr(State state);

  void SetCurState(State new_state);
  void SetTgtState(State new_state, const char *source);

  void RunOnce();

  Input *in_close_;
  Output *out_;
  struct mgos_config_gdo *cfg_;

  Input::HandlerID in_close_handler_ = Input::kInvalidHandlerID;
  mgos::ScopedTimer state_timer_;

  Characteristic *cur_state_char_ = nullptr;
  Characteristic *tgt_state_char_ = nullptr;

  State cur_state_;
  State tgt_state_;
  int64_t begin_ = 0;
};

}  // namespace hap
}  // namespace shelly
