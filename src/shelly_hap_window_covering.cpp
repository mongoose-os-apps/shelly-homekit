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

#include "shelly_hap_window_covering.hpp"

#include <cmath>

#include "mgos.hpp"
#include "mgos_system.hpp"

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
      cfg_(cfg),
      state_timer_(std::bind(&WindowCovering::RunOnce, this)),
      cur_pos_(cfg_->current_pos),
      tgt_pos_(cfg_->current_pos),
      move_ms_per_pct_(cfg_->move_time_ms / 100.0),
      move_limit_ms_per_pct_(cfg_->move_time_limit_pos_ms / 100.0) {
  if (!cfg_->swap_inputs) {
    in_open_ = in0;
    in_close_ = in1;
  } else {
    in_open_ = in1;
    in_close_ = in0;
  }
  if (!cfg_->swap_outputs) {
    out_open_ = out0;
    out_close_ = out1;
    pm_open_ = pm0;
    pm_close_ = pm1;
  } else {
    out_open_ = out1;
    out_close_ = out0;
    pm_open_ = pm1;
    pm_close_ = pm0;
  }
}

WindowCovering::~WindowCovering() {
  if (in_open_handler_ != Input::kInvalidHandlerID) {
    in_open_->RemoveHandler(in_open_handler_);
  }
  if (in_close_handler_ != Input::kInvalidHandlerID) {
    in_close_->RemoveHandler(in_close_handler_);
  }
  out_open_->SetState(false, "dtor");
  out_close_->SetState(false, "dtor");
  SaveState();
}

Status WindowCovering::Init() {
  uint16_t iid = svc_.iid + 1;
  // Name
  AddNameChar(iid++, cfg_->name);
  // Target Position
  tgt_pos_char_ = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_TargetPosition, 0, 100, 1,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        *value = tgt_pos_;
        return kHAPError_None;
      },
      true /* supports_notification */,
      [this](HAPAccessoryServerRef *,
             const HAPUInt8CharacteristicWriteRequest *, uint8_t value) {
        // We need to decouple from the current invocation
        // because we may want to raise a notification on the target position
        // and we can't do that within the write callback.
        mgos::InvokeCB(std::bind(&WindowCovering::HAPSetTgtPos, this, value));
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_TargetPosition);
  AddChar(tgt_pos_char_);
  // Current Position
  cur_pos_char_ = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_CurrentPosition, 0, 100, 1,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        *value = cur_pos_;
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_CurrentPosition);
  AddChar(cur_pos_char_);
  // Position State
  pos_state_char_ = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_PositionState, 0, 2, 1,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        switch (moving_dir_) {
          case Direction::kNone:
            *value = kHAPCharacteristicValue_PositionState_Stopped;
            break;
          case Direction::kClose:
            *value = kHAPCharacteristicValue_PositionState_GoingToMinimum;
            break;
          case Direction::kOpen:
            *value = kHAPCharacteristicValue_PositionState_GoingToMaximum;
            break;
        }
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_PositionState);
  AddChar(pos_state_char_);
  // Hold Position
  AddChar(new mgos::hap::BoolCharacteristic(
      iid++, &kHAPCharacteristicType_HoldPosition, nullptr /* read_handler */,
      false /* supports_notification */,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicWriteRequest *,
             bool value) {
        if (value) {
          LOG(LL_INFO, ("WC %d: Hold position", id()));
          SetInternalState(State::kStop);
        }
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_HoldPosition));
  // Obstruction Detected
  obst_char_ = new mgos::hap::BoolCharacteristic(
      iid++, &kHAPCharacteristicType_ObstructionDetected,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicReadRequest *,
             bool *value) {
        *value = obstruction_detected_;
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_ObstructionDetected);
  AddChar(obst_char_);
  switch (static_cast<InMode>(cfg_->in_mode)) {
    case InMode::kSeparateMomentary:
    case InMode::kSeparateToggle:
      in_open_handler_ = in_open_->AddHandler(std::bind(
          &WindowCovering::HandleInputEvent01, this, Direction::kOpen, _1, _2));
      in_close_handler_ =
          in_close_->AddHandler(std::bind(&WindowCovering::HandleInputEvent01,
                                          this, Direction::kClose, _1, _2));
      break;
    case InMode::kSingle:
      in_open_handler_ = in_open_->AddHandler(
          std::bind(&WindowCovering::HandleInputEvent2, this, _1, _2));
      break;
    case InMode::kDetached:
      break;
  }
  if (cfg_->calibrated) {
    LOG(LL_INFO, ("WC %d: mp %.2f, mt_ms %d, cur_pos %.2f", id(),
                  cfg_->move_power, cfg_->move_time_ms, cur_pos_));
  } else if (cfg_->man_cal) {
    LOG(LL_INFO, ("WC %d: mp %.2f, mt_ms %d, mtlpos_ms %d, cur_pos %.2f", id(),
                  cfg_->move_power, cfg_->move_time_ms,
                  cfg_->move_time_limit_pos_ms, cur_pos_));
  } else {
    LOG(LL_INFO, ("WC %d: not calibrated", id()));
  }
  state_timer_.Reset(100, MGOS_TIMER_REPEAT);
  return Status::OK();
}

