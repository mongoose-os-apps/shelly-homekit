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

#include "shelly_hap_humidity_sensor.hpp"

#include <cmath>

#include "mgos.hpp"
#include "mgos_hap.hpp"

#include "shelly_main.hpp"

namespace shelly {
namespace hap {

HumiditySensor::HumiditySensor(int id, HumidityTempSensor *sensor,
                               struct mgos_config_ts *cfg)
    : Component(id),
      Service(SHELLY_HAP_IID_BASE_HUMIDITY_SENSOR +
                  SHELLY_HAP_IID_STEP_SENSOR * (id - 1),
              &kHAPServiceType_HumiditySensor,
              kHAPServiceDebugDescription_HumiditySensor),
      hum_sensor_(sensor),
      cfg_(cfg) {
  hum_sensor_->SetNotifierHumidity(
      std::bind(&HumiditySensor::ValueChanged, this));
}

HumiditySensor::~HumiditySensor() {
  hum_sensor_->StopUpdating();
  hum_sensor_->SetNotifier(nullptr);
}

Component::Type HumiditySensor::type() const {
  return Type::kTemperatureSensor;
}

std::string HumiditySensor::name() const {
  return cfg_->name;
}

Status HumiditySensor::SetConfig(const std::string &config_json,
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
  if (cfg.unit != 2) {
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
  if (cfg_->offset != cfg.offset) {
    cfg_->offset = cfg.offset;
  }
  if (cfg_->update_interval != cfg.update_interval) {
    cfg_->update_interval = cfg.update_interval;
    // update interval is set via temperature sensor on DHT
  }
  return Status::OK();
}

Status HumiditySensor::SetState(const std::string &state_json UNUSED_ARG) {
  return Status::OK();
}

void HumiditySensor::ValueChanged() {
  auto tr = hum_sensor_->GetTemperature();
  if (tr.ok()) {
    LOG(LL_DEBUG, ("TS %d: T = %.2f", id(), tr.ValueOrDie()));
  } else {
    LOG(LL_ERROR, ("TS %d: %s", id(), tr.status().ToString().c_str()));
  }
  current_humidity_characteristic_->RaiseEvent();
}

Status HumiditySensor::Init() {
  uint16_t iid = svc_.iid + 1;
  current_humidity_characteristic_ = new mgos::hap::FloatCharacteristic(
      iid++, &kHAPCharacteristicType_CurrentRelativeHumidity, 0, 100.0, 1,
      [this](HAPAccessoryServerRef *server UNUSED_ARG,
             const HAPFloatCharacteristicReadRequest *request UNUSED_ARG,
             float *value) {
        auto tempval = hum_sensor_->GetHumidity();
        if (!tempval.ok()) {
          return kHAPError_Busy;
        }
        float temp = static_cast<float>(tempval.ValueOrDie());
        *value = truncf((temp + cfg_->offset / 100.0) * 10) / 10;
        return kHAPError_None;
      },

      true /* supports_notification */, nullptr /* write_handler */,
      kHAPCharacteristicDebugDescription_CurrentRelativeHumidity);
  AddChar(current_humidity_characteristic_);

  return Status::OK();
}

StatusOr<std::string> HumiditySensor::GetInfo() const {
  auto tempval = hum_sensor_->GetHumidity();
  if (!tempval.ok()) {
    return tempval.status();
  }
  return mgos::SPrintf("t:%.2f", tempval.ValueOrDie());
}

StatusOr<std::string> HumiditySensor::GetInfoJSON() const {
  std::string res = mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, unit: %d, "
      "update_interval: %d, offset: %d, ",
      id(), type(), cfg_->name, 2, cfg_->update_interval, cfg_->offset);
  auto tempval = hum_sensor_->GetHumidity();
  if (tempval.ok()) {
    mgos::JSONAppendStringf(&res, "value: %.1f",
                            tempval.ValueOrDie() + cfg_->offset / 100.0);
  } else {
    mgos::JSONAppendStringf(&res, "error: %.1f", tempval.ValueOrDie());
  }
  res.append("}");
  return res;
}

void CreateHAPHumiditySensor(
    int id, HumidityTempSensor *sensor, const struct mgos_config_ts *ts_cfg,
    std::vector<std::unique_ptr<Component>> *comps,
    std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
    HAPAccessoryServerRef *svr) {
  struct mgos_config_ts *cfg = (struct mgos_config_ts *) ts_cfg;
  std::unique_ptr<hap::HumiditySensor> ts(
      new hap::HumiditySensor(id, sensor, cfg));
  if (ts == nullptr || !ts->Init().ok()) {
    return;
  }

  std::unique_ptr<mgos::hap::Accessory> acc(
      new mgos::hap::Accessory(SHELLY_HAP_AID_BASE_HUMIDITY_SENSOR + id,
                               kHAPAccessoryCategory_BridgedAccessory,
                               ts_cfg->name, GetIdentifyCB(), svr));
  acc->AddHAPService(&mgos_hap_accessory_information_service);
  acc->AddService(ts.get());
  accs->push_back(std::move(acc));
  comps->push_back(std::move(ts));
}

}  // namespace hap
}  // namespace shelly
