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
#include "shelly_pm.hpp"

namespace shelly {
namespace hap {

// Common base for Switch, Outlet and Lock services.
class WindowCovering : public Component, public Service {
 public:
  WindowCovering(int id, Input *in0, Input *in1, Output *out0, Output *out1,
                 PowerMeter *pm0, PowerMeter *pm1, struct mgos_config_wc *cfg);
  virtual ~WindowCovering();

  Status Init() override;

  // Component interface impl.
  Type type() const override {
    return Type::kWindowCovering;
  }
  StatusOr<std::string> GetInfo() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;

 private:
  enum class State {
    kNone = -1,
    kIdle = 0,
    // Calibration states.
    kPreCal0 = 10,
    kCal0 = 11,
    kPostCal0 = 12,
    kPreCal1 = 13,
    kCal1 = 14,
    kPostCal1 = 15,
    // Movement states
    kMove = 20,
    kMoving = 21,
    kStop = 22,
    kStopping = 23,
    // Error states
    kError = 100,
  };

  enum class Direction {
    kNone = 0,
    kOpen = 1,
    kClose = 2,
  };

  static constexpr float kNotSet = -1;
  static constexpr float kFullyOpen = 100;
  static constexpr float kFullyClosed = 0;
  static constexpr int kOpenOutIdx = 0;
  static constexpr int kCloseOutIdx = 1;

  static float TrimPos(float pos);

  static const char *StateStr(State state);

  void SaveState();

  void SetState(State new_state);
  void SetCurPos(float new_cur_pos);
  void SetTgtPos(float new_tgt_pos, const char *src);

  void HAPSetTgtPos(float value);

  Direction GetDesiredMoveDirection();
  void Move(Direction dir);

  void RunOnce();

  void HandleOpenInputEvent(Input::Event ev, bool state);
  void HandleCloseInputEvent(Input::Event ev, bool state);
  void HandleInputEventCommon();

  Input *in_open_, *in_close_;
  Output *out_open_, *out_close_;
  PowerMeter *pm_open_, *pm_close_;
  struct mgos_config_wc *cfg_;

  std::vector<Input::HandlerID> input_handlers_;
  Input::HandlerID in_open_handler_ = Input::kInvalidHandlerID;
  Input::HandlerID in_close_handler_ = Input::kInvalidHandlerID;
  mgos::ScopedTimer state_timer_;

  Characteristic *cur_pos_char_ = nullptr;
  Characteristic *tgt_pos_char_ = nullptr;
  Characteristic *pos_state_char_ = nullptr;

  float cur_pos_ = kNotSet;
  float tgt_pos_ = kNotSet;

  State state_ = State::kIdle;
  State tgt_state_ = State::kNone;

  int p_num_ = 0;
  float p_sum_ = 0;
  int64_t begin_ = 0;
  float move_start_pos_ = 0;
  float last_notify_pos_ = 0;
  float move_ms_per_pct_ = 0;
  Direction moving_dir_ = Direction::kNone;
  Direction ext_move_dir_ = Direction::kNone;
};

}  // namespace hap
}  // namespace shelly