Component::Type WindowCovering::type() const {
  return Type::kWindowCovering;
}

std::string WindowCovering::name() const {
  return cfg_->name;
}

StatusOr<std::string> WindowCovering::GetInfo() const {
  return mgos::SPrintf(
      "c:%d mp:%.2f mt_ms:%d cp:%.2f tp:%.2f "
      "md:%d lemd:%d lhmd:%d",
      cfg_->calibrated, cfg_->move_power, cfg_->move_time_ms, cur_pos_,
      tgt_pos_, (int) moving_dir_, (int) last_ext_move_dir_,
      (int) last_hap_move_dir_);
}

StatusOr<std::string> WindowCovering::GetInfoJSON() const {
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, "
      "in_mode: %d, swap_inputs: %B, swap_outputs: %B, "
      "cal_done: %B, man_cal: %B, move_time_ms: %d, "
      "move_time_limit_pos_ms: %d, move_power: %d, "
      "state: %d, state_str: %Q, cur_pos: %d, tgt_pos: %d}",
      id(), type(), cfg_->name, cfg_->in_mode, cfg_->swap_inputs,
      cfg_->swap_outputs, cfg_->calibrated, cfg_->man_cal, cfg_->move_time_ms,
      cfg_->move_time_limit_pos_ms, (int) cfg_->move_power, (int) state_,
      StateStr(state_), (int) cur_pos_, (int) tgt_pos_);
}

Status WindowCovering::SetConfig(const std::string &config_json,
                                 bool *restart_required) {
  struct mgos_config_wc cfg = *cfg_;
  cfg.name = nullptr;
  int in_mode = -1, move_time_ms = -1, move_time_limit_pos_ms = -1;
  int8_t man_cal = -1, swap_inputs = -1, swap_outputs = -1;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, in_mode: %d, "
             "man_cal: %B, move_time_ms: %d, "
             "move_time_limit_pos_ms: %d, "
             "swap_inputs: %B, swap_outputs: %B}",
             &cfg.name, &in_mode, &man_cal, &move_time_ms,
             &move_time_limit_pos_ms, &swap_inputs, &swap_outputs);
  mgos::ScopedCPtr name_owner((void *) cfg.name);
  // Validate.
  if (cfg.name != nullptr && strlen(cfg.name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (in_mode > 3) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "in_mode");
  }
  // Apply.
  if (cfg.name != nullptr && strcmp(cfg_->name, cfg.name) != 0) {
    mgos_conf_set_str(&cfg_->name, cfg.name);
    *restart_required = true;
  }
  if (in_mode != -1 && in_mode != cfg_->in_mode) {
    cfg_->in_mode = in_mode;
    *restart_required = true;
  }
  if (man_cal != -1 && man_cal != cfg_->man_cal) {
    cfg_->man_cal = man_cal;
    if (man_cal && cfg_->calibrated) {
      cfg_->calibrated = false;
    }
    *restart_required = true;
  }
  if (move_time_ms != -1 && move_time_ms != cfg_->move_time_ms) {
    cfg_->move_time_ms = move_time_ms;
    *restart_required = true;
  }
  if (move_time_limit_pos_ms != -1 &&
      move_time_limit_pos_ms != cfg_->move_time_limit_pos_ms) {
    cfg_->move_time_limit_pos_ms = move_time_limit_pos_ms;
    *restart_required = true;
  }
  if (swap_inputs != -1 && swap_inputs != cfg_->swap_inputs) {
    cfg_->swap_inputs = swap_inputs;
    *restart_required = true;
  }
  if (swap_outputs != -1 && swap_outputs != cfg_->swap_outputs) {
    cfg_->swap_outputs = swap_outputs;
    // As movement direction is now reversed, position is now incorrect too.
    // Let's re-calibrate.
    cfg_->calibrated = false;
    *restart_required = true;
  }
  return Status::OK();
}

