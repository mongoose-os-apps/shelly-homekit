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

#include "shelly_hap_temperature_sensor.hpp"

#include <cmath>

#include "mgos.hpp"
#include "mgos_hap.hpp"

#include "shelly_main.hpp"

namespace shelly {
namespace hap {

TemperatureSensor::TemperatureSensor(int id, TempSensor *sensor,
                                     struct mgos_config_ts *cfg)
    : Component(id),
      Service(SHELLY_HAP_IID_BASE_TEMPERATURE_SENSOR +
                  SHELLY_HAP_IID_STEP_SENSOR * (id - 1),
              &kHAPServiceType_TemperatureSensor,
              kHAPServiceDebugDescription_TemperatureSensor),
      temp_sensor_(sensor),
      cfg_(cfg) {
  temp_sensor_->SetNotifier(std::bind(&TemperatureSensor::ValueChanged, this));
}

TemperatureSensor::~TemperatureSensor() {
  temp_sensor_->StopUpdating();
  temp_sensor_->SetNotifier(nullptr);
}

Component::Type TemperatureSensor::type() const {
  return Type::kTemperatureSensor;
}

std::string TemperatureSensor::name() const {
  return cfg_->name;
}

Status TemperatureSensor::SetConfig(const std::string &config_json,
                                    bool *restart_required) {
  struct mgos_config_ts cfg = *cfg_;
  cfg.name = nullptr;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, unit: %d, update_interval: %d, offset: %d", &cfg.name,
             &cfg.unit, &cfg.update_interval, &cfg.offset);

  mgos::ScopedCPtr name_owner((void *) cfg.name);
  // Validation.
  if (cfg.name != nullptr && strlen(cfg.name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (cfg.unit < 0 || cfg.unit > 1) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid unit");
  }
  if (cfg.update_interval < 1) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid update interval");
  }
  // Now copy over.
  if (cfg_->name != nullptr && strcmp(cfg_->name, cfg.name) != 0) {
    mgos_conf_set_str(&cfg_->name, cfg.name);
    *restart_required = true;
  }
  if (cfg_->unit != cfg.unit) {
    cfg_->unit = cfg.unit;
  }
  if (cfg_->update_interval != cfg.update_interval) {
    cfg_->update_interval = cfg.update_interval;
    temp_sensor_->StartUpdating(cfg_->update_interval * 1000);
  }
  if (cfg_->offset != cfg.offset) {
    cfg_->offset = cfg.offset;
  }
  return Status::OK();
}

Status TemperatureSensor::SetState(const std::string &state_json UNUSED_ARG) {
  return Status::OK();
}

void TemperatureSensor::ValueChanged() {
  auto tr = temp_sensor_->GetTemperature();
  if (tr.ok()) {
    LOG(LL_DEBUG, ("TS %d: T = %.2f", id(), tr.ValueOrDie()));
  } else {
    LOG(LL_ERROR, ("TS %d: %s", id(), tr.status().ToString().c_str()));
  }
  current_temperature_characteristic_->RaiseEvent();
}

Status TemperatureSensor::Init() {
  uint16_t iid = svc_.iid + 1;
  current_temperature_characteristic_ = new mgos::hap::FloatCharacteristic(
      iid++, &kHAPCharacteristicType_CurrentTemperature, -55.0, 125.0, 0.1,
      [this](HAPAccessoryServerRef *server UNUSED_ARG,
             const HAPFloatCharacteristicReadRequest *request UNUSED_ARG,
             float *value) {
        auto tempval = temp_sensor_->GetTemperature();
        if (!tempval.ok()) {
          return kHAPError_Busy;
        }
        float temp = static_cast<float>(tempval.ValueOrDie());
        *value = truncf((temp + cfg_->offset / 100) * 10) / 10;
        return kHAPError_None;
      },

      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_CurrentTemperature);
  AddChar(current_temperature_characteristic_);
  AddChar(new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_TemperatureDisplayUnits, 0, 1, 1,
      std::bind(&mgos::hap::ReadUInt8<int>, _1, _2, _3, &cfg_->unit),
      true /* supports_notification */,
      [this](HAPAccessoryServerRef *server UNUSED_ARG,
             const HAPUInt8CharacteristicWriteRequest *request UNUSED_ARG,
             uint8_t value) {
        if (value <= 1) {
          cfg_->unit = value;
        }
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_TemperatureDisplayUnits));

  temp_sensor_->StartUpdating(cfg_->update_interval * 1000);
  return Status::OK();
}

StatusOr<std::string> TemperatureSensor::GetInfo() const {
  auto tempval = temp_sensor_->GetTemperature();
  if (!tempval.ok()) {
    return tempval.status();
  }
  return mgos::SPrintf("t:%.2f", tempval.ValueOrDie());
}

StatusOr<std::string> TemperatureSensor::GetInfoJSON() const {
  std::string res = mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, unit: %d, "
      "update_interval: %d, offset: %d, ",
      id(), type(), cfg_->name, cfg_->unit, cfg_->update_interval,
      cfg_->offset);
  auto tempval = temp_sensor_->GetTemperature();
  if (tempval.ok()) {
    mgos::JSONAppendStringf(&res, "value: %.1f",
                            tempval.ValueOrDie() + cfg_->offset / 100);
  } else {
    mgos::JSONAppendStringf(&res, "error: %.1f", tempval.ValueOrDie());
  }
  res.append("}");
  return res;
}

void CreateHAPTemperatureSensor(
    int id, TempSensor *sensor, const struct mgos_config_ts *ts_cfg,
    std::vector<std::unique_ptr<Component>> *comps,
    std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
    HAPAccessoryServerRef *svr) {
  struct mgos_config_ts *cfg = (struct mgos_config_ts *) ts_cfg;
  std::unique_ptr<hap::TemperatureSensor> ts(
      new hap::TemperatureSensor(id, sensor, cfg));
  if (ts == nullptr || !ts->Init().ok()) {
    return;
  }

  std::unique_ptr<mgos::hap::Accessory> acc(
      new mgos::hap::Accessory(SHELLY_HAP_AID_BASE_TEMPERATURE_SENSOR + id,
                               kHAPAccessoryCategory_BridgedAccessory,
                               ts_cfg->name, GetIdentifyCB(), svr));
  acc->AddHAPService(&mgos_hap_accessory_information_service);
  acc->AddService(ts.get());
  accs->push_back(std::move(acc));
  comps->push_back(std::move(ts));
}

}  // namespace hap
}  // namespace shelly
