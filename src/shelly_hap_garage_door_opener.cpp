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

#include "shelly_hap_garage_door_opener.hpp"

#include "mgos.hpp"
#include "mgos_system.hpp"

namespace shelly {
namespace hap {

GarageDoorOpener::GarageDoorOpener(int id, Input *in_close, Input *in_open,
                                   Output *out_close, Output *out_open,
                                   struct mgos_config_gdo *cfg)
    : Component(id),
      Service((SHELLY_HAP_IID_BASE_GARAGE_DOOR_OPENER +
               (SHELLY_HAP_IID_STEP_GARAGE_DOOR_OPENER * (id - 1))),
              &kHAPServiceType_GarageDoorOpener,
              kHAPServiceDebugDescription_GarageDoorOpener),
      in_close_(in_close),
      in_open_(in_open),
      out_close_(out_close),
      out_open_(out_open),
      cfg_(cfg),
      state_timer_(std::bind(&GarageDoorOpener::RunOnce, this)) {
  out_close_->SetState(false, "ctor");
  out_open_->SetState(false, "ctor");
}

GarageDoorOpener::~GarageDoorOpener() {
  out_close_->SetState(false, "dtor");
  out_open_->SetState(false, "dtor");
}

Component::Type GarageDoorOpener::type() const {
  return Type::kGarageDoorOpener;
}

Status GarageDoorOpener::Init() {
  uint16_t iid = svc_.iid + 1;
  // Name
  AddNameChar(iid++, cfg_->name);
  // Current Door State
  cur_state_char_ = new UInt8Characteristic(
      iid++, &kHAPCharacteristicType_CurrentDoorState, 0, 4, 1,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        *value = static_cast<uint8_t>(cur_state_);
        LOG(LL_DEBUG, ("GDO %d: Read cur: %d", id(), *value));
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr, /* write_handler */
      kHAPCharacteristicDebugDescription_CurrentDoorState);
  AddChar(cur_state_char_);
  // Target Door State
  tgt_state_char_ = new UInt8Characteristic(
      iid++, &kHAPCharacteristicType_TargetDoorState, 0, 1, 1,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        *value = static_cast<uint8_t>(tgt_state_);
        LOG(LL_DEBUG, ("GDO %d: Read tgt: %d", id(), *value));
        return kHAPError_None;
      },
      true /* supports_notification */,
      std::bind(&GarageDoorOpener::HAPTgtStateWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_CurrentPosition);
  AddChar(tgt_state_char_);
  // Obstruction Detected
  obst_char_ = new BoolCharacteristic(
      iid++, &kHAPCharacteristicType_ObstructionDetected,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicReadRequest *,
             bool *value) {
        *value = obstruction_detected_;
        LOG(LL_DEBUG, ("GDO %d: Read obst: %d", id(), *value));
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_ObstructionDetected);
  AddChar(obst_char_);
  cur_state_ = (in_close_->GetState() ? State::kClosed : State::kOpen);
  tgt_state_ = cur_state_;
  LOG(LL_INFO, ("GDO %d: cur_state %d", id(), (int) cur_state_));
  state_timer_.Reset(100, MGOS_TIMER_REPEAT);
  return Status::OK();
}

StatusOr<std::string> GarageDoorOpener::GetInfo() const {
  int is_closed, is_open;
  GetInputsState(&is_closed, &is_open);
  return mgos::SPrintf("cur:%s tgt:%s cl:%d op:%d", StateStr(cur_state_),
                       StateStr(tgt_state_), is_closed, is_open);
}

StatusOr<std::string> GarageDoorOpener::GetInfoJSON() const {
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, "
      "cur_state: %d, cur_state_str: %Q, "
      "move_time: %d, pulse_time_ms: %d, "
      "close_sensor_mode: %d, open_sensor_mode: %d, "
      "out_mode: %d}",
      id(), type(), cfg_->name, (int) cur_state_, StateStr(cur_state_),
      cfg_->move_time_ms / 1000, cfg_->pulse_time_ms, cfg_->close_sensor_mode,
      cfg_->open_sensor_mode, (out_open_ != out_close_ ? cfg_->out_mode : -1));
}

Status GarageDoorOpener::SetConfig(const std::string &config_json,
                                   bool *restart_required) {
  struct mgos_config_gdo cfg = *cfg_;
  cfg.name = nullptr;
  int move_time = -1, pulse_time_ms = -1, out_mode = -1;
  int close_sensor_mode = -1, open_sensor_mode = -1;
  int8_t toggle = -1;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, toggle: %B, move_time: %d, pulse_time_ms: %d, "
             "close_sensor_mode: %d, open_sensor_mode: %d, out_mode: %d}",
             &cfg.name, &toggle, &move_time, &pulse_time_ms, &close_sensor_mode,
             &open_sensor_mode, &out_mode);
  mgos::ScopedCPtr name_owner((void *) cfg.name);
  // Validate.
  if (cfg.name != nullptr && strlen(cfg.name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (close_sensor_mode > 1) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "close_sensor_mode");
  }
  if (open_sensor_mode > 2) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "open_sensor_mode");
  }
  if (out_mode > 1) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "out_mode");
  }
  // We don't impose a limit on pulse time.
  // Apply.
  if (toggle != -1 && toggle) {
    ToggleState("RPC");
    RunOnce();
  }
  if (cfg.name != nullptr && strcmp(cfg_->name, cfg.name) != 0) {
    mgos_conf_set_str(&cfg_->name, cfg.name);
    *restart_required = true;
  }
  if (move_time > 0) {
    cfg_->move_time_ms = move_time * 1000;
  }
  if (pulse_time_ms > 0) {
    cfg_->pulse_time_ms = pulse_time_ms;
  }
  if (close_sensor_mode >= 0) {
    cfg_->close_sensor_mode = close_sensor_mode;
  }
  if (open_sensor_mode >= 0) {
    cfg_->open_sensor_mode = open_sensor_mode;
  }
  if (out_mode >= 0) {
    cfg_->out_mode = out_mode;
    *restart_required = true;
  }
  return Status::OK();
}