Status WindowCovering::SetState(const std::string &state_json) {
  int state = -2, tgt_pos = -2;
  json_scanf(state_json.c_str(), state_json.size(), "{state: %d, tgt_pos: %d}",
             &state, &tgt_pos);
  if (state != -2 && strcmp(StateStr(static_cast<State>(state)), "???") == 0) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "state");
  }
  if (state >= 0) {
    tgt_state_ = static_cast<State>(state);
    if (state_ != State::kIdle) {
      SetInternalState(State::kStop);
    }
    return Status::OK();
  }
  if (tgt_pos >= 0) {
    SetTgtPos(tgt_pos, "RPC");
  } else if (tgt_pos == -1) {
    RunOnce();
    SetTgtPos(cur_pos_, "RPC");  // Stop
    RunOnce();
  }
  return Status::OK();
}

bool WindowCovering::IsIdle() {
  return (state_ == State::kIdle);
}

// static
const char *WindowCovering::StateStr(State state) {
  switch (state) {
    case State::kNone:
      return "none";
    case State::kIdle:
      return "idle";
    case State::kPreCal0:
      return "precal0";
    case State::kCal0:
      return "cal0";
    case State::kPostCal0:
      return "postcal0";
    case State::kPreCal1:
      return "precal1";
    case State::kCal1:
      return "cal1";
    case State::kPostCal1:
      return "postcal1";
    case State::kMove:
      return "move";
    case State::kRampUp:
      return "rampup";
    case State::kMoving:
      return "moving";
    case State::kStop:
      return "stop";
    case State::kStopping:
      return "stopping";
    case State::kError:
      return "error";
  }
  return "???";
}

// static
float WindowCovering::TrimPos(float pos) {
  if (pos < kFullyClosed) {
    pos = kFullyClosed;
  } else if (pos > kFullyOpen) {
    pos = kFullyOpen;
  }
  return pos;
}

void WindowCovering::SaveState() {
  mgos_sys_config_save(&mgos_sys_config, false /* try_once */, NULL /* msg */);
}

void WindowCovering::SetInternalState(State new_state) {
  if (state_ == new_state) return;
  LOG(LL_INFO, ("WC %d: State: %s -> %s (%d -> %d)", id(), StateStr(state_),
                StateStr(new_state), (int) state_, (int) new_state));
  state_ = new_state;
  begin_ = mgos_uptime_micros();
}

void WindowCovering::SetCurPos(float new_cur_pos, float p) {
  new_cur_pos = TrimPos(new_cur_pos);
  if (new_cur_pos == cur_pos_) return;
  LOG_EVERY_N(LL_INFO, 8,
              ("WC %d: Cur pos %.2f -> %.2f, P = %.2f", id(), cur_pos_,
               new_cur_pos, p));
  cur_pos_ = new_cur_pos;
  cfg_->current_pos = cur_pos_;
  cur_pos_char_->RaiseEvent();
}

