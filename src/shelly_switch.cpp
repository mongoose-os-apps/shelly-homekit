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

#include "shelly_switch.hpp"

#include "mgos.hpp"
#include "mgos_hap_accessory.hpp"
#include "mgos_hap_chars.hpp"

#include "shelly_main.hpp"

namespace shelly {

// Inofficial HK Chars defined by Eve
// https://gist.github.com/gomfunkel/b1a046d729757120907c#elgato-eve-energy-firmware-revision-131466

// Eve power consumption (W)
// E863F10D-079E-48FF-8F27-9C2605A29F52
const HAPUUID kHAPCharacteristic_EveConsumption = {
    0x52, 0x9F, 0xA2, 0x05, 0x26, 0x9C, 0x27, 0x8F,
    0xFF, 0x48, 0x9E, 0x07, 0x0D, 0xF1, 0x63, 0xE8,
};

// Eve total power consumption (kWh)
// E863F10C-079E-48FF-8F27-9C2605A29F52
const HAPUUID kHAPCharacteristic_EveTotalConsumption = {
    0x52, 0x9F, 0xA2, 0x05, 0x26, 0x9C, 0x27, 0x8F,
    0xFF, 0x48, 0x9E, 0x07, 0x0C, 0xF1, 0x63, 0xE8,
};

ShellySwitch::ShellySwitch(int id, Input *in, Output *out, PowerMeter *out_pm,
                           Output *led_out, struct mgos_config_sw *cfg)
    : Component(id),
      ins_(in ? std::vector<Input *>{in} : std::vector<Input *>{}),
      out_(out),
      led_out_(led_out),
      out_pm_(out_pm),
      cfg_(cfg),
      auto_off_timer_(std::bind(&ShellySwitch::AutoOffTimerCB, this)),
      power_timer_(std::bind(&ShellySwitch::PowerMeterTimerCB, this)) {
}

ShellySwitch::~ShellySwitch() {
  for (size_t i = 0; i < in_handler_ids_.size(); i++) {
    ins_[i]->RemoveHandler(in_handler_ids_[i]);
  }
  SaveState();
}

Component::Type ShellySwitch::type() const {
  return Type::kSwitch;
}

std::string ShellySwitch::name() const {
  return cfg_->name;
}

StatusOr<std::string> ShellySwitch::GetInfo() const {
  int in_st = -1;
  if (!ins_.empty()) in_st = GetInputState();
  const_cast<ShellySwitch *>(this)->SaveState();
  return mgos::SPrintf("st:%d in_st:%d inm:%d ininv:%d", out_->GetState(),
                       in_st, cfg_->in_mode, cfg_->in_inverted);
}

StatusOr<std::string> ShellySwitch::GetInfoJSON() const {
  const bool hdim = (SHELLY_HAVE_DUAL_INPUT_MODES ? true : false);
  std::string res = mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, svc_type: %d, valve_type: %d, in_mode: %d, "
      "in_inverted: %B, initial: %d, state: %B, auto_off: %B, "
      "auto_off_delay: %.3f, state_led_en: %d, out_inverted: %B, hdim: %B",
      id(), type(), (cfg_->name ? cfg_->name : ""), cfg_->svc_type,
      cfg_->valve_type, cfg_->in_mode, cfg_->in_inverted, cfg_->initial_state,
      out_->GetState(), cfg_->auto_off, cfg_->auto_off_delay,
      cfg_->state_led_en, cfg_->out_inverted, hdim);
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

Status ShellySwitch::SetConfig(const std::string &config_json,
                               bool *restart_required) {
  struct mgos_config_sw cfg = *cfg_;
  int8_t in_inverted = -1;
  cfg.name = nullptr;
  cfg.in_mode = -2;
  json_scanf(
      config_json.c_str(), config_json.size(),
      "{name: %Q, svc_type: %d, valve_type: %d, in_mode: %d, in_inverted: %B, "
      "initial_state: %d, "
      "auto_off: %B, auto_off_delay: %lf, state_led_en: %d, out_inverted: %B}",
      &cfg.name, &cfg.svc_type, &cfg.valve_type, &cfg.in_mode, &in_inverted,
      &cfg.initial_state, &cfg.auto_off, &cfg.auto_off_delay, &cfg.state_led_en,
      &cfg.out_inverted);
  mgos::ScopedCPtr name_owner((void *) cfg.name);
  // Validation.
  if (cfg.name != nullptr && strlen(cfg.name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
  }
  if (cfg.svc_type < -1 || cfg.svc_type > 3) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "svc_type");
  }
  if ((cfg.svc_type != 3 && cfg.valve_type != -1) ||
      (cfg.svc_type == 3 && cfg.valve_type < 0) ||
      (cfg.svc_type == 3 && cfg.valve_type > 1)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "valve_type");
  }
  if (cfg.in_mode != -2 &&
      (cfg.in_mode < 0 || cfg.in_mode >= (int) InMode::kMax)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "in_mode");
  }
  if (cfg.initial_state < 0 || cfg.initial_state >= (int) InitialState::kMax ||
      (cfg_->in_mode == -1 &&
       cfg.initial_state == (int) InitialState::kInput)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "initial_state");
  }
  cfg.auto_off = (cfg.auto_off != 0);
  if (cfg.initial_state < 0 || cfg.initial_state > (int) InitialState::kMax) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "initial_state");
  }
  if ((cfg_->state_led_en == -1 && cfg.state_led_en != -1) ||
      (cfg_->state_led_en != -1 && cfg.state_led_en != 0 &&
       cfg.state_led_en != 1)) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s", "state_led_en");
  }
  // Now copy over.
  if (cfg_->name != nullptr && strcmp(cfg_->name, cfg.name) != 0) {
    mgos_conf_set_str(&cfg_->name, cfg.name);
    *restart_required = true;
  }
  if (cfg_->svc_type != cfg.svc_type) {
    cfg_->svc_type = cfg.svc_type;
    *restart_required = true;
  }
  if (cfg_->valve_type != cfg.valve_type) {
    cfg_->valve_type = cfg.valve_type;
    *restart_required = true;
  }
  if (cfg.in_mode != -2 && cfg_->in_mode != cfg.in_mode) {
    if (cfg_->in_mode == (int) InMode::kDetached ||
        cfg.in_mode == (int) InMode::kDetached ||
#if SHELLY_HAVE_DUAL_INPUT_MODES
        cfg.in_mode == (int) InMode::kEdgeBoth ||
        cfg_->in_mode == (int) InMode::kEdgeBoth ||
        cfg.in_mode == (int) InMode::kActivationBoth ||
        cfg_->in_mode == (int) InMode::kActivationBoth ||
#endif
        false) {
      *restart_required = true;
    }
    cfg_->in_mode = cfg.in_mode;
  }
  if (in_inverted != -1 && cfg_->in_inverted != in_inverted) {
    cfg_->in_inverted = in_inverted;
    *restart_required = true;
  }
  cfg_->initial_state = cfg.initial_state;
  cfg_->auto_off = cfg.auto_off;
  cfg_->auto_off_delay = cfg.auto_off_delay;
  if (cfg_->state_led_en != cfg.state_led_en) {
    cfg_->state_led_en = cfg.state_led_en;
    *restart_required = true;
  }
  if (cfg_->out_inverted != cfg.out_inverted) {
    cfg_->out_inverted = cfg.out_inverted;
    *restart_required = true;
  }
  return Status::OK();
}

