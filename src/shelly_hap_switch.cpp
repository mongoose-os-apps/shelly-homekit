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

#include "shelly_hap_switch.h"

#include "mgos.h"

#include "shelly_hap_chars.h"

#define IID_BASE_SWITCH 0x100
#define IID_STEP_SWITCH 4
#define IID_BASE_OUTLET 0x200
#define IID_STEP_OUTLET 5
#define IID_BASE_LOCK 0x300
#define IID_STEP_LOCK 4

namespace shelly {

HAPSwitch::HAPSwitch(int id, Input *in, Output *out, PowerMeter *out_pm,
                     struct mgos_config_sw *cfg, HAPAccessoryServerRef *server,
                     const HAPAccessory *accessory)
    : Component(id),
      in_(in),
      out_(out),
      out_pm_(out_pm),
      cfg_(cfg),
      server_(server),
      accessory_(accessory),
      svc_({}),
      auto_off_timer_id_(MGOS_INVALID_TIMER_ID) {
}

HAPSwitch::~HAPSwitch() {
  if (auto_off_timer_id_ != MGOS_INVALID_TIMER_ID) {
    mgos_clear_timer(auto_off_timer_id_);
    auto_off_timer_id_ = MGOS_INVALID_TIMER_ID;
  }
  if (in_ != nullptr) {
    in_->RemoveHandler(handler_id_);
  }
}

StatusOr<std::string> HAPSwitch::GetInfo() const {
  std::string res = mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, svc_type: %d, in_mode: %d, initial: %d, "
      "state: %B, auto_off: %B, auto_off_delay: %.3f",
      id(), type(), (cfg_->name ? cfg_->name : ""), cfg_->svc_type,
      cfg_->in_mode, cfg_->initial_state, out_->GetState(), cfg_->auto_off,
      cfg_->auto_off_delay);
  if (out_pm_ != nullptr) {
    auto power = out_pm_->GetPowerW();
    if (power.ok()) {
      mgos::JSONAppendStringf(&res, ", apower: %.3f", power.ValueOrDie());
    }
    auto energy = out_pm_->GetEnergyWH();
    if (energy.ok()) {
      mgos::JSONAppendStringf(&res, ", aenergy: %.3f", energy.ValueOrDie());
    }
  }
  res.append("}");
  return res;
}

Status HAPSwitch::SetConfig(const std::string &config_json,
                            bool *restart_required) {
  struct mgos_config_sw cfg = *cfg_;
  cfg.name = nullptr;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, svc_type: %d, in_mode: %d, initial_state: %d, "
             "auto_off: %B, auto_off_delay: %lf}",
             &cfg.name, &cfg.svc_type, &cfg.in_mode, &cfg.initial_state,
             &cfg.auto_off, &cfg.auto_off_delay);
  std::unique_ptr<char> name_owner((char *) cfg.name);
  // Validation.
  if (cfg.name == nullptr || strlen(cfg.name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (cfg.svc_type < -1 || cfg.svc_type > 2) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "svc_type");
  }
  if (cfg.in_mode < 0 || cfg.in_mode > 3) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "in_mode");
  }
  if (cfg.initial_state < 0 || cfg.initial_state > 3) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "initial_state");
  }
  cfg.auto_off = (cfg.auto_off != 0);
  if (cfg.initial_state < 0 || cfg.initial_state > 3) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "initial_state");
  }
  // Now copy over.
  *restart_required = false;
  if (cfg_->name != nullptr && strcmp(cfg_->name, cfg.name) != 0) {
    mgos_conf_set_str(&cfg_->name, cfg.name);
    *restart_required = true;
  }
  if (cfg_->svc_type != cfg.svc_type) {
    cfg_->svc_type = cfg.svc_type;
    *restart_required = true;
  }
  cfg_->in_mode = cfg.in_mode;
  cfg_->initial_state = cfg.initial_state;
  cfg_->auto_off = cfg.auto_off;
  cfg_->auto_off_delay = cfg.auto_off_delay;
  return Status::OK();
}

const HAPService *HAPSwitch::GetHAPService() const {
  if (cfg_->svc_type < 0) return nullptr;
  return &svc_;
}