void WindowCovering::SetTgtPos(float new_tgt_pos, const char *src) {
  new_tgt_pos = TrimPos(new_tgt_pos);
  if (new_tgt_pos == tgt_pos_) return;
  LOG(LL_INFO,
      ("WC %d: Tgt pos %.2f -> %.2f (%s)", id(), tgt_pos_, new_tgt_pos, src));
  tgt_pos_ = new_tgt_pos;
  tgt_pos_char_->RaiseEvent();
}

// We want tile taps to cycle the open-stop-close-stop sequence.
// Problem is, tile taps behave as "prefer close": Home will send
// 0 ("fully close") if tile is tapped while in the intermediate position.
// We try to detect this case and ignore the target setting, instead
// we use the opposite of last action. This results in more natural and
// intuitive behavior (originally requested in
// https://github.com/mongoose-os-apps/shelly-homekit/issues/181).
// However, this causes issues with automations (reported in
// https://github.com/mongoose-os-apps/shelly-homekit/issues/254).
// To address this, we "expire" last state after 1 minute. This provides
// intuitive behavior within short time span so short-term interactive use
// is unaffected and allow automated changes to work properly.
void WindowCovering::HAPSetTgtPos(float value) {
  // If last action was a while ago, ignore it.
  if (mgos_uptime_micros() - last_hap_set_tgt_pos_ > 60 * 1000000) {
    last_hap_move_dir_ = Direction::kNone;
  }
  LOG(LL_INFO, ("WC %d: HAPSetTgtPos %.2f cur %.2f tgt %.2f lhmd %d", id(),
                value, cur_pos_, tgt_pos_, (int) last_hap_move_dir_));
  // If the specified position is intermediate, just do what we are told.
  if ((value != kFullyClosed && value != kFullyOpen) ||
      last_hap_move_dir_ == Direction::kNone) {
    SetTgtPos(value, "HAP");
    if (value == kFullyClosed) {
      last_hap_move_dir_ = Direction::kClose;
    } else if (value == kFullyOpen) {
      last_hap_move_dir_ = Direction::kOpen;
    } else {
      last_hap_move_dir_ = Direction::kNone;
    }
  } else if ((value == kFullyClosed &&
              (cur_pos_ == kFullyClosed || tgt_pos_ == kFullyClosed)) ||
             (value == kFullyOpen &&
              (cur_pos_ == kFullyOpen || tgt_pos_ == kFullyOpen))) {
    // Nothing to do.
  } else {
    // This is most likely a tap on the tile.
    HandleInputSingle("HAPalt", &last_hap_move_dir_);
  }
  last_hap_set_tgt_pos_ = mgos_uptime_micros();
  // Run state machine immediately to improve reaction time,
  RunOnce();
}

WindowCovering::Direction WindowCovering::GetDesiredMoveDirection() {
  if (tgt_pos_ == kNotSet) return Direction::kNone;
  float pos_diff = tgt_pos_ - cur_pos_;
  if (!(cfg_->calibrated || cfg_->man_cal) || std::abs(pos_diff) < 0.5) {
    return Direction::kNone;
  }
  return (pos_diff > 0 ? Direction::kOpen : Direction::kClose);
}

void WindowCovering::Move(Direction dir) {
  const char *ss = StateStr(state_);
  bool want_open = false, want_close = false;
  if (cfg_->calibrated || cfg_->man_cal) {
    switch (dir) {
      case Direction::kNone:
        break;
      case Direction::kOpen:
        want_open = true;
        break;
      case Direction::kClose:
        want_close = true;
        break;
    }
  }
  out_open_->SetState(want_open, ss);
  out_close_->SetState(want_close, ss);
  if (moving_dir_ != dir) pos_state_char_->RaiseEvent();
  moving_dir_ = dir;
}

