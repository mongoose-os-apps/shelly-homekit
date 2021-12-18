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
#include "mgos.hpp"

#include <cmath>

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

Component::SensorType TemperatureSensor::sensor_type() const {
  return SensorType::kTemperatureSensor;
}

std::string TemperatureSensor::name() const {
  return cfg_->name;
}

Status TemperatureSensor::SetConfig(const std::string &config_json,
                                    bool *restart_required) {
  struct mgos_config_se cfg = *cfg_;
  cfg.name = nullptr;
  json_scanf(config_json.c_str(), config_json.size(), "{name: %Q, unit: %d",
             &cfg.name, &cfg.unit);

  mgos::ScopedCPtr name_owner((void *) cfg.name);
  // Validation.
  if (cfg.name != nullptr && strlen(cfg.name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (cfg.unit < 0 || cfg.unit > 1) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid unit");
  }
  // Now copy over.
  if (cfg_->name != nullptr && strcmp(cfg_->name, cfg.name) != 0) {
    mgos_conf_set_str(&cfg_->name, cfg.name);
    *restart_required = true;
  }
  if (cfg_->unit != cfg.unit) {
    cfg_->unit = cfg.unit;
  }
  return Status::OK();
}

Status TemperatureSensor::SetState(const std::string &state_json) {
  (void) state_json;
  return Status::OK();
}

void TemperatureSensor::ValueChanged() {
  current_temperature_characteristic->RaiseEvent();
}

Status TemperatureSensor::Init() {
  uint16_t iid = svc_.iid + 1;
  current_temperature_characteristic = new mgos::hap::FloatCharacteristic(
      iid++, &kHAPCharacteristicType_CurrentTemperature, 0.0, 100.0, 0.1,
      std::bind(&TemperatureSensor::HandleCurrentTemperatureRead, this, _1, _2,
                _3),
      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_CurrentTemperature);
  AddChar(current_temperature_characteristic);
  AddChar(new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_TemperatureDisplayUnits, 0, 255, 1,
      std::bind(&TemperatureSensor::HandleTemperatureDisplayUnitsRead, this, _1,
                _2, _3),
      true /* supports_notification */,
      std::bind(&TemperatureSensor::HandleTemperatureDisplayUnitsWrite, this,
                _1, _2, _3),
      kHAPCharacteristicDebugDescription_TemperatureDisplayUnits));
  temp_sensor_->notifier_ = std::bind(&TemperatureSensor::ValueChanged, this);
  LOG(LL_INFO, ("Exporting Temp"));

  temp_sensor_->StartUpdating(5000);
  return Status::OK();
}

HAPError TemperatureSensor::HandleCurrentTemperatureRead(
    HAPAccessoryServerRef *server,
    const HAPFloatCharacteristicReadRequest *request, float *value) {
  float temp = static_cast<float>(temp_sensor_->GetTemperature().ValueOrDie());
  *value = truncf(temp * 10) / 10;
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError TemperatureSensor::HandleTemperatureDisplayUnitsWrite(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicWriteRequest *request, uint8_t value) {
  (void) value;
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError TemperatureSensor::HandleTemperatureDisplayUnitsRead(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicReadRequest *request, uint8_t *value) {
  *value = cfg_->unit;
  (void) server;
  (void) request;
  return kHAPError_None;
}

StatusOr<std::string> TemperatureSensor::GetInfo() const {
  return mgos::SPrintf("v: %f", temp_sensor_->GetTemperature().ValueOrDie());
}

StatusOr<std::string> TemperatureSensor::GetInfoJSON() const {
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, sensor_type: %d, name: %Q, value: %f, unit: %d}",
      id(), type(), sensor_type(), cfg_->name,
      temp_sensor_->GetTemperature().ValueOrDie(), cfg_->unit);
}

}  // namespace hap
}  // namespace shelly