Status HAPSwitch::Init() {
  if (!cfg_->enable) {
    LOG(LL_INFO, ("'%s' is disabled", cfg_->name));
    mgos_gpio_setup_output(cfg_->out_gpio, !cfg_->out_on_value);
    return Status::OK();
  }
  svc_.name = cfg_->name;
  svc_.properties.primaryService = true;
  const char *svc_type_name = NULL;
  const int id1 = id() - 1;  // IDs used to start at 0, preserve compat.
  switch (static_cast<ServiceType>(cfg_->svc_type)) {
    case ServiceType::kDisabled: {
      svc_type_name = "disabled";
      break;
    }
    case ServiceType::kSwitch: {
      uint16_t iid = IID_BASE_SWITCH + (IID_STEP_SWITCH * id1);
      svc_.iid = iid++;
      svc_.serviceType = &kHAPServiceType_Switch;
      svc_.debugDescription = kHAPServiceDebugDescription_Switch;
      // Name
      std::unique_ptr<ShellyHAPCharacteristic> name_char(
          new ShellyHAPStringCharacteristic(
              iid++, &kHAPCharacteristicType_Name, 64, cfg_->name,
              kHAPCharacteristicDebugDescription_Name));
      hap_chars_.push_back(name_char->GetBase());
      chars_.emplace_back(std::move(name_char));
      // On
      std::unique_ptr<ShellyHAPCharacteristic> on_char(
          new ShellyHAPBoolCharacteristic(
              iid++, &kHAPCharacteristicType_On,
              std::bind(&HAPSwitch::HandleOnRead, this, _1, _2, _3),
              std::bind(&HAPSwitch::HandleOnWrite, this, _1, _2, _3),
              kHAPCharacteristicDebugDescription_On));
      hap_chars_.push_back(on_char->GetBase());
      state_notify_char_ = on_char->GetBase();
      chars_.emplace_back(std::move(on_char));

      svc_type_name = "switch";
      break;
    }
    case ServiceType::kOutlet: {
      uint16_t iid = IID_BASE_OUTLET + (IID_STEP_OUTLET * id1);
      svc_.iid = iid++;
      svc_.serviceType = &kHAPServiceType_Outlet;
      svc_.debugDescription = kHAPServiceDebugDescription_Outlet;
      // Name
      std::unique_ptr<ShellyHAPCharacteristic> name_char(
          new ShellyHAPStringCharacteristic(
              iid++, &kHAPCharacteristicType_Name, 64, cfg_->name,
              kHAPCharacteristicDebugDescription_Name));
      hap_chars_.push_back(name_char->GetBase());
      chars_.emplace_back(std::move(name_char));
      // On
      std::unique_ptr<ShellyHAPCharacteristic> on_char(
          new ShellyHAPBoolCharacteristic(
              iid++, &kHAPCharacteristicType_On,
              std::bind(&HAPSwitch::HandleOnRead, this, _1, _2, _3),
              std::bind(&HAPSwitch::HandleOnWrite, this, _1, _2, _3),
              kHAPCharacteristicDebugDescription_On));
      hap_chars_.push_back(on_char->GetBase());
      state_notify_char_ = on_char->GetBase();
      chars_.emplace_back(std::move(on_char));
      // In Use
      std::unique_ptr<ShellyHAPCharacteristic> in_use_char(
          new ShellyHAPBoolCharacteristic(
              iid++, &kHAPCharacteristicType_OutletInUse,
              std::bind(&HAPSwitch::HandleOutletInUseRead, this, _1, _2, _3),
              nullptr,  // write_handler
              kHAPCharacteristicDebugDescription_OutletInUse));
      hap_chars_.push_back(in_use_char->GetBase());
      chars_.emplace_back(std::move(in_use_char));
      svc_type_name = "outlet";
      break;
    }
    case ServiceType::kLock: {
      uint16_t iid = IID_BASE_LOCK + (IID_STEP_LOCK * id1);
      svc_.iid = iid++;
      svc_.serviceType = &kHAPServiceType_LockMechanism;
      svc_.debugDescription = kHAPServiceDebugDescription_LockMechanism;
      // Name
      std::unique_ptr<ShellyHAPCharacteristic> name_char(
          new ShellyHAPStringCharacteristic(
              iid++, &kHAPCharacteristicType_Name, 64, cfg_->name,
              kHAPCharacteristicDebugDescription_Name));
      hap_chars_.push_back(name_char->GetBase());
      chars_.emplace_back(std::move(name_char));
      // Current State
      std::unique_ptr<ShellyHAPCharacteristic> cur_state_char(
          new shelly::ShellyHAPUInt8Characteristic(
              iid++, &kHAPCharacteristicType_LockCurrentState, 0, 3, 1,
              std::bind(&HAPSwitch::HandleLockCurrentStateRead, this, _1, _2,
                        _3),
              nullptr,  // write_handler
              kHAPCharacteristicDebugDescription_LockCurrentState));
      hap_chars_.push_back(cur_state_char->GetBase());
      state_notify_char_ = cur_state_char->GetBase();
      chars_.emplace_back(std::move(cur_state_char));
      // Target State
      std::unique_ptr<ShellyHAPCharacteristic> tgt_state_char(
          new shelly::ShellyHAPUInt8Characteristic(
              iid++, &kHAPCharacteristicType_LockTargetState, 0, 3, 1,
              std::bind(&HAPSwitch::HandleLockCurrentStateRead, this, _1, _2,
                        _3),
              std::bind(&HAPSwitch::HandleLockTargetStateWrite, this, _1, _2,
                        _3),
              kHAPCharacteristicDebugDescription_LockTargetState));
      hap_chars_.push_back(tgt_state_char->GetBase());
      tgt_state_notify_char_ = tgt_state_char->GetBase();
      chars_.emplace_back(std::move(tgt_state_char));
      svc_type_name = "lock";
      break;
    }
  }
  hap_chars_.push_back(nullptr);
  svc_.characteristics = hap_chars_.data();
  switch (static_cast<InitialState>(cfg_->initial_state)) {
    case InitialState::kOff:
      SetState(false, "init");
      break;
    case InitialState::kOn:
      SetState(true, "init");
      break;
    case InitialState::kLast:
      SetState(cfg_->state, "init");
      break;
    case InitialState::kInput:
      if (in_ != nullptr &&
          cfg_->in_mode == static_cast<int>(InMode::kToggle)) {
        SetState(in_->GetState(), "init");
      }
      break;
  }
  LOG(LL_INFO,
      ("Exporting '%s': type %s, GPIO out: %d, in: %d, state: %d", cfg_->name,
       svc_type_name, cfg_->out_gpio, cfg_->in_gpio, out_->GetState()));
  if (in_ != nullptr) {
    handler_id_ =
        in_->AddHandler(std::bind(&HAPSwitch::InputEventHandler, this, _1, _2));
  }
  return Status::OK();
}