void WindowCovering::RunOnce() {
  const char *ss = StateStr(state_);
  if (state_ != State::kIdle) {
    LOG(LL_DEBUG, ("WC %d: %s md %d pos %.2f -> %.2f", id(), ss,
                   (int) moving_dir_, cur_pos_, tgt_pos_));
  }
  switch (state_) {
    case State::kNone:
    case State::kIdle: {
      if (tgt_state_ != State::kNone && tgt_state_ != state_) {
        SetInternalState(tgt_state_);
        tgt_state_ = State::kNone;
        break;
      }
      if (GetDesiredMoveDirection() != Direction::kNone) {
        SetInternalState(State::kMove);
      }
      break;
    }
    case State::kPreCal0: {
      out_open_->SetState(false, ss);
      out_close_->SetState(false, ss);
      LOG(LL_INFO, ("Begin calibration"));
      cfg_->calibrated = false;
      SaveState();
      out_open_->SetState(true, ss);
      out_close_->SetState(false, ss);
      SetInternalState(State::kCal0);
      break;
    }
    case State::kCal0: {
      auto p0v = pm_open_->GetPowerW();
      if (!p0v.ok()) {
        LOG(LL_ERROR, ("PM error"));
        SetInternalState(State::kError);
        break;
      }
      const float p0 = p0v.ValueOrDie();
      LOG_EVERY_N(LL_INFO, 8, ("WC %d: P0 = %.3f", id(), p0));
      if (p0 < cfg_->idle_power_thr &&
          (mgos_uptime_micros() - begin_ > cfg_->max_ramp_up_time_ms * 1000)) {
        out_open_->SetState(false, ss);
        SetInternalState(State::kPostCal0);
      }
      break;
    }
    case State::kPostCal0: {
      out_open_->SetState(false, ss);
      out_close_->SetState(false, ss);
      SetInternalState(State::kPreCal1);
      break;
    }
    case State::kPreCal1: {
      out_open_->SetState(false, ss);
      out_close_->SetState(true, ss);
      p_sum_ = 0;
      p_num_ = 0;
      SetInternalState(State::kCal1);
      break;
    }
    case State::kCal1: {
      auto p1v = pm_close_->GetPowerW();
      if (!p1v.ok()) {
        LOG(LL_ERROR, ("PM error"));
        SetInternalState(State::kError);
        break;
      }
      const float p1 = p1v.ValueOrDie();
      int move_time_ms = (mgos_uptime_micros() - begin_) / 1000;
      LOG_EVERY_N(LL_INFO, 8, ("WC %d: P1 = %.3f", id(), p1));
      if (p1 < cfg_->idle_power_thr && move_time_ms > 1000) {
        out_close_->SetState(false, StateStr(state_));
        float move_power = p_sum_ / p_num_;
        LOG(LL_INFO, ("WC %d: calibration done, move_time %d, move_power %.3f",
                      id(), move_time_ms, move_power));
        cfg_->move_time_ms = move_time_ms;
        cfg_->move_power = move_power;
        move_ms_per_pct_ = cfg_->move_time_ms / 100.0;
        move_limit_ms_per_pct_ = cfg_->move_time_limit_pos_ms / 100.0;
        SetInternalState(State::kPostCal1);
      } else {
        p_sum_ += p1;
        p_num_++;
      }
      break;
    }
    case State::kPostCal1: {
      cfg_->calibrated = true;
      SetCurPos(kFullyClosed, -1);
      SaveState();
      SetTgtPos((kFullyOpen - kFullyClosed) / 2, "postcal1");
      SetInternalState(State::kIdle);
      break;
    }
    case State::kMove: {
      Direction dir = GetDesiredMoveDirection();
      if (dir == Direction::kNone ||
          (dir == Direction::kClose && cur_pos_ == kFullyClosed) ||
          (dir == Direction::kOpen && cur_pos_ == kFullyOpen)) {
        SetInternalState(State::kStop);
        break;
      }
      if (obstruction_detected_) {
        obstruction_detected_ = false;
        obst_char_->RaiseEvent();
      }
      move_start_pos_ = cur_pos_;
      Move(dir);
      SetInternalState(State::kRampUp);
      break;
    }
    case State::kRampUp: {
      auto *pm = (moving_dir_ == Direction::kOpen ? pm_open_ : pm_close_);
      auto pmv = pm->GetPowerW();
      float p = -1;
      if (pmv.ok()) {
        p = pmv.ValueOrDie();
      } else {
        LOG(LL_ERROR, ("PM error"));
        tgt_state_ = State::kError;
        SetInternalState(State::kStop);
        break;
      }
      LOG(LL_INFO, ("P = %.2f -> %.2f", p, cfg_->move_power));
      if (cfg_->man_cal || p >= cfg_->move_power * 0.75) {
        SetInternalState(State::kMoving);
        break;
      }
      int elapsed_us = (mgos_uptime_micros() - begin_);
      if (elapsed_us > cfg_->max_ramp_up_time_ms * 1000) {
        LOG(LL_ERROR, ("Failed to start moving"));
        tgt_state_ = State::kError;
        SetInternalState(State::kStop);
      }
      break;
    }
    case State::kMoving: {
      bool moving_to_limit_pos =
          ((tgt_pos_ == kFullyOpen && moving_dir_ == Direction::kOpen) ||
           (tgt_pos_ == kFullyClosed && moving_dir_ == Direction::kClose));
      int moving_time_ms = (mgos_uptime_micros() - begin_) / 1000;
      // if manually calibrated move `move_to_end_time_ms` instead of
      // `move_time_ms`, in order to really reach the limit
      // (Caution: this works for motors, that stop at their limit
      // positions by themselves, only).
      float pos_diff = moving_time_ms / (moving_to_limit_pos && cfg_->man_cal
                                             ? move_limit_ms_per_pct_
                                             : move_ms_per_pct_);
      float new_cur_pos =
          (moving_dir_ == Direction::kOpen ? move_start_pos_ + pos_diff
                                           : move_start_pos_ - pos_diff);
      auto *pm = (moving_dir_ == Direction::kOpen ? pm_open_ : pm_close_);
      auto pmv = pm->GetPowerW();
      float p = -1;
      if (pmv.ok()) {
        p = pmv.ValueOrDie();
      } else {
        LOG(LL_ERROR, ("PM error"));
        tgt_state_ = State::kError;
        SetInternalState(State::kStop);
        break;
      }
      SetCurPos(new_cur_pos, p);
      float too_much_power = cfg_->move_power * 2.5;
      int too_long_time = cfg_->move_time_ms * 1.5;
      if (p > cfg_->idle_power_thr &&
          (p > too_much_power || moving_time_ms > too_long_time)) {
        obstruction_detected_ = true;
        obst_char_->RaiseEvent();
        LOG(LL_ERROR, ("Obstruction: p = %.2f t = %d", p, moving_time_ms));
        tgt_state_ = State::kError;
        SetInternalState(State::kStop);
        break;
      }
      Direction want_move_dir = GetDesiredMoveDirection();
      bool reverse =
          (want_move_dir != moving_dir_ && want_move_dir != Direction::kNone);
      // If moving to one of the limit positions, keep moving
      // until no current is flowing. But, if manually calibrated
      // move `move_to_end_time_ms` instead of `move_time_ms`, in order
      // to really reach the limit (Caution: this works for motors, that
      // stop at their limit positions by themselves, only).
      if (moving_to_limit_pos && !reverse && !cfg_->man_cal) {
        LOG_EVERY_N(LL_INFO, 8, ("Moving to %d, p %.2f", (int) tgt_pos_, p));
        if (p > cfg_->idle_power_thr || (mgos_uptime_micros() - begin_ <
                                         cfg_->max_ramp_up_time_ms * 1000)) {
          // Still moving or ramping up.
          break;
        } else {
          float pos =
              (moving_dir_ == Direction::kOpen ? kFullyOpen : kFullyClosed);
          SetCurPos(pos, p);
        }
      } else if (want_move_dir == moving_dir_) {
        // Still moving.
        break;
      } else {
        // We stopped moving. Reconcile target position with current,
        // pretend we wanted to be exactly where we ended up.
        if (std::abs(tgt_pos_ - cur_pos_) < 1) {
          SetTgtPos(cur_pos_, "fixup");
        }
      }
      Move(Direction::kNone);  // Stop moving immediately to minimize error.
      SetInternalState(State::kStop);
      break;
    }
    case State::kStop: {
      Move(Direction::kNone);
      SaveState();
      SetInternalState(State::kStopping);
      break;
    }
    case State::kStopping: {
      float p0 = 0, p1 = 0;
      auto p0v = pm_open_->GetPowerW();
      if (p0v.ok()) p0 = p0v.ValueOrDie();
      auto p1v = pm_close_->GetPowerW();
      if (p1v.ok()) p1 = p1v.ValueOrDie();
      if (p0 < cfg_->idle_power_thr && p1 < cfg_->idle_power_thr) {
        SetInternalState(State::kIdle);
      }
      break;
    }
    case State::kError: {
      Move(Direction::kNone);
      SetTgtPos(cur_pos_, "error");
      SetInternalState(State::kIdle);
      break;
    }
  }
}

