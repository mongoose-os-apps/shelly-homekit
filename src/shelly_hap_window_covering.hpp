/*
 * Copyright (c) Shelly-HomeKit Contributors
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

#include "mgos_hap.hpp"
#include "mgos_sys_config.h"
#include "mgos_timers.hpp"

#include "shelly_common.hpp"
#include "shelly_component.hpp"
#include "shelly_input.hpp"
#include "shelly_output.hpp"
#include "shelly_pm.hpp"

namespace shelly {
namespace hap {

// Common base for Switch, Outlet and Lock services.
class WindowCovering : public Component, public mgos::hap::Service {
 public:
  enum class InMode {
    kSeparateMomentary = 0,
    kSeparateToggle = 1,
    kSingle = 2,
    kDetached = 3,
  };

  WindowCovering(int id, Input *in0, Input *in1, Output *out0, Output *out1,
                 PowerMeter *pm0, PowerMeter *pm1, struct mgos_config_wc *cfg);
  virtual ~WindowCovering();

  // Component interface impl.
  Type type() const override;
  std::string name() const override;
  Status Init() override;
  StatusOr<std::string> GetInfo() const override;
  StatusOr<std::string> GetInfoJSON() const override;
  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override;
  Status SetState(const std::string &state_json) override;
  bool IsIdle() override;

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
    kRampUp = 22,
    kMoving = 23,
    kStop = 24,
    kStopping = 25,
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

  void SetInternalState(State new_state);
  void SetCurPos(float new_cur_pos, float p);
  void SetTgtPos(float new_tgt_pos, const char *src);

  void HAPSetTgtPos(float value);

  Direction GetDesiredMoveDirection();
  void Move(Direction dir);

  void RunOnce();

  void HandleInputEvent01(Direction dir, Input::Event ev, bool state);
  void HandleInputEvent2(Input::Event ev, bool state);
  void HandleInputEventNotCalibrated();
  void HandleInputSingle(const char *src, Direction *last_move_dir);

  Input *in_open_, *in_close_;
  Output *out_open_, *out_close_;
  PowerMeter *pm_open_, *pm_close_;
  struct mgos_config_wc *cfg_;

  Input::HandlerID in_open_handler_ = Input::kInvalidHandlerID;
  Input::HandlerID in_close_handler_ = Input::kInvalidHandlerID;
  mgos::Timer state_timer_;

  mgos::hap::Characteristic *cur_pos_char_ = nullptr;
  mgos::hap::Characteristic *tgt_pos_char_ = nullptr;
  mgos::hap::Characteristic *pos_state_char_ = nullptr;
  mgos::hap::Characteristic *obst_char_ = nullptr;

  float cur_pos_ = kNotSet;
  float tgt_pos_ = kNotSet;

  State state_ = State::kIdle;
  State tgt_state_ = State::kNone;

  int p_num_ = 0;
  float p_sum_ = 0;
  int64_t begin_ = 0;
  float move_start_pos_ = 0;
  float move_ms_per_pct_ = 0;
  float move_limit_ms_per_pct_ = 0;
  bool obstruction_detected_ = false;
  int64_t last_hap_set_tgt_pos_ = 0;
  Direction moving_dir_ = Direction::kNone;
  Direction last_ext_move_dir_ = Direction::kNone;
  Direction last_hap_move_dir_ = Direction::kNone;
};

}  // namespace hap
}  // namespace shelly