void HAPSwitch::SetState(bool new_state, const char *source) {
  SetStateInternal(new_state, source, false /* is_auto_off */);
}

void HAPSwitch::SetStateInternal(bool new_state, const char *source,
                                 bool is_auto_off) {
  bool cur_state = out_->GetState();
  out_->SetState(new_state, source);
  if (cfg_->state != new_state) {
    cfg_->state = new_state;
    mgos_sys_config_save(&mgos_sys_config, false /* try_once */,
                         NULL /* msg */);
  }
  if (new_state == cur_state) return;
  if (state_notify_char_ != nullptr) {
    HAPAccessoryServerRaiseEvent(server_, state_notify_char_, &svc_,
                                 accessory_);
  }

  if (auto_off_timer_id_ != MGOS_INVALID_TIMER_ID) {
    // Cancel timer if state changes so that only the last timer is triggered if
    // state changes multiple times
    mgos_clear_timer(auto_off_timer_id_);
    auto_off_timer_id_ = MGOS_INVALID_TIMER_ID;
  }

  if (cfg_->auto_off && !is_auto_off) {
    auto_off_timer_id_ =
        mgos_set_timer(cfg_->auto_off_delay * 1000, 0, AutoOffTimerCB, this);
    LOG(LL_INFO,
        ("%d: Set auto-off timer for %.3f", id(), cfg_->auto_off_delay));
  }
}

// static
void HAPSwitch::AutoOffTimerCB(void *ctx) {
  HAPSwitch *sw = static_cast<HAPSwitch *>(ctx);
  sw->auto_off_timer_id_ = MGOS_INVALID_TIMER_ID;
  if (sw->cfg_->auto_off) {
    // Don't set state if auto off has been disabled during timer run
    sw->SetStateInternal(false, "auto_off", true /* is_auto_off */);
  }
}

void HAPSwitch::InputEventHandler(Input::Event ev, bool state) {
  if (ev != Input::Event::kChange) return;
  switch (static_cast<InMode>(cfg_->in_mode)) {
    case InMode::kMomentary:
      if (state) {  // Only on 0 -> 1 transitions.
        SetState(!out_->GetState(), "button");
      }
      break;
    case InMode::kToggle:
      SetState(state, "switch");
      break;
    case InMode::kEdge:
      SetState(!out_->GetState(), "button");
      break;
    case InMode::kDetached:
      // Nothing to do
      break;
  }
}

HAPError HAPSwitch::HandleOnRead(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicReadRequest *request, bool *value) {
  *value = out_->GetState();
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError HAPSwitch::HandleOnWrite(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicWriteRequest *request, bool value) {
  SetState(value, "HAP");
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError HAPSwitch::HandleOutletInUseRead(
    HAPAccessoryServerRef *server,
    const HAPBoolCharacteristicReadRequest *request, bool *value) {
  *value = true;
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError HAPSwitch::HandleLockCurrentStateRead(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicReadRequest *request, uint8_t *value) {
  *value = (out_->GetState() ? 0 : 1);
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError HAPSwitch::HandleLockTargetStateWrite(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicWriteRequest *request, uint8_t value) {
  SetState((value == 0), "HAP");
  HAPAccessoryServerRaiseEvent(server_, tgt_state_notify_char_, &svc_,
                               accessory_);
  (void) server;
  (void) request;
  return kHAPError_None;
}

}  // namespace shelly