Status ShellySwitch::SetState(const std::string &state_json) {
  int8_t state = -1;
  json_scanf(state_json.c_str(), state_json.size(), "{state: %B}", &state);
  if (state < 0) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "%s is required", "state");
  }
  SetOutputState(state, "RPC");
  return Status::OK();
}

bool ShellySwitch::IsIdle() {
  return !auto_off_timer_.IsValid();
}

Status ShellySwitch::Init() {
  if (!cfg_->enable) {
    LOG(LL_INFO, ("'%s' is disabled", cfg_->name));
    return Status::OK();
  }
  for (Input *in : ins_) {
    auto handler_id = in->AddHandler(
        std::bind(&ShellySwitch::InputEventHandler, this, _1, _2));
    in->SetInvert(cfg_->in_inverted);
    in_handler_ids_.push_back(handler_id);
  }
  out_->SetInvert(cfg_->out_inverted);
  bool should_restore = (cfg_->initial_state == (int) InitialState::kLast);
  if (IsSoftReboot()) should_restore = true;
  if (should_restore) {
    SetOutputState(cfg_->state, "init");
  } else {
    switch (static_cast<InitialState>(cfg_->initial_state)) {
      case InitialState::kOff:
        SetOutputState(false, "init");
        break;
      case InitialState::kOn:
        SetOutputState(true, "init");
        break;
      case InitialState::kInput:
        if (!ins_.empty() &&
            cfg_->in_mode == static_cast<int>(InMode::kToggle)) {
          SetOutputState(GetInputState(), "init");
        }
        break;
      case InitialState::kLast:
      case InitialState::kMax:
        break;
    }
  }
  LOG(LL_INFO, ("Exporting '%s': type %d, state: %d", cfg_->name,
                cfg_->svc_type, out_->GetState()));
  return Status::OK();
}

bool ShellySwitch::GetOutputState() const {
  return out_->GetState();
}

void ShellySwitch::SetOutputState(bool new_state, const char *source) {
  bool cur_state = out_->GetState();
  out_->SetState(new_state, source);
  if (led_out_ != nullptr) {
    led_out_->SetState((cfg_->state_led_en == 1 && new_state), source);
  }
  if (cfg_->state != new_state) {
    cfg_->state = new_state;
    dirty_ = true;
  }

  if (new_state && cfg_->auto_off) {
    auto_off_timer_.Reset(cfg_->auto_off_delay * 1000, 0);
  } else {
    auto_off_timer_.Clear();
  }

  if (new_state == cur_state) return;

  for (auto *c : state_notify_chars_) {
    c->RaiseEvent();
  }
}