void WindowCovering::HandleInputEvent01(Direction dir, Input::Event ev,
                                        bool state) {
  if (!(cfg_->calibrated || cfg_->man_cal)) {
    HandleInputEventNotCalibrated();
    return;
  }
  if (ev != Input::Event::kChange) return;
  bool stop = false;
  bool is_toggle = (cfg_->in_mode == (int) InMode::kSeparateToggle);
  if (state) {
    if (moving_dir_ == Direction::kNone) {
      float pos = (dir == Direction::kOpen ? kFullyOpen : kFullyClosed);
      last_ext_move_dir_ = dir;
      SetTgtPos(pos, "ext");
    } else {
      stop = true;
    }
  } else if (is_toggle && moving_dir_ == dir) {
    stop = true;
  }
  if (stop) {
    // Run state machine before to update cur_pos_.
    RunOnce();
    SetTgtPos(cur_pos_, "ext");
  }
  last_hap_move_dir_ = Direction::kNone;
  // Run the state machine immediately for quicker response.
  RunOnce();
}

void WindowCovering::HandleInputEvent2(Input::Event ev, bool state) {
  if (!(cfg_->calibrated || cfg_->man_cal)) {
    HandleInputEventNotCalibrated();
    return;
  }
  if (ev != Input::Event::kChange) return;
  if (!state) return;
  HandleInputSingle("ext", &last_ext_move_dir_);
  last_hap_move_dir_ = Direction::kNone;
  // Run the state machine immediately for quicker response.
  RunOnce();
}

