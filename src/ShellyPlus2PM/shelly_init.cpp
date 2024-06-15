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

#include "mgos_ade7953.h"

#include "shelly_dht_sensor.hpp"
#include "shelly_input_pin.hpp"
#include "shelly_main.hpp"
#include "shelly_pm.hpp"
#include "shelly_pm_ade7953.hpp"
#include "shelly_sys_led_btn.hpp"
#include "shelly_temp_sensor_ntc.hpp"
#include "shelly_temp_sensor_ow.hpp"

#include "esp_partition.h"

#include <algorithm>

#include "shelly_hap_garage_door_opener.hpp"
#include "shelly_hap_input.hpp"
#include "shelly_hap_window_covering.hpp"
#include "shelly_main.hpp"

namespace shelly {

static std::unique_ptr<Onewire> s_onewire;
static std::vector<std::unique_ptr<TempSensor>> sensors;

static struct mgos_ade7953 *s_ade7953 = NULL;

#define MGOS_ADE7953_REG_AIGAIN 0x380
#define MGOS_ADE7953_REG_AVGAIN 0x381
#define MGOS_ADE7953_REG_AWGAIN 0x382
#define MGOS_ADE7953_REG_BVGAIN 0x38D
#define MGOS_ADE7953_REG_BWGAIN 0x38E
#define MGOS_ADE7953_REG_BIGAIN 0x38C

static void ReadFactoryData(mgos_config *c) {
  const char *label = "shelly";
  esp_partition_subtype_t subtype = ESP_PARTITION_SUBTYPE_ANY;
  esp_partition_type_t type = ESP_PARTITION_TYPE_ANY;

  const esp_partition_t *part = esp_partition_find_first(type, subtype, label);
  if (part == NULL) {
    LOG(LL_INFO, ("Partition %s not found", label));
    return;
  }

  size_t part_offset = 0x0;
  size_t size = 0x2000;  // map both "partitions"
  spi_flash_mmap_handle_t out_handle;
  const void *out_ptr;

  esp_err_t err = esp_partition_mmap(
      part, part_offset, size, SPI_FLASH_MMAP_DATA, &out_ptr, &out_handle);
  if (err) {
    LOG(LL_INFO, ("Partition %s not mapped", label));
    return;
  }

  // read first partition at 0x0
  const struct mg_str str = mg_mk_str_n((const char *) out_ptr, size);
  bool succ = mgos_conf_parse_sub(str, mgos_config_get_schema(), c);
  if (!succ) {
    LOG(LL_INFO, ("Partition %s not parsed", "factory data"));
    return;
  }

  // read escond partition at 0x1000
  void *ptr2 = (uint8_t *) out_ptr + 0x1000;
  const struct mg_str str2 = mg_mk_str_n((const char *) ptr2, size);
  succ = mgos_conf_parse_sub(str2, mgos_config_get_schema(), c);
  if (!succ) {
    LOG(LL_INFO, ("Partition %s not parsed", "factory calib"));
    return;
  }

  spi_flash_munmap(out_handle);
}

static Status PowerMeterInit(std::vector<std::unique_ptr<PowerMeter>> *pms) {
  const struct mgos_config_ade7953 ade7953_cfg = {
      .voltage_scale = .0000382602,
      .voltage_offset = -0.068,
      .current_scale_0 = 0.00000949523,
      .current_scale_1 = 0.00000949523,
      .current_offset_0 = -0.017,
      .current_offset_1 = -0.017,
      .apower_scale_0 = (1 / 164.0),
      .apower_scale_1 = (1 / 164.0),
      .aenergy_scale_0 = (1 / 25240.0),
      .aenergy_scale_1 = (1 / 25240.0),
      .voltage_pga_gain = 0,    // MGOS_ADE7953_PGA_GAIN_2,
      .current_pga_gain_0 = 0,  // MGOS_ADE7953_PGA_GAIN_8,
      .current_pga_gain_1 = 0,  // MGOS_ADE7953_PGA_GAIN_8,
  };

  mgos_config_factory *c = &(mgos_sys_config.factory);

  int reset_pin = -1;
  bool conf_changed = false;
  if (c->model != NULL && strcmp(c->model, "SNSW-102P16EU") == 0) {
    if (mgos_sys_config_get_i2c_sda_gpio() != 26) {
      mgos_sys_config_set_i2c_sda_gpio(26);
      conf_changed = true;
    }
    reset_pin = 33;
  } else {
    if (mgos_sys_config_get_i2c_sda_gpio() != 33) {
      mgos_sys_config_set_i2c_sda_gpio(33);
      conf_changed = true;
    }
    reset_pin = -1;  // TODO: unknown?
  }

  if (conf_changed) {
    mgos_sys_config_save(&mgos_sys_config, false /* try_once */,
                         NULL /* msg */);
    LOG(LL_INFO, ("i2c config changed. reboot necessary to detect PM"));
  }

  if (c->model != NULL && c->batch != NULL) {
    LOG(LL_INFO, ("factory data: model: %s batch: %s", c->model, c->batch));
  }

  if (reset_pin != -1) {
    mgos_gpio_setup_output(reset_pin, 0);
    mgos_usleep(10);
    mgos_gpio_setup_output(reset_pin, 1);
    mgos_msleep(10);
  }

  s_ade7953 = mgos_ade7953_create(mgos_i2c_get_global(), &ade7953_cfg);

  if (s_ade7953 == nullptr) {
    LOG(LL_INFO, ("Failed to init ADE7953"));

    return mgos::Errorf(STATUS_UNAVAILABLE, "Failed to init ADE7953");
  }

  if (c->calib.done && false) {  // do not use for now

    mgos_config_gains *g = &c->calib.gains0;
    LOG(LL_INFO, ("gains: av %f ai %f aw %f", g->avgain, g->aigain, g->awgain));
    mgos_ade7953_write_reg(s_ade7953, MGOS_ADE7953_REG_AVGAIN, g->avgain);
    mgos_ade7953_write_reg(s_ade7953, MGOS_ADE7953_REG_AIGAIN, g->aigain);
    mgos_ade7953_write_reg(s_ade7953, MGOS_ADE7953_REG_AWGAIN, g->awgain);
    mgos_ade7953_write_reg(s_ade7953, MGOS_ADE7953_REG_BVGAIN, g->bvgain);
    mgos_ade7953_write_reg(s_ade7953, MGOS_ADE7953_REG_BIGAIN, g->bigain);
    mgos_ade7953_write_reg(s_ade7953, MGOS_ADE7953_REG_BWGAIN, g->bwgain);
  }

  Status st;
  std::unique_ptr<PowerMeter> pm1(new ADE7953PowerMeter(1, s_ade7953, 0));
  if (!(st = pm1->Init()).ok()) return st;
  std::unique_ptr<PowerMeter> pm2(new ADE7953PowerMeter(2, s_ade7953, 1));
  if (!(st = pm2->Init()).ok()) return st;

  pms->emplace_back(std::move(pm1));
  pms->emplace_back(std::move(pm2));

  return Status::OK();
}

void CreatePeripherals(std::vector<std::unique_ptr<Input>> *inputs,
                       std::vector<std::unique_ptr<Output>> *outputs,
                       std::vector<std::unique_ptr<PowerMeter>> *pms,
                       std::unique_ptr<TempSensor> *sys_temp) {
  ReadFactoryData(&mgos_sys_config);

  outputs->emplace_back(new OutputPin(1, 13, 1));
  outputs->emplace_back(new OutputPin(2, 12, 1));

  bool new_rev = false;
  mgos_config_factory *c = &(mgos_sys_config.factory);
  if (c->model != NULL && strcmp(c->model, "SNSW-102P16EU") == 0) {
    new_rev = true;
  }

  int pin1 = new_rev ? 5 : 2;

  auto *in1 = new InputPin(1, pin1, 1, MGOS_GPIO_PULL_NONE, true);
  in1->AddHandler(std::bind(&HandleInputResetSequence, in1, 4, _1, _2));
  in1->Init();
  inputs->emplace_back(in1);
  auto *in2 = new InputPin(2, 18, 1, MGOS_GPIO_PULL_NONE, false);
  in2->Init();
  inputs->emplace_back(in2);

  const Status &st = PowerMeterInit(pms);
  if (!st.ok()) {
    const std::string &s = st.ToString();
    LOG(LL_INFO, ("Failed to init ADE7953: %s", s.c_str()));
  }

  int adc_pin = new_rev ? 35 : 37;
  sys_temp->reset(new TempSensorSDNT1608X103F3950(adc_pin, 3.3f, 10000.0f));

  int pin_out = 0;
  int pin_in = 1;

  if (DetectAddon(pin_in, pin_out)) {
    s_onewire.reset(new Onewire(pin_in, pin_out));
    sensors = s_onewire->DiscoverAll();
    if (sensors.empty()) {
      s_onewire.reset();
      sensors = DiscoverDHTSensors(pin_in, pin_out);
    }

    auto *in_digital = new InputPin(3, 19, 0, MGOS_GPIO_PULL_NONE, false);
    in_digital->Init();
    inputs->emplace_back(in_digital);

  } else {
    RestoreUART();
    InitSysLED(LED_GPIO, LED_ON);
  }

  InitSysBtn(new_rev ? BTN_GPIO : 27, BTN_DOWN);
}

void CreateComponents(std::vector<std::unique_ptr<Component>> *comps,
                      std::vector<std::unique_ptr<mgos::hap::Accessory>> *accs,
                      HAPAccessoryServerRef *svr) {
  if (mgos_sys_config_get_shelly_mode() == (int) Mode::kRollerShutter) {
    hap::CreateHAPWC(1, FindInput(1), FindInput(2), FindOutput(1),
                     FindOutput(2), FindPM(1), FindPM(2),
                     mgos_sys_config_get_wc1(), mgos_sys_config_get_in1(),
                     mgos_sys_config_get_in2(), comps, accs, svr);
  } else if (mgos_sys_config_get_shelly_mode() == (int) Mode::kGarageDoor) {
    hap::CreateHAPGDO(1, FindInput(1), FindInput(2), FindOutput(1),
                      FindOutput(2), mgos_sys_config_get_gdo1(), comps, accs,
                      svr, true);
  } else {
    CreateHAPSwitch(1, mgos_sys_config_get_sw1(), mgos_sys_config_get_in1(),
                    comps, accs, svr, false /* to_pri_acc */);
    CreateHAPSwitch(2, mgos_sys_config_get_sw2(), mgos_sys_config_get_in2(),
                    comps, accs, svr, false /* to_pri_acc */);
  }

  if (!sensors.empty()) {
    CreateHAPSensors(&sensors, comps, accs, svr);
  }

  bool additional_input_digital = (FindInput(3) != nullptr);
  if (additional_input_digital) {
    hap::CreateHAPInput(3, mgos_sys_config_get_in3(), comps, accs, svr);
  }
}

}  // namespace shelly