void ShellySwitch::AutoOffTimerCB() {
  // Don't set state if auto off has been disabled during timer run
  if (!cfg_->auto_off) return;
  if (static_cast<InMode>(cfg_->in_mode) == InMode::kActivation &&
      !ins_.empty() && GetInputState() && GetOutputState()) {
    // Input is active, re-arm.
    LOG(LL_INFO, ("Input is active, re-arming auto off timer"));
    auto_off_timer_.Reset(cfg_->auto_off_delay * 1000, 0);
    return;
  }
  SetOutputState(false, "auto_off");
}

void ShellySwitch::SaveState() {
  if (!dirty_) return;
  mgos_sys_config_save(&mgos_sys_config, false /* try_once */, NULL /* msg */);
  dirty_ = false;
}

void ShellySwitch::AddInput(Input *in) {
  auto handler_id =
      in->AddHandler(std::bind(&ShellySwitch::InputEventHandler, this, _1, _2));
  in->SetInvert(cfg_->in_inverted);
  ins_.push_back(in);
  in_handler_ids_.push_back(handler_id);
}

bool ShellySwitch::GetInputState() const {
  for (Input *in : ins_) {
    if (in->GetState()) return true;
  }
  return false;
}

void ShellySwitch::InputEventHandler(Input::Event ev, bool state) {
  InMode in_mode = static_cast<InMode>(cfg_->in_mode);
  if (in_mode == InMode::kDetached) {
    // Nothing to do
    return;
  }
  switch (ev) {
    case Input::Event::kChange: {
      switch (static_cast<InMode>(cfg_->in_mode)) {
        case InMode::kMomentary:
          if (state) {  // Only on 0 -> 1 transitions.
            SetOutputState(!out_->GetState(), "ext_mom");
          }
          break;
        case InMode::kToggle:
          SetOutputState(state, "switch");
          break;
        case InMode::kEdge:
#if SHELLY_HAVE_DUAL_INPUT_MODES
        case InMode::kEdgeBoth:
#endif
          SetOutputState(!out_->GetState(), "ext_edge");
          break;
        case InMode::kActivation:
#if SHELLY_HAVE_DUAL_INPUT_MODES
        case InMode::kActivationBoth:
#endif
          if (state) {
            SetOutputState(true, "ext_act");
          } else if (GetOutputState() && cfg_->auto_off) {
            // On 1 -> 0 transitions do not turn on output
            // but re-arm auto off timer if running.
            auto_off_timer_.Reset(cfg_->auto_off_delay * 1000, 0);
          }
          break;
        case InMode::kAbsent:
        case InMode::kDetached:
        case InMode::kMax:
          break;
      }
      break;
    }
    case Input::Event::kLong:
      // Disable auto-off if it was active.
      if (in_mode == InMode::kMomentary) {
        auto_off_timer_.Clear();
      }
      break;
    case Input::Event::kSingle:
    case Input::Event::kDouble:
    case Input::Event::kReset:
    case Input::Event::kMax:
      break;
  }
}

void ShellySwitch::AddPowerMeter(uint16_t *iid) {
  if (out_pm_ == nullptr) return;

  // Power
  power_char_ = new mgos::hap::UInt16Characteristic(
      (*iid)++, &kHAPCharacteristic_EveConsumption, 0, 65535, 1,
      [this](HAPAccessoryServerRef *,
             const HAPUInt16CharacteristicReadRequest *, uint16_t *value) {
        auto power = out_pm_->GetPowerW();
        if (!power.ok()) return kHAPError_Busy;
        *value = power.ValueOrDie();
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr, "eve-power-consumption");
  AddChar(power_char_);
  // Energy
  total_power_char_ = new mgos::hap::UInt16Characteristic(
      (*iid)++, &kHAPCharacteristic_EveTotalConsumption, 0, 65535, 1,
      [this](HAPAccessoryServerRef *,
             const HAPUInt16CharacteristicReadRequest *, uint16_t *value) {
        auto energy = out_pm_->GetEnergyWH();
        if (!energy.ok()) return kHAPError_Busy;
        *value = energy.ValueOrDie() / 1000.0f;
        return kHAPError_None;
      },
      true /* supports_notification */, nullptr, "eve-total-power-consumption");
  AddChar(total_power_char_);

  // Fuzz the interval a little bit to avoid many devices reporting at once.
  power_timer_.Reset(mgos_rand_range(4500, 5500), MGOS_TIMER_REPEAT);
}

void ShellySwitch::PowerMeterTimerCB() {
  auto current_power = out_pm_->GetPowerW();
  auto current_total_power = out_pm_->GetEnergyWH();

  if (current_power.ok() && current_power.ValueOrDie() != last_power_) {
    last_power_ = current_power.ValueOrDie();
    power_char_->RaiseEvent();
  }
  if (current_total_power.ok() &&
      current_total_power.ValueOrDie() != last_total_power_) {
    last_total_power_ = current_total_power.ValueOrDie();
    total_power_char_->RaiseEvent();
  }
}

}  // namespace shelly
