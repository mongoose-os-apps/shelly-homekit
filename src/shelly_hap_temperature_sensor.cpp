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

namespace shelly {
namespace hap {

TemperatureSensor::TemperatureSensor(int id, std::unique_ptr<TempSensor> sensor,
                                     struct mgos_config_se *cfg)
    : Component(id),
      Service(SHELLY_HAP_IID_BASE_TEMPERATURE_SENSOR +
                  SHELLY_HAP_IID_STEP_SENSOR * (id - 1),
              &kHAPServiceType_TemperatureSensor,
              kHAPServiceDebugDescription_TemperatureSensor),
      temp_sensor_(std::move(sensor)),
      cfg_(cfg) {
}

TemperatureSensor::~TemperatureSensor() {
}

Component::Type TemperatureSensor::type() const {
  return Type::kSensor;
}

TemperatureSensor::SensorType TemperatureSensor::sensor_type() const {
  return SensorType::kTemperatureSensor;
}

std::string TemperatureSensor::name() const {
  return cfg_->name;
}

Status TemperatureSensor::SetConfig(const std::string &config_json,
                                    bool *restart_required) {
  struct mgos_config_se cfg = *cfg_;
  cfg.name = nullptr;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, unit: %d, update_interval: %d", &cfg.name, &cfg.unit,
             &cfg.update_interval);

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
  return Status::OK();
}

Status TemperatureSensor::SetState(const std::string &state_json) {
  (void) state_json;
  return Status::OK();
}

void TemperatureSensor::ValueChanged() {
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
        *value = truncf(temp * 10) / 10;
        return kHAPError_None;
      },

      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_CurrentTemperature);
  AddChar(current_temperature_characteristic_);
  AddChar(new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_TemperatureDisplayUnits, 0, 1, 1,
      std::bind(&mgos::hap::ReadUInt8<int>, _1, _2, _3, cfg_->unit),
      true /* supports_notification */,
      [this](HAPAccessoryServerRef *server UNUSED_ARG,
             const HAPUInt8CharacteristicWriteRequest *request UNUSED_ARG,
             uint8_t value) {
        if (value >= 0 && value <= 1) {
          cfg_->unit = value;
        }
        return kHAPError_None;
      },
      kHAPCharacteristicDebugDescription_TemperatureDisplayUnits));
  temp_sensor_->notifier_ = std::bind(&TemperatureSensor::ValueChanged, this);
  LOG(LL_INFO, ("Exporting Temp"));

  temp_sensor_->StartUpdating(cfg_->update_interval * 1000);
  return Status::OK();
}

StatusOr<std::string> TemperatureSensor::GetInfo() const {
  auto tempval = temp_sensor_->GetTemperature();
  if (!tempval.ok()) {
    return tempval.status();
  }
  return mgos::SPrintf("v: %f", tempval.ValueOrDie());
}

StatusOr<std::string> TemperatureSensor::GetInfoJSON() const {
  std::string res = mgos::JSONPrintStringf(
      "{id: %d, type: %d, sensor_type: %d, name: %Q, unit: %d, "
      "update_interval: %d",
      id(), type(), sensor_type(), cfg_->name, cfg_->unit,
      cfg_->update_interval);
  auto tempval = temp_sensor_->GetTemperature();
  if (tempval.ok()) {
    mgos::JSONAppendStringf(&res, ", value: %.1f", tempval.ValueOrDie());
  }
  res.append("}");
  return res;
}

}  // namespace hap
}  // namespace shelly
