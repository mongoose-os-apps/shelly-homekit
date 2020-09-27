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
      move_ms_per_pct_(cfg_->move_time_ms / 100.0) {
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
  tgt_pos_char_ = new UInt8Characteristic(
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
  cur_pos_char_ = new UInt8Characteristic(
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
  pos_state_char_ = new UInt8Characteristic(
      iid++, &kHAPCharacteristicType_PositionState, 0, 2, 1,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        if (moving_dir_ != Direction::kNone && tgt_pos_ == kFullyClosed) {
          *value = kHAPCharacteristicValue_PositionState_GoingToMinimum;
        } else if (moving_dir_ != Direction::kNone && tgt_pos_ == kFullyOpen) {
          *value = kHAPCharacteristicValue_PositionState_GoingToMaximum;
        } else {
          // TODO: Figure out what to do when moving to an intermediate position
          *value = kHAPCharacteristicValue_PositionState_Stopped;
        }
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_PositionState);
  AddChar(pos_state_char_);
  // Hold Position
  AddChar(new BoolCharacteristic(
      iid++, &kHAPCharacteristicType_HoldPosition, nullptr /* read_handler */,
      false /* supports_notification */,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicWriteRequest *,
             bool value) {
        if (value) {
          LOG(LL_INFO, ("WC %d: Hold position", id()));
          SetState(State::kStop);
        }
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_HoldPosition));
  // Obstruction Detected
  AddChar(new BoolCharacteristic(
      iid++, &kHAPCharacteristicType_ObstructionDetected,
      [](HAPAccessoryServerRef *, const HAPBoolCharacteristicReadRequest *,
         bool *value) {
        *value = false; /* TODO */
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_ObstructionDetected));
  switch (static_cast<InMode>(cfg_->in_mode)) {
    case InMode::kSeparate:
      in_open_handler_ = in_open_->AddHandler(std::bind(
          &WindowCovering::HandleInputEvent0, this, Direction::kOpen, _1, _2));
      in_close_handler_ = in_close_->AddHandler(std::bind(
          &WindowCovering::HandleInputEvent0, this, Direction::kClose, _1, _2));
      break;
    case InMode::kSingle:
      in_open_handler_ = in_open_->AddHandler(
          std::bind(&WindowCovering::HandleInputEvent1, this, _1, _2));
      break;
    case InMode::kDetached:
      break;
  }
  if (cfg_->calibrated) {
    LOG(LL_INFO, ("WC %d: mp %.2f, mt_ms %d, cur_pos %.2f", id(),
                  cfg_->move_power, cfg_->move_time_ms, cur_pos_));
  } else {
    LOG(LL_INFO, ("WC %d: not calibrated", id()));
  }
  state_timer_.Reset(100, MGOS_TIMER_REPEAT);
  return Status::OK();
}

StatusOr<std::string> WindowCovering::GetInfo() const {
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, "
      "open_output_id: %d, close_output_id: %d, "
      "in_mode: %d, swap_inputs: %B, swap_outputs: %B, "
      "cal_done: %B, move_time_ms: %d, move_power: %d, "
      "state: %d, state_str: %Q, cur_pos: %d, tgt_pos: %d}",
      id(), type(), cfg_->name, out_open_->id(), out_close_->id(),
      cfg_->in_mode, cfg_->swap_inputs, cfg_->swap_outputs, cfg_->calibrated,
      cfg_->move_time_ms, (int) cfg_->move_power, (int) state_,
      StateStr(state_), (int) cur_pos_, (int) tgt_pos_);
}

Status WindowCovering::SetConfig(const std::string &config_json,
                                 bool *restart_required) {
  struct mgos_config_wc cfg = *cfg_;
  cfg.name = nullptr;
  int state = -2, tgt_pos = -1, in_mode = -1;
  int8_t swap_inputs = -1, swap_outputs = -1;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, state: %d, tgt_pos: %d, "
             "in_mode: %d, swap_inputs: %B, swap_outputs: %B}",
             &cfg.name, &state, &tgt_pos, &in_mode, &swap_inputs,
             &swap_outputs);
  mgos::ScopedCPtr name_owner((void *) cfg.name);
  // Validate.
  if (cfg.name != nullptr && strlen(cfg.name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (in_mode > 2) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "in_mode");
  }
  if (state != -2 && strcmp(StateStr(static_cast<State>(state)), "???") == 0) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "state");
  }
  // Apply.
  if (state >= 0) {
    tgt_state_ = static_cast<State>(state);
    if (state_ != State::kIdle) {
      SetState(State::kStop);
    }
    return Status::OK();
  }
  if (tgt_pos >= 0) {
    SetTgtPos(tgt_pos, "rpc");
  }
  if (cfg.name != nullptr && strcmp(cfg_->name, cfg.name) != 0) {
    mgos_conf_set_str(&cfg_->name, cfg.name);
    *restart_required = true;
  }
  if (in_mode != -1 && in_mode != cfg_->in_mode) {
    cfg_->in_mode = in_mode;
    *restart_required = true;
  }
  if (swap_inputs != -1 && swap_inputs != cfg_->swap_inputs) {
    cfg_->swap_inputs = swap_inputs;
    *restart_required = true;
  }
  if (swap_outputs != -1 && swap_outputs != cfg_->swap_outputs) {
    cfg_->swap_outputs = swap_outputs;
    *restart_required = true;
  }
  return Status::OK();
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