void WindowCovering::HandleInputEventNotCalibrated() {
  if (state_ != State::kIdle) return;
  bool want_open = (in_open_ != nullptr && in_open_->GetState());
  bool is_open = out_open_->GetState();
  bool want_close = (in_close_ != nullptr && in_close_->GetState());
  bool is_close = out_close_->GetState();
  // Don't allow both at the same time and sudden transitions.
  if ((want_open && want_close) || (want_open && is_close) ||
      (want_close && is_open)) {
    want_open = want_close = false;
  }
  out_open_->SetState(want_open, "ext");
  out_close_->SetState(want_close, "ext");
}

void WindowCovering::HandleInputSingle(const char *src,
                                       Direction *last_move_dir) {
  switch (moving_dir_) {
    case Direction::kNone: {
      if (cur_pos_ == kFullyClosed || *last_move_dir != Direction::kOpen) {
        SetTgtPos(kFullyOpen, src);
        *last_move_dir = Direction::kOpen;
      } else {
        SetTgtPos(kFullyClosed, src);
        *last_move_dir = Direction::kClose;
      }
      break;
    }
    // Stop.
    case Direction::kOpen:
      SetTgtPos(cur_pos_ + 1, src);
      break;
    case Direction::kClose:
      SetTgtPos(cur_pos_ - 1, src);
      break;
  }
}

}  // namespace hap
}  // namespace shelly
