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

#include <memory>
#include <vector>

#include "mgos_sys_config.h"
#include "mgos_timers.h"

#include "shelly_common.h"
#include "shelly_component.h"
#include "shelly_hap.h"
#include "shelly_hap_chars.h"
#include "shelly_input.h"
#include "shelly_output.h"

namespace shelly {

class HAPSwitch : public Component, Service {
 public:
  struct Info {
    bool state;
    float apower;
    float aenergy;
  };

  enum class ServiceType {
    SWITCH = 0,
    OUTLET = 1,
    LOCK = 2,
  };

  enum class InMode {
    MOMENTARY = 0,
    TOGGLE = 1,
    EDGE = 2,
    DETACHED = 3,
  };

  enum class InitialState {
    OFF = 0,
    ON = 1,
    LAST = 2,
    INPUT = 3,
  };

  HAPSwitch(Input *in, Output *out, PowerMeter *out_pm,
            const struct mgos_config_sw *cfg, HAPAccessoryServerRef *server,
            const HAPAccessory *accessory);
  virtual ~HAPSwitch();

  StatusOr<std::string> GetInfo() const override;
  const HAPService *GetHAPService() const override;

  Status Init() override;

  void SetState(bool new_state, const char *source);

 private:
  void InputEventHandler(Input::Event ev, bool state);

  void SetStateInternal(bool new_state, const char *source, bool is_auto_off);

  static void AutoOffTimerCB(void *ctx);

  void SaveState();

  HAPError HandleOnRead(HAPAccessoryServerRef *server,
                        const HAPBoolCharacteristicReadRequest *request,
                        bool *value);
  HAPError HandleOnWrite(HAPAccessoryServerRef *server,
                         const HAPBoolCharacteristicWriteRequest *request,
                         bool value);
  HAPError HandleOutletInUseRead(
      HAPAccessoryServerRef *server,
      const HAPBoolCharacteristicReadRequest *request, bool *value);
  HAPError HandleLockCurrentStateRead(
      HAPAccessoryServerRef *server,
      const HAPUInt8CharacteristicReadRequest *request, uint8_t *value);
  HAPError HandleLockTargetStateWrite(
      HAPAccessoryServerRef *server,
      const HAPUInt8CharacteristicWriteRequest *request, uint8_t value);

  Input *const in_;
  Output *const out_;
  PowerMeter *const out_pm_;
  const struct mgos_config_sw *const cfg_;
  HAPAccessoryServerRef *const server_;
  const HAPAccessory *const accessory_;

  HAPService svc_;
  Input::HandlerID handler_id_ = Input::kInvalidHandlerID;
  HAPCharacteristic *state_notify_char_ = nullptr;
  HAPCharacteristic *tgt_state_notify_char_ = nullptr;
  std::vector<std::unique_ptr<ShellyHAPCharacteristic>> chars_;
  std::vector<HAPCharacteristic *> hap_chars_;

  mgos_timer_id auto_off_timer_id_ = MGOS_INVALID_TIMER_ID;

  HAPSwitch(const HAPSwitch &other) = delete;
};

}  // namespace shelly