// static
const char *GarageDoorOpener::StateStr(State state) {
  switch (state) {
    case State::kOpen:
      return "open";
    case State::kClosed:
      return "closed";
    case State::kOpening:
      return "opening";
    case State::kClosing:
      return "closing";
    case State::kStopped:
      return "stopped";
  }
  return "???";
}

void GarageDoorOpener::GetInputsState(int *is_closed, int *is_open) const {
  bool in_close_act_state = (cfg_->close_sensor_mode == 0);
  *is_closed = (in_close_->GetState() == in_close_act_state);
  if (in_open_ != nullptr &&
      (cfg_->open_sensor_mode == 0 || cfg_->open_sensor_mode == 1)) {
    bool in_open_act_state = (cfg_->open_sensor_mode == 0);
    *is_open = (in_open_->GetState() == in_open_act_state);
  } else {
    *is_open = -1;
  }
}

void GarageDoorOpener::SetCurState(State new_state) {
  if (cur_state_ == new_state) return;
  LOG(LL_INFO,
      ("GDO %d: Cur State: %s -> %s (%d -> %d)", id(), StateStr(cur_state_),
       StateStr(new_state), (int) cur_state_, (int) new_state));
  bool obst_notify;
  if (cur_state_ == State::kStopped) {
    // Leaving Stopepd state - reset the "obstruction detected" flag.
    obstruction_detected_ = false;
    obst_notify = true;
  }
  if (new_state == State::kStopped) {
    // Entering Stopped state - remember what was the previous state.
    pre_stopped_state_ = cur_state_;
    obst_notify = true;
  }
  cur_state_ = new_state;
  begin_ = mgos_uptime_micros();
  cur_state_char_->RaiseEvent();
  if (obst_notify) {
    obst_char_->RaiseEvent();
  }
}

void GarageDoorOpener::ToggleState(const char *source) {
  // Every target state change generates a pulse.
  State new_state =
      (tgt_state_ == State::kClosed ? State::kOpen : State::kClosed);
  const char *out_src = (new_state == State::kOpen ? "GDO:open" : "GDO:close");
  if (cfg_->out_mode == 0 || new_state == State::kClosed) {
    out_close_->Pulse(true, cfg_->pulse_time_ms, out_src);
  } else {
    out_open_->Pulse(true, cfg_->pulse_time_ms, out_src);
  }
  switch (cur_state_) {
    case State::kOpen:
      SetTgtState(new_state, source);
      SetCurState(State::kClosing);
      break;
    case State::kClosed:
      SetTgtState(new_state, source);
      SetCurState(State::kOpening);
      break;
    case State::kOpening:
      SetTgtState(new_state, source);
      SetCurState(State::kStopped);
      break;
    case State::kClosing:
      SetTgtState(State::kOpen, source);
      SetCurState(State::kOpening);
      break;
    case State::kStopped:
      if (pre_stopped_state_ == State::kOpening) {
        SetTgtState(State::kClosed, "fixup");
        SetCurState(State::kClosing);
      } else {
        SetTgtState(State::kOpen, "fixup");
        SetCurState(State::kOpening);
      }
      break;
  }
}