void WindowCovering::SetState(State new_state) {
  if (state_ == new_state) return;
  LOG(LL_INFO,
      ("WC %d: State transition: %s -> %s (%d -> %d)", id(), StateStr(state_),
       StateStr(new_state), (int) state_, (int) new_state));
  state_ = new_state;
  begin_ = mgos_uptime_micros();
}

void WindowCovering::SetCurPos(float new_cur_pos) {
  new_cur_pos = TrimPos(new_cur_pos);
  if (new_cur_pos == cur_pos_) return;
  LOG(LL_INFO,
      ("WC %d: Current pos %.2f -> %.2f", id(), cur_pos_, new_cur_pos));
  cur_pos_ = new_cur_pos;
  cfg_->current_pos = cur_pos_;
  cur_pos_char_->RaiseEvent();
}

void WindowCovering::SetTgtPos(float new_tgt_pos, const char *src) {
  new_tgt_pos = TrimPos(new_tgt_pos);
  if (new_tgt_pos == tgt_pos_) return;
  LOG(LL_INFO, ("WC %d: Target pos %.2f -> %.2f (%s)", id(), tgt_pos_,
                new_tgt_pos, src));
  tgt_pos_ = new_tgt_pos;
  tgt_pos_char_->RaiseEvent();
}

void WindowCovering::HAPSetTgtPos(float value) {
  // If already moving and the command has come to do a 180,
  // this represents a tap on the tile by the user.
  // In this case we just stop the movement.
  if (moving_dir_ == Direction::kOpen && value == kFullyClosed) {
    SetTgtPos(cur_pos_ + 1, "HAP");
  } else if (moving_dir_ == Direction::kClose && value == kFullyOpen) {
    SetTgtPos(cur_pos_ - 1, "HAP");
  } else {
    SetTgtPos(value, "HAP");
  }
  // Run state machine immediately to improve reaction time,
  RunOnce();
}

WindowCovering::Direction WindowCovering::GetDesiredMoveDirection() {
  if (tgt_pos_ == kNotSet) return Direction::kNone;
  float pos_diff = tgt_pos_ - cur_pos_;
  if (!cfg_->calibrated || std::abs(pos_diff) < 1) {
    return Direction::kNone;
  }
  return (pos_diff > 0 ? Direction::kOpen : Direction::kClose);
}

