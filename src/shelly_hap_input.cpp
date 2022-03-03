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

#include "shelly_hap_input.hpp"

#include "mgos.hpp"
#include "mgos_hap.h"

#include "shelly_hap_contact_sensor.hpp"
#include "shelly_hap_doorbell.hpp"
#include "shelly_hap_leak_sensor.hpp"
#include "shelly_hap_motion_sensor.hpp"
#include "shelly_hap_occupancy_sensor.hpp"
#include "shelly_hap_smoke_sensor.hpp"
#include "shelly_hap_stateless_switch.hpp"
#include "shelly_main.hpp"

namespace shelly {
namespace hap {

class ShellyDisabledInput : public Component, public mgos::hap::Service {
 public:
  explicit ShellyDisabledInput(int id)
      : Component(id), Service(0, nullptr, nullptr) {
  }
  virtual ~ShellyDisabledInput() {
  }

  Type type() const override {
    return Type::kDisabledInput;
  }

  Status Init() override {
    return Status::OK();
  }

  std::string name() const override {
    return "";
  }

  StatusOr<std::string> GetInfo() const override {
    return std::string();
  }

  StatusOr<std::string> GetInfoJSON() const override {
    return mgos::JSONPrintStringf("{id: %d, type: %d}", id(), (int) type());
  }

  Status SetConfig(const std::string &config_json,
                   bool *restart_required) override {
    (void) config_json;
    (void) restart_required;
    return Status::OK();
  }

  Status SetState(const std::string &state_json) override {
    (void) state_json;
    return Status::UNIMPLEMENTED();
  }
};

ShellyInput::ShellyInput(int id, Input *in, struct mgos_config_in *cfg)
    : Component(id),
      in_(in),
      cfg_(cfg),
      initial_type_(static_cast<Type>(cfg->type)) {
  // Always have a valid type, so it can be changed in the UI.
  // Invalid values can end up in the config due to downgrade.
  if (!IsValidType((int) initial_type_)) {
    initial_type_ = Type::kStatelessSwitch;
    cfg_->type = (int) Type::kStatelessSwitch;
  }
}

ShellyInput::~ShellyInput() {
}

Component::Type ShellyInput::type() const {
  return c_->type();
}

std::string ShellyInput::name() const {
  return c_->name();
}

Status ShellyInput::Init() {
  if (in_ == nullptr) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "input is required");
  }
  switch (initial_type_) {
    case Type::kDisabledInput: {
      c_.reset(new ShellyDisabledInput(id()));
      s_ = nullptr;
      break;
    }
    case Type::kStatelessSwitch: {
      auto *ssw = new hap::StatelessSwitch(
          id(), in_, (struct mgos_config_in_ssw *) &cfg_->ssw);
      c_.reset(ssw);
      s_ = ssw;
      break;
    }
    case Type::kMotionSensor: {
      auto *ms = new hap::MotionSensor(
          id(), in_, (struct mgos_config_in_sensor *) &cfg_->sensor);
      c_.reset(ms);
      s_ = ms;
      break;
    }
    case Type::kOccupancySensor: {
      auto *os = new hap::OccupancySensor(
          id(), in_, (struct mgos_config_in_sensor *) &cfg_->sensor);
      c_.reset(os);
      s_ = os;
      break;
    }
    case Type::kContactSensor: {
      auto *cs = new hap::ContactSensor(
          id(), in_, (struct mgos_config_in_sensor *) &cfg_->sensor);
      c_.reset(cs);
      s_ = cs;
      break;
    }
    case Type::kDoorbell: {
      auto *db = new hap::Doorbell(id(), in_,
                                   (struct mgos_config_in_ssw *) &cfg_->ssw);
      c_.reset(db);
      s_ = db;
      break;
    }
    case Type::kLeakSensor: {
      auto *cs = new hap::LeakSensor(
          id(), in_, (struct mgos_config_in_sensor *) &cfg_->sensor);
      c_.reset(cs);
      s_ = cs;
      break;
    }
    case Type::kSmokeSensor: {
      auto *cs = new hap::SmokeSensor(
          id(), in_, (struct mgos_config_in_sensor *) &cfg_->sensor);
      c_.reset(cs);
      s_ = cs;
      break;
    }
    default: {
      return mgos::Errorf(STATUS_INVALID_ARGUMENT, "Invalid type %d",
                          (int) initial_type_);
    }
  }
  in_->SetInvert(cfg_->inverted);
  return c_->Init();
}