HAPError GarageDoorOpener::HAPTgtStateWrite(
    HAPAccessoryServerRef *, const HAPUInt8CharacteristicWriteRequest *,
    uint8_t value) {
  if ((value == kHAPCharacteristicValue_TargetDoorState_Open &&
       (cur_state_ == State::kOpen || cur_state_ == State::kOpening)) ||
      (value == kHAPCharacteristicValue_TargetDoorState_Closed &&
       (cur_state_ == State::kClosed || cur_state_ == State::kClosing))) {
    // Nothing to do.
    return kHAPError_None;
  }
  // We need to decouple from the current invocation
  // because we may want to raise a notification on the target
  // position and we can't do that within the write callback.
  mgos::InvokeCB([this, value] {
    // We want every tap to cause an action, so we basically ignore
    // the actual value.
    ToggleState((value ? "HAPclose" : "HAPopen"));
    RunOnce();
  });
  return kHAPError_None;
}

void GarageDoorOpener::SetTgtState(State new_state, const char *src) {
  if (tgt_state_ != new_state) {
    LOG(LL_INFO, ("GDO %d: Tgt State: %s -> %s (%d -> %d) (%s)", id(),
                  StateStr(tgt_state_), StateStr(new_state), (int) tgt_state_,
                  (int) new_state, src));
  }
  tgt_state_ = new_state;
  // Always notify, even if not changed, to make sure HAP is in sync with
  // reality that may be different from what it thinks it is.
  tgt_state_char_->RaiseEvent();
}

void GarageDoorOpener::RunOnce() {
  int is_closed, is_open;
  GetInputsState(&is_closed, &is_open);
  LOG(LL_DEBUG,
      ("GDO %d: cur %s tgt %s is_closed %d is_open %d", id(),
       StateStr(cur_state_), StateStr(tgt_state_), is_closed, is_open));
  if (cur_state_ != State::kStopped && is_closed && is_open == 1) {
    LOG(LL_ERROR, ("Both sensors active, error"));
    SetCurState(State::kStopped);
  }
  switch (cur_state_) {
    case State::kOpen: {
      if (is_closed) {  // Closed externally.
        SetTgtState(State::kClosed, "ext");
        SetCurState(State::kClosed);
        break;
      }
      if (!is_open) {
        SetTgtState(State::kClosed, "ext");
        SetCurState(State::kClosing);
      }
      break;
    }
    case State::kClosed: {
      if (!is_closed) {
        SetTgtState(State::kOpen, "ext");
        SetCurState(State::kOpening);
      }
      break;
    }
    case State::kOpening: {
      int64_t elapsed_ms = (mgos_uptime_micros() - begin_) / 1000;
      if (is_open != -1) {
        if (is_open) {
          SetCurState(State::kOpen);
          break;
        }
        if (elapsed_ms > cfg_->move_time_ms) {
          obstruction_detected_ = true;
          SetCurState(State::kStopped);
          break;
        }
      } else {
        if (elapsed_ms > cfg_->move_time_ms) {
          SetCurState(State::kOpen);
          break;
        }
      }
      if (is_closed && elapsed_ms > 5000) {
        SetTgtState(State::kClosed, "ext");
        SetCurState(State::kClosed);
      }
      break;
    }
    case State::kClosing: {
      if (is_closed) {
        SetCurState(State::kClosed);
        break;
      }
      int64_t elapsed_ms = (mgos_uptime_micros() - begin_) / 1000;
      if (elapsed_ms > cfg_->move_time_ms) {
        obstruction_detected_ = true;
        SetCurState(State::kStopped);
        break;
      }
      if (is_open == 1 && elapsed_ms > 5000) {
        SetTgtState(State::kOpen, "ext");
        SetCurState(State::kOpen);
      }
      break;
    }
    case State::kStopped: {
      if (is_closed && is_open == 1) {
        break;
      }
      if (is_closed) {
        SetTgtState(State::kClosed, "ext");
        SetCurState(State::kClosed);
        break;
      }
      if (is_open == 1) {
        SetTgtState(State::kOpen, "ext");
        SetCurState(State::kOpen);
        break;
      }
      break;
    }
  }
}

}  // namespace hap
}  // namespace shelly
