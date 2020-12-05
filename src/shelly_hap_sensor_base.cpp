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

#include "shelly_hap_sensor_base.hpp"

#include "mgos.hpp"
#include "mgos_hap.hpp"

namespace shelly {
namespace hap {

SensorBase::SensorBase(int id, Input *in, struct mgos_config_in_sensor *cfg,
                       uint16_t iid_base, const HAPUUID *type,
                       const char *debug_description)
    : Component(id),
      Service(iid_base + SHELLY_HAP_IID_STEP_SENSOR * (id - 1), type,
              debug_description),
      in_(in),
      cfg_(cfg),
      auto_off_timer_(std::bind(&SensorBase::AutoOffTimerCB, this)) {
}

SensorBase::~SensorBase() {
  in_->RemoveHandler(handler_id_);
}

std::string SensorBase::name() const {
  return cfg_->name;
}

Status SensorBase::Init() {
  if (in_ == nullptr) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "input is required");
  }

  AddNameChar(svc_.iid + 1, cfg_->name);

  handler_id_ =
      in_->AddHandler(std::bind(&SensorBase::InputEventHandler, this, _1, _2));

  if (cfg_->in_mode == (int) InMode::kLevel) {
    SetState(in_->GetState());
  }

  return Status::OK();
}

StatusOr<std::string> SensorBase::GetInfo() const {
  double last_ev_age = -1;
  if (last_ev_ts_ > 0) {
    last_ev_age = mgos_uptime() - last_ev_ts_;
  }
  return mgos::SPrintf("st:%d lea:%.3f", state_, last_ev_age);
}

StatusOr<std::string> SensorBase::GetInfoJSON() const {
  double last_ev_age = -1;
  if (last_ev_ts_ > 0) {
    last_ev_age = mgos_uptime() - last_ev_ts_;
  }
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, in_mode: %d, idle_time: %d, "
      "state: %B, last_ev_age: %.3f}",
      id(), type(), (cfg_->name ? cfg_->name : ""), cfg_->in_mode,
      cfg_->idle_time, state_, last_ev_age);
}

Status SensorBase::SetConfig(const std::string &config_json,
                             bool *restart_required) {
  char *name = nullptr;
  int in_mode = -1, idle_time = -1;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, in_mode: %d, idle_time: %d}", &name, &in_mode,
             &idle_time);
  mgos::ScopedCPtr name_owner(name);
  // Validation.
  if (name != nullptr && strlen(name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (in_mode != -1 && (in_mode < 0 || in_mode >= (int) InMode::kMax)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "in_mode");
  }
  if (idle_time != -1 && (idle_time <= 0 || idle_time > 10000)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "idle_time");
  }
  // Now copy over.
  if (name != nullptr && strcmp(name, cfg_->name) != 0) {
    mgos_conf_set_str(&cfg_->name, name);
    *restart_required = true;
  }
  if (in_mode != -1) {
    cfg_->in_mode = in_mode;
    bool md = (in_mode == (int) InMode::kLevel ? in_->GetState() : false);
    SetState(md);
  }
  if (idle_time != -1) {
    cfg_->idle_time = idle_time;
  }
  return Status::OK();
}

HAPError SensorBase::BoolStateCharRead(HAPAccessoryServerRef *,
                                       const HAPBoolCharacteristicReadRequest *,
                                       bool *value) {
  *value = state_;
  return kHAPError_None;
}

void SensorBase::InputEventHandler(Input::Event ev, bool state) {
  if (ev != Input::Event::kChange) return;
  const auto in_mode = static_cast<InMode>(cfg_->in_mode);
  switch (in_mode) {
    case InMode::kLevel:
      SetState(state);
      break;
    case InMode::kPulse:
      if (state) {
        SetState(true);
      }
      break;
    case InMode::kMax:
      break;
  }
}

void SensorBase::SetState(bool state) {
  if (state != state_) {
    LOG(LL_INFO, ("Sensor state: %d -> %d", state_, state));
    if (state) {
      last_ev_ts_ = mgos_uptime();
    }
    state_ = state;
    chars_[1]->RaiseEvent();
  }
  if (state && cfg_->in_mode == (int) InMode::kPulse) {
    auto_off_timer_.Reset(cfg_->idle_time * 1000, 0);
  }
}

void SensorBase::AutoOffTimerCB() {
  if (cfg_->in_mode != (int) InMode::kPulse) return;
  SetState(false);
}

}  // namespace hap
}  // namespace shelly