void WindowCovering::Move(Direction dir) {
  const char *ss = StateStr(state_);
  bool want_open = false, want_close = false;
  if (cfg_->calibrated) {
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
        SetState(tgt_state_);
        tgt_state_ = State::kNone;
        break;
      }
      if (GetDesiredMoveDirection() != Direction::kNone) {
        SetState(State::kMove);
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
      SetState(State::kCal0);
      break;
    }
    case State::kCal0: {
      auto p0v = pm_open_->GetPowerW();
      if (!p0v.ok()) {
        LOG(LL_ERROR, ("PM error"));
        SetState(State::kError);
        break;
      }
      const float p0 = p0v.ValueOrDie();
      LOG(LL_DEBUG, ("WC %d: P0 = %.3f", id(), p0));
      if (p0 < 5 && (mgos_uptime_micros() - begin_ > 300000)) {
        out_open_->SetState(false, ss);
        SetState(State::kPostCal0);
      }
      break;
    }
    case State::kPostCal0: {
      out_open_->SetState(false, ss);
      out_close_->SetState(false, ss);
      SetState(State::kPreCal1);
      break;
    }
    case State::kPreCal1: {
      out_open_->SetState(false, ss);
      out_close_->SetState(true, ss);
      p_sum_ = 0;
      p_num_ = 0;
      SetState(State::kCal1);
      break;
    }
    case State::kCal1: {
      auto p1v = pm_close_->GetPowerW();
      if (!p1v.ok()) {
        LOG(LL_ERROR, ("PM error"));
        SetState(State::kError);
        break;
      }
      const float p1 = p1v.ValueOrDie();
      LOG(LL_DEBUG, ("WC %d: P1 = %.3f", id(), p1));
      if (p_num_ > 1 && p1 < 5) {
        int64_t end = mgos_uptime_micros();
        out_close_->SetState(false, StateStr(state_));
        int move_time_ms = (end - begin_) / 1000;
        float move_power = p_sum_ / p_num_;
        LOG(LL_INFO, ("WC %d: calibration done, move_time %d, move_power %.3f",
                      id(), move_time_ms, move_power));
        cfg_->move_time_ms = move_time_ms;
        cfg_->move_power = move_power;
        move_ms_per_pct_ = cfg_->move_time_ms / 100.0;
        SetState(State::kPostCal1);
      } else {
        p_sum_ += p1;
        p_num_++;
      }
      break;
    }
    case State::kPostCal1: {
      cfg_->calibrated = true;
      SetCurPos(kFullyClosed);
      SaveState();
      SetTgtPos((kFullyOpen - kFullyClosed) / 2, "postcal1");
      SetState(State::kIdle);
      break;
    }
    case State::kMove: {
      Direction dir = GetDesiredMoveDirection();
      if (dir == Direction::kNone ||
          (dir == Direction::kClose && cur_pos_ == kFullyClosed) ||
          (dir == Direction::kOpen && cur_pos_ == kFullyOpen)) {
        SetState(State::kStop);
        break;
      }
      move_start_pos_ = cur_pos_;
      Move(dir);
      SetState(State::kMoving);
      break;
    }
    case State::kMoving: {
      int moving_time_ms = (mgos_uptime_micros() - begin_) / 1000;
      float pos_diff = moving_time_ms / move_ms_per_pct_;
      float new_cur_pos =
          (moving_dir_ == Direction::kOpen ? move_start_pos_ + pos_diff
                                           : move_start_pos_ - pos_diff);
      SetCurPos(new_cur_pos);
      auto *pm = (moving_dir_ == Direction::kOpen ? pm_open_ : pm_close_);
      auto pmv = pm->GetPowerW();
      float p = 0;
      if (pmv.ok()) {
        p = pmv.ValueOrDie();
      } else {
        LOG(LL_ERROR, ("PM error"));
        SetState(State::kError);
        break;
      }
      Direction want_move_dir = GetDesiredMoveDirection();
      bool reverse =
          (want_move_dir != moving_dir_ && want_move_dir != Direction::kNone);
      // If moving to one of the limit positions, keep moving
      // until no current is flowing.
      if (((tgt_pos_ == kFullyOpen && moving_dir_ == Direction::kOpen) ||
           (tgt_pos_ == kFullyClosed && moving_dir_ == Direction::kClose)) &&
          !reverse) {
        LOG(LL_DEBUG, ("Moving to %d, p %.2f", (int) tgt_pos_, p));
        if (p > 5 || (mgos_uptime_micros() - begin_ < 300000)) {
          // Still moving or ramping up.
          break;
        } else {
          SetCurPos(moving_dir_ == Direction::kOpen ? kFullyOpen
                                                    : kFullyClosed);
        }
      } else if (want_move_dir == moving_dir_) {
        // Still moving.
        break;
      } else {
        // We stoped moving. Reconcile target position with current,
        // pretend we wanted to be exactly where we ended up.
        if (std::abs(tgt_pos_ - cur_pos_) < 1) {
          SetTgtPos(cur_pos_, "fixup");
        }
      }
      Move(Direction::kNone);  // Stop moving immediately to minimize error.
      SetState(State::kStop);
      break;
    }
    case State::kStop: {
      Move(Direction::kNone);
      SaveState();
      SetState(State::kStopping);
      break;
    }
    case State::kStopping: {
      float p0 = 0, p1 = 0;
      auto p0v = pm_open_->GetPowerW();
      if (p0v.ok()) p0 = p0v.ValueOrDie();
      auto p1v = pm_close_->GetPowerW();
      if (p1v.ok()) p1 = p1v.ValueOrDie();
      if (p0 < 5 && p1 < 5) {
        SetState(State::kIdle);
      }
      break;
    }
    case State::kError: {
      Move(Direction::kNone);
      break;
    }
  }
}

void WindowCovering::HandleInputEvent0(Direction dir, Input::Event ev,
                                       bool state) {
  if (ev != Input::Event::kChange) return;
  if (!cfg_->calibrated) {
    return;
  }
  if (!state) return;
  if (moving_dir_ == Direction::kNone || moving_dir_ != dir) {
    if (dir == Direction::kOpen) {
      SetTgtPos(kFullyOpen, "ext");
    } else {
      SetTgtPos(kFullyClosed, "ext");
    }
  } else {
    // Stop.
    SetTgtPos(cur_pos_, "ext");
  }
  // Run the state machine immediately for quicker response.
  RunOnce();
}

void WindowCovering::HandleInputEvent1(Input::Event ev, bool state) {
  if (ev != Input::Event::kChange) return;
  if (!cfg_->calibrated) {
    return;
  }
  if (!state) return;
  switch (moving_dir_) {
    case Direction::kNone: {
      if (last_ext_move_dir_ != Direction::kOpen) {
        SetTgtPos(kFullyOpen, "ext");
        last_ext_move_dir_ = Direction::kOpen;
      } else {
        SetTgtPos(kFullyClosed, "ext");
        last_ext_move_dir_ = Direction::kClose;
      }
      break;
    }
    // Stop.
    case Direction::kOpen:
      SetTgtPos(cur_pos_ + 1, "ext");
      break;
    case Direction::kClose:
      SetTgtPos(cur_pos_ - 1, "ext");
      break;
  }
  // Run the state machine immediately for quicker response.
  RunOnce();
}

}  // namespace hap
}  // namespace shelly
