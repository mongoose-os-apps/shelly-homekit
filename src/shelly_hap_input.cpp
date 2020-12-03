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

#include "shelly_hap_input.hpp"

#include "mgos.hpp"
#include "mgos_hap.h"

#include "shelly_hap_motion_occupancy_sensor.hpp"
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
};

ShellyInput::ShellyInput(int id, Input *in, struct mgos_config_in *cfg)
    : Component(id),
      in_(in),
      cfg_(cfg),
      svc_type_(static_cast<ServiceType>(cfg->svc_type)) {
  // Always have a valid type, so it can be changed in the UI.
  // Invalid values can end up in the config due to downgrade.
  if ((int) svc_type_ < 0 || (int) svc_type_ >= (int) ServiceType::kMax) {
    svc_type_ = ServiceType::kStatelessSwitch;
    cfg_->svc_type = (int) ServiceType::kStatelessSwitch;
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
  in_->SetInvert(cfg_->inverted);
  switch (svc_type_) {
    case ShellyInput::ServiceType::kDisabled: {
      c_.reset(new ShellyDisabledInput(id()));
      s_ = nullptr;
      break;
    }
    case ShellyInput::ServiceType::kStatelessSwitch: {
      auto *ssw = new hap::StatelessSwitch(
          id(), in_, (struct mgos_config_in_ssw *) &cfg_->ssw);
      c_.reset(ssw);
      s_ = ssw;
      break;
    }
    case ShellyInput::ServiceType::kMotionSensor: {
      auto *ms = new hap::MotionOccupancySensor(
          id(), in_, (struct mgos_config_in_ms *) &cfg_->ms,
          false /* occupancy */);
      c_.reset(ms);
      s_ = ms;
      break;
    }
    case ShellyInput::ServiceType::kOccupancySensor: {
      auto *ms = new hap::MotionOccupancySensor(
          id(), in_, (struct mgos_config_in_ms *) &cfg_->ms,
          true /* occupancy */);
      c_.reset(ms);
      s_ = ms;
      break;
    }
    default: {
      return mgos::Errorf(STATUS_INVALID_ARGUMENT, "Invalid svc_type %d",
                          (int) svc_type_);
    }
  }
  return c_->Init();
}

StatusOr<std::string> ShellyInput::GetInfo() const {
  const auto &sv = c_->GetInfo();
  if (!sv.ok()) return sv;
  const auto &s = sv.ValueOrDie();
  return mgos::SPrintf("svt:%d inv:%d %s", (int) svc_type_, cfg_->inverted,
                       s.c_str());
}

StatusOr<std::string> ShellyInput::GetInfoJSON() const {
  const auto &ssi = c_->GetInfoJSON();
  if (!ssi.ok()) return ssi;
  const std::string &si = ssi.ValueOrDie();
  return mgos::JSONPrintStringf("%.*s, svc_type: %d, inverted: %B}",
                                (int) si.length() - 1, si.c_str(),
                                cfg_->svc_type, cfg_->inverted);
}

Status ShellyInput::SetConfig(const std::string &config_json,
                              bool *restart_required) {
  int svc_type = -2;
  int8_t inverted = -1;
  json_scanf(config_json.c_str(), config_json.size(),
             "{svc_type: %d, inverted: %B}", &svc_type, &inverted);
  if (svc_type != -2 && svc_type != (int) svc_type_) {
    if (svc_type < 0 || svc_type >= (int) ServiceType::kMax) {
      return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "svc_type");
    }
    cfg_->svc_type = svc_type;
    *restart_required = true;
  }
  if (inverted != -1 && inverted != cfg_->inverted) {
    cfg_->inverted = inverted;
    *restart_required = true;
  }
  // Service may have changed but we still call SetConfig for the current one.
  return c_->SetConfig(config_json, restart_required);
}

uint16_t ShellyInput::GetAIDBase() const {
  switch (svc_type_) {
    case ShellyInput::ServiceType::kDisabled:
      return 0;
    case ShellyInput::ServiceType::kStatelessSwitch:
      return SHELLY_HAP_AID_BASE_STATELESS_SWITCH;
    case ShellyInput::ServiceType::kMotionSensor:
      return SHELLY_HAP_AID_BASE_MOTION_SENSOR;
    case ShellyInput::ServiceType::kOccupancySensor:
      return SHELLY_HAP_AID_BASE_OCCUPANCY_SENSOR;
    default:
      return 0;
  }
}

mgos::hap::Service *ShellyInput::GetService() const {
  return s_;
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
