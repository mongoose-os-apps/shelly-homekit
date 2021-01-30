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

#include "shelly_hap_stateless_switch.hpp"

#include "mgos.hpp"
#include "mgos_hap.hpp"

namespace shelly {
namespace hap {

StatelessSwitch::StatelessSwitch(int id, Input *in,
                                 struct mgos_config_in_ssw *cfg)
    : Component(id),
      Service(
          // IDs used to start at 0, preserve compat.
          (SHELLY_HAP_IID_BASE_STATELESS_SWITCH +
           (SHELLY_HAP_IID_STEP_STATELESS_SWITCH * (id - 1))),
          &kHAPServiceType_StatelessProgrammableSwitch,
          kHAPServiceDebugDescription_StatelessProgrammableSwitch),
      in_(in),
      cfg_(cfg) {
}

StatelessSwitch::~StatelessSwitch() {
  in_->RemoveHandler(handler_id_);
}

Component::Type StatelessSwitch::type() const {
  return Type::kStatelessSwitch;
}

std::string StatelessSwitch::name() const {
  return cfg_->name;
}

Status StatelessSwitch::Init() {
  if (in_ == nullptr) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "input is required");
  }
  uint16_t iid = svc_.iid + 1;
  // Name
  AddNameChar(iid++, cfg_->name);
  // Programmable Switch Event
  AddChar(new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_ProgrammableSwitchEvent, 0, 2, 1,
      [this](HAPAccessoryServerRef *, const HAPUInt8CharacteristicReadRequest *,
             uint8_t *value) {
        if (last_ev_ts_ == 0) return kHAPError_InvalidState;
        *value = last_ev_;
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_ProgrammableSwitchEvent));

  handler_id_ = in_->AddHandler(
      std::bind(&StatelessSwitch::InputEventHandler, this, _1, _2));

  return Status::OK();
}

StatusOr<std::string> StatelessSwitch::GetInfo() const {
  double last_ev_age = -1;
  if (last_ev_ts_ > 0) {
    last_ev_age = mgos_uptime() - last_ev_ts_;
  }
  return mgos::SPrintf("st:%d m:%d lea: %.3f", in_->GetState(), cfg_->in_mode,
                       last_ev_age);
}

StatusOr<std::string> StatelessSwitch::GetInfoJSON() const {
  double last_ev_age = -1;
  if (last_ev_ts_ > 0) {
    last_ev_age = mgos_uptime() - last_ev_ts_;
  }
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, in_mode: %d, "
      "last_ev: %d, last_ev_age: %.3f}",
      id(), type(), (cfg_->name ? cfg_->name : ""), cfg_->in_mode, last_ev_,
      last_ev_age);
}

Status StatelessSwitch::SetConfig(const std::string &config_json,
                                  bool *restart_required) {
  char *name = nullptr;
  int in_mode = -1;
  json_scanf(config_json.c_str(), config_json.size(), "{name: %Q, in_mode: %d}",
             &name, &in_mode);
  mgos::ScopedCPtr name_owner(name);
  // Validation.
  if (name != nullptr && strlen(name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (in_mode < 0 || in_mode > 2) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "in_mode");
  }
  // Now copy over.
  if (name != nullptr && strcmp(name, cfg_->name) != 0) {
    mgos_conf_set_str(&cfg_->name, name);
    *restart_required = true;
  }
  cfg_->in_mode = in_mode;
  return Status::OK();
}

Status StatelessSwitch::SetState(const std::string &state_json) {
  (void) state_json;
  return Status::UNIMPLEMENTED();
}

void StatelessSwitch::InputEventHandler(Input::Event ev, bool state) {
  const auto in_mode = static_cast<InMode>(cfg_->in_mode);
  switch (in_mode) {
    // In momentary input mode we translate input events to HAP events directly.
    case InMode::kMomentary:
      switch (ev) {
        case Input::Event::kSingle:
          RaiseEvent(
              kHAPCharacteristicValue_ProgrammableSwitchEvent_SinglePress);
          break;
        case Input::Event::kDouble:
          RaiseEvent(
              kHAPCharacteristicValue_ProgrammableSwitchEvent_DoublePress);
          break;
        case Input::Event::kLong:
          RaiseEvent(kHAPCharacteristicValue_ProgrammableSwitchEvent_LongPress);
          break;
        case Input::Event::kChange:
        case Input::Event::kReset:
        case Input::Event::kMax:
          // Ignore.
          break;
      }
      break;
    // In toggle switch input mode we translate changes to HAP events.
    case InMode::kToggleShort:
    case InMode::kToggleShortLong:
      if (ev != Input::Event::kChange) break;
      if (in_mode == InMode::kToggleShortLong && !state) {
        RaiseEvent(kHAPCharacteristicValue_ProgrammableSwitchEvent_DoublePress);
      } else {
        RaiseEvent(kHAPCharacteristicValue_ProgrammableSwitchEvent_SinglePress);
      }
      break;
  }
}

void StatelessSwitch::RaiseEvent(uint8_t ev) {
  last_ev_ = ev;
  last_ev_ts_ = mgos_uptime();
  LOG(LL_INFO, ("Input %d: HAP event (mode %d): %d", id(), cfg_->in_mode, ev));
  // May happen during init, we don't want to raise events until initialized.
  if (handler_id_ != Input::kInvalidHandlerID) {
    chars_[1]->RaiseEvent();
  }
}

}  // namespace hap
}  // namespace shelly
