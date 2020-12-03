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

#include "shelly_hap_motion_occupancy_sensor.hpp"

#include "mgos.hpp"
#include "mgos_hap.hpp"

namespace shelly {
namespace hap {

MotionOccupancySensor::MotionOccupancySensor(int id, Input *in,
                                             struct mgos_config_in_ms *cfg,
                                             bool occupancy)
    : Component(id),
      Service((occupancy ? (SHELLY_HAP_IID_BASE_OCCUPANCY_SENSOR +
                            (SHELLY_HAP_IID_STEP_OCCUPANCY_SENSOR * (id - 1)))
                         : (SHELLY_HAP_IID_BASE_MOTION_SENSOR +
                            (SHELLY_HAP_IID_STEP_MOTION_SENSOR * (id - 1)))),
              (occupancy ? &kHAPServiceType_OccupancySensor
                         : &kHAPServiceType_MotionSensor),
              (occupancy ? kHAPServiceDebugDescription_OccupancySensor
                         : kHAPServiceDebugDescription_MotionSensor)),
      in_(in),
      cfg_(cfg),
      occupancy_(occupancy),
      auto_off_timer_(std::bind(&MotionOccupancySensor::AutoOffTimerCB, this)) {
}

MotionOccupancySensor::~MotionOccupancySensor() {
  in_->RemoveHandler(handler_id_);
}

Component::Type MotionOccupancySensor::type() const {
  return (occupancy_ ? Type::kOccupancySensor : Type::kMotionSensor);
}

std::string MotionOccupancySensor::name() const {
  return cfg_->name;
}

Status MotionOccupancySensor::Init() {
  if (in_ == nullptr) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "input is required");
  }
  uint16_t iid = svc_.iid + 1;
  // Name
  AddNameChar(iid++, cfg_->name);
  // Motion or Occupancy Detected
  if (occupancy_) {
    AddChar(new mgos::hap::BoolCharacteristic(
        iid++, &kHAPCharacteristicType_OccupancyDetected,
        std::bind(&MotionOccupancySensor::MotionDetectedRead, this, _1, _2, _3),
        true /* supports_notification */, nullptr /* write_handler */,
        kHAPCharacteristicDebugDescription_OccupancyDetected));
  } else {
    AddChar(new mgos::hap::BoolCharacteristic(
        iid++, &kHAPCharacteristicType_MotionDetected,
        std::bind(&MotionOccupancySensor::MotionDetectedRead, this, _1, _2, _3),
        true /* supports_notification */, nullptr /* write_handler */,
        kHAPCharacteristicDebugDescription_MotionDetected));
  }

  handler_id_ = in_->AddHandler(
      std::bind(&MotionOccupancySensor::InputEventHandler, this, _1, _2));

  if (cfg_->in_mode == (int) InMode::kLevel) {
    SetMotionOccupancyDetected(in_->GetState());
  }

  return Status::OK();
}

StatusOr<std::string> MotionOccupancySensor::GetInfo() const {
  double last_ev_age = -1;
  if (last_ev_ts_ > 0) {
    last_ev_age = mgos_uptime() - last_ev_ts_;
  }
  return mgos::SPrintf("st:%d lea:%.3f", motion_detected_, last_ev_age);
}

StatusOr<std::string> MotionOccupancySensor::GetInfoJSON() const {
  double last_ev_age = -1;
  if (last_ev_ts_ > 0) {
    last_ev_age = mgos_uptime() - last_ev_ts_;
  }
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, in_mode: %d, idle_time: %d, "
      "state: %B, last_ev_age: %.3f}",
      id(), type(), (cfg_->name ? cfg_->name : ""), cfg_->in_mode,
      cfg_->idle_time, motion_detected_, last_ev_age);
}

Status MotionOccupancySensor::SetConfig(const std::string &config_json,
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
    SetMotionOccupancyDetected(md);
  }
  if (idle_time != -1) {
    cfg_->idle_time = idle_time;
  }
  return Status::OK();
}

HAPError MotionOccupancySensor::MotionDetectedRead(
    HAPAccessoryServerRef *, const HAPBoolCharacteristicReadRequest *,
    bool *value) {
  *value = motion_detected_;
  return kHAPError_None;
}

void MotionOccupancySensor::InputEventHandler(Input::Event ev, bool state) {
  if (ev != Input::Event::kChange) return;
  const auto in_mode = static_cast<InMode>(cfg_->in_mode);
  switch (in_mode) {
    case InMode::kLevel:
      SetMotionOccupancyDetected(state);
      break;
    case InMode::kPulse:
      if (state) {
        SetMotionOccupancyDetected(true);
      }
      break;
    case InMode::kMax:
      break;
  }
}

void MotionOccupancySensor::SetMotionOccupancyDetected(bool motion_detected) {
  if (motion_detected == motion_detected_) return;
  LOG(LL_INFO, ("MotionOccupancy detected: %d -> %d", motion_detected_,
                motion_detected));
  motion_detected_ = motion_detected;
  chars_[1]->RaiseEvent();
  if (motion_detected) {
    last_ev_ts_ = mgos_uptime();
    if (cfg_->in_mode == (int) InMode::kPulse) {
      auto_off_timer_.Reset(cfg_->idle_time * 1000, 0);
    }
  }
}

void MotionOccupancySensor::AutoOffTimerCB() {
  if (cfg_->in_mode != (int) InMode::kPulse) return;
  SetMotionOccupancyDetected(false);
}

}  // namespace hap
}  // namespace shelly
