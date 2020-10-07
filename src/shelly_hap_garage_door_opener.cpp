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
      cfg_(cfg),
      state_timer_(std::bind(&GarageDoorOpener::RunOnce, this)) {
}

GarageDoorOpener::~GarageDoorOpener() {
  if (in_close_handler_ != Input::kInvalidHandlerID) {
    in_close_->RemoveHandler(in_close_handler_);
  }
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
        mgos::InvokeCB(
            [this, value] { SetTgtState(static_cast<State>(value), "HAP"); });
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_CurrentPosition);
  AddChar(tgt_state_char_);
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
      "cur_state: %d, move_time_ms: %d",
      id(), type(), cfg_->name, (int) cur_state_, cfg_->move_time_ms);
}

Status GarageDoorOpener::SetConfig(const std::string &config_json,
                                   bool *restart_required) {
  struct mgos_config_gdo cfg = *cfg_;
  cfg.name = nullptr;
  int tgt_state = -1, move_time = -1;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, tgt_state: %d, move_time: %d", &cfg.name, &tgt_state,
             &move_time);
  mgos::ScopedCPtr name_owner((void *) cfg.name);
  // Validate.
  if (cfg.name != nullptr && strlen(cfg.name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (tgt_state > 1) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "tgt_state");
  }
  // Apply.
  if (tgt_state >= 0) {
    SetTgtState(static_cast<State>(tgt_state), "rpc");
  }
  if (cfg.name != nullptr && strcmp(cfg_->name, cfg.name) != 0) {
    mgos_conf_set_str(&cfg_->name, cfg.name);
    *restart_required = true;
  }
  if (move_time > 0) {
    cfg_->move_time_ms = move_time * 1000;
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

void GarageDoorOpener::SetTgtState(State new_state, const char *src) {
  if (tgt_state_ == new_state) return;
  LOG(LL_INFO, ("GDO %d: Tgt State: %s -> %s (%d -> %d) (%s)", id(),
                StateStr(tgt_state_), StateStr(new_state), (int) tgt_state_,
                (int) new_state, src));
  tgt_state_ = new_state;
  tgt_state_char_->RaiseEvent();
}

void GarageDoorOpener::RunOnce() {
  LOG(LL_DEBUG, ("GDO %d: cur %s tgt %s", id(), StateStr(cur_state_),
                 StateStr(tgt_state_)));
  switch (cur_state_) {
    case State::kOpen: {
      break;
    }
    case State::kClosed: {
      break;
    }
    case State::kOpening: {
      break;
    }
    case State::kClosing: {
      break;
    }
  }
}

}  // namespace hap
}  // namespace shelly
