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

GarageDoorOpener::GarageDoorOpener(int id, Input *in, Output *out,
                                   struct mgos_config_gdo *cfg)
    : Component(id),
      Service((SHELLY_HAP_IID_BASE_GARAGE_DOOR_OPENER +
               (SHELLY_HAP_IID_STEP_GARAGE_DOOR_OPENER * (id - 1))),
              &kHAPServiceType_GarageDoorOpener,
              kHAPServiceDebugDescription_GarageDoorOpener),
      in_close_(in),
      out_(out),
      cfg_(cfg),
      state_timer_(std::bind(&GarageDoorOpener::RunOnce, this)) {
}

GarageDoorOpener::~GarageDoorOpener() {
  out_->SetState(false, "dtor");
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
        return kHAPError_None;
      },
      true /* supports_notification */,
      [this](HAPAccessoryServerRef *,
             const HAPUInt8CharacteristicWriteRequest *, uint8_t value) {
        // We need to decouple from the current invocation
        // because we may want to raise a notification on the target position
        // and we can't do that within the write callback.
        mgos::InvokeCB([this, value] {
          UserSetTgtState(static_cast<State>(value), "HAP");
        });
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_CurrentPosition);
  AddChar(tgt_state_char_);
  // Obstruction Detected
  obst_char_ = new BoolCharacteristic(
      iid++, &kHAPCharacteristicType_ObstructionDetected,
      [this](HAPAccessoryServerRef *, const HAPBoolCharacteristicReadRequest *,
             bool *value) {
        *value = (cur_state_ == State::kStopped);
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

Component::Type GarageDoorOpener::type() const {
  return Type::kGarageDoorOpener;
}

StatusOr<std::string> GarageDoorOpener::GetInfo() const {
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, "
      "cur_state: %d, cur_state_str: %Q, "
      "move_time: %d, pulse_time_ms: %d, close_sensor_mode: %d}",
      id(), type(), cfg_->name, (int) cur_state_, StateStr(cur_state_),
      cfg_->move_time_ms / 1000, cfg_->pulse_time_ms, cfg_->close_sensor_mode);
}

Status GarageDoorOpener::SetConfig(const std::string &config_json,
                                   bool *restart_required) {
  struct mgos_config_gdo cfg = *cfg_;
  cfg.name = nullptr;
  int move_time = -1, pulse_time_ms = -1, close_sensor_mode = -1;
  int8_t toggle = -1;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, toggle: %B, move_time: %d, pulse_time_ms: %d, "
             "close_sensor_mode: %d}",
             &cfg.name, &toggle, &move_time, &pulse_time_ms,
             &close_sensor_mode);
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
  // We don't impose a limit on pulse time.
  // Apply.
  if (toggle != -1 && toggle) {
    State new_state =
        (tgt_state_ == State::kOpen ? State::kClosed : State::kOpen);
    UserSetTgtState(new_state, "RPC");
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

void GarageDoorOpener::SetCurState(State new_state) {
  if (cur_state_ == new_state) return;
  LOG(LL_INFO,
      ("GDO %d: Cur State: %s -> %s (%d -> %d)", id(), StateStr(cur_state_),
       StateStr(new_state), (int) cur_state_, (int) new_state));
  cur_state_ = new_state;
  begin_ = mgos_uptime_micros();
  cur_state_char_->RaiseEvent();
}

void GarageDoorOpener::UserSetTgtState(State new_state, const char *source) {
  // Every target state change generates a pulse.
  if (new_state != tgt_state_) {
    out_->Pulse(true, cfg_->pulse_time_ms,
                (new_state == State::kOpen ? "GDO:open" : "GDO:close"));
  }
  SetTgtState(new_state, source);
  if (cur_state_ == State::kClosed) {
    SetCurState(State::kOpening);
  } else {
    // This really depends on the controller behavior, we just guess...
    SetCurState(State::kClosing);
  }
}

void GarageDoorOpener::SetTgtState(State new_state, const char *src) {
  if (tgt_state_ == new_state) return;
  LOG(LL_INFO, ("GDO %d: Tgt State: %s -> %s (%d -> %d) (%s)", id(),
                StateStr(tgt_state_), StateStr(new_state), (int) tgt_state_,
                (int) new_state, src));
  tgt_state_ = new_state;
  tgt_state_char_->RaiseEvent();
}

void GarageDoorOpener::RunOnce() {
  bool in_close_act_state = (cfg_->close_sensor_mode == 0);
  bool closed = (in_close_->GetState() == in_close_act_state);
  LOG(LL_DEBUG, ("GDO %d: cur %s tgt %s closed %d", id(), StateStr(cur_state_),
                 StateStr(tgt_state_), closed));
  switch (cur_state_) {
    case State::kOpen: {
      if (closed) {  // Closed externally.
        SetTgtState(State::kClosed, "ext");
        SetCurState(State::kClosed);
        break;
      }
      break;
    }
    case State::kClosed: {
      if (!closed) {
        SetTgtState(State::kOpen, "ext");
        SetCurState(State::kOpening);
      }
      break;
    }
    case State::kOpening: {
      int64_t elapsed_ms = (mgos_uptime_micros() - begin_) / 1000;
      if (elapsed_ms > cfg_->move_time_ms) {
        SetCurState(State::kOpen);
      } else if (elapsed_ms > 5000 && closed) {
        SetTgtState(State::kClosed, "ext");
        SetCurState(State::kClosed);
      }
      break;
    }
    case State::kClosing: {
      if (closed) {
        SetCurState(State::kClosed);
        break;
      }
      int64_t elapsed_ms = (mgos_uptime_micros() - begin_) / 1000;
      if (elapsed_ms > cfg_->move_time_ms) {
        SetCurState(State::kStopped);
        obst_char_->RaiseEvent();
        break;
      }
      break;
    }
    case State::kStopped: {
      if (closed) {
        SetTgtState(State::kClosed, "ext");
        SetCurState(State::kClosed);
        obst_char_->RaiseEvent();
        break;
      }
      break;
    }
  }
}

}  // namespace hap
}  // namespace shelly