StatusOr<std::string> ShellyInput::GetInfo() const {
  const auto &sv = c_->GetInfo();
  if (!sv.ok()) return sv;
  const auto &s = sv.ValueOrDie();
  return mgos::SPrintf("svt:%d inv:%d %s", (int) initial_type_, cfg_->inverted,
                       s.c_str());
}

StatusOr<std::string> ShellyInput::GetInfoJSON() const {
  const auto &ssi = c_->GetInfoJSON();
  if (!ssi.ok()) return ssi;
  const std::string &si = ssi.ValueOrDie();
  return mgos::JSONPrintStringf("%.*s, inverted: %B}", (int) si.length() - 1,
                                si.c_str(), cfg_->inverted);
}

Status ShellyInput::SetConfig(const std::string &config_json,
                              bool *restart_required) {
  int new_type = -2;
  int8_t inverted = -1;
  json_scanf(config_json.c_str(), config_json.size(),
             "{type: %d, inverted: %B}", &new_type, &inverted);
  if (new_type != -2 && new_type != (int) initial_type_) {
    if (!IsValidType(new_type)) {
      return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "type");
    }
    cfg_->type = new_type;
    *restart_required = true;
  }
  if (inverted != -1 && inverted != cfg_->inverted) {
    cfg_->inverted = inverted;
    *restart_required = true;
  }
  // Service may have changed but we still call SetConfig for the current one.
  return c_->SetConfig(config_json, restart_required);
}

Status ShellyInput::SetState(const std::string &state_json) {
  (void) state_json;
  return Status::UNIMPLEMENTED();
}

uint16_t ShellyInput::GetAIDBase() const {
  switch (initial_type_) {
    case Type::kDisabledInput:
      return 0;
    case Type::kStatelessSwitch:
      return SHELLY_HAP_AID_BASE_STATELESS_SWITCH;
    case Type::kMotionSensor:
      return SHELLY_HAP_AID_BASE_MOTION_SENSOR;
    case Type::kOccupancySensor:
      return SHELLY_HAP_AID_BASE_OCCUPANCY_SENSOR;
    case Type::kContactSensor:
      return SHELLY_HAP_AID_BASE_CONTACT_SENSOR;
    case Type::kDoorbell:
      return SHELLY_HAP_AID_BASE_DOORBELL;
    case Type::kLeakSensor:
      return SHELLY_HAP_AID_BASE_LEAK_SENSOR;
    case Type::kSmokeSensor:
      return SHELLY_HAP_AID_BASE_SMOKE_SENSOR;
    default:
      return 0;
  }
}

mgos::hap::Service *ShellyInput::GetService() const {
  return s_;
}

// static
bool ShellyInput::IsValidType(int type) {
  switch (type) {
    case (int) Type::kDisabledInput:
    case (int) Type::kStatelessSwitch:
    case (int) Type::kMotionSensor:
    case (int) Type::kOccupancySensor:
    case (int) Type::kContactSensor:
    case (int) Type::kDoorbell:
    case (int) Type::kLeakSensor:
    case (int) Type::kSmokeSensor:
      return true;
  }
  return false;
}

void CreateHAPInput(int id, const struct mgos_config_in *cfg,
                    std::vector<std::unique_ptr<Component>> *comps,
                    std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                    HAPAccessoryServerRef *svr) {
  Input *in = FindInput(id);
  if (in == nullptr) return;
  std::unique_ptr<ShellyInput> sin(
      new ShellyInput(id, in, (struct mgos_config_in *) cfg));
  const auto &st = sin->Init();
  if (!st.ok()) {
    const auto &sts = st.ToString();
    LOG(LL_ERROR, ("Error: %s", sts.c_str()));
    return;
  }
  if (sin->GetService() != nullptr) {
    std::unique_ptr<mgos::hap::Accessory> acc(new mgos::hap::Accessory(
        sin->GetAIDBase() + id, kHAPAccessoryCategory_BridgedAccessory,
        sin->name(), &AccessoryIdentifyCB, svr));
    acc->AddHAPService(&mgos_hap_accessory_information_service);
    acc->AddService(sin->GetService());
    accs->push_back(std::move(acc));
  }
  comps->push_back(std::move(sin));
}

}  // namespace hap
}  // namespace shelly
