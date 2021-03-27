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

#include "shelly_hap_rgb.hpp"
#include "shelly_main.hpp"
#include "shelly_switch.hpp"

#include "mgos.hpp"
#include "mgos_system.hpp"

#include "mgos_hap_accessory.hpp"

#include <math.h>

namespace shelly {
namespace hap {

RGB::RGB(int id, Input *in, Output *out_r, Output *out_g, Output *out_b,
         struct mgos_config_lb *cfg)
    : Component(id),
      Service((SHELLY_HAP_IID_BASE_LIGHTING +
               (SHELLY_HAP_IID_STEP_LIGHTING * (id - 1))),
              &kHAPServiceType_LightBulb,
              kHAPServiceDebugDescription_LightBulb),
      in_(in),
      out_r_(out_r),
      out_g_(out_g),
      out_b_(out_b),
      cfg_(cfg),
      auto_off_timer_(std::bind(&RGB::AutoOffTimerCB, this)) {
}

RGB::~RGB() {
  if (in_ != nullptr) {
    in_->RemoveHandler(handler_id_);
  }
  SaveState();
}

Component::Type RGB::type() const {
  return Type::kLightBulb;
}

std::string RGB::name() const {
  return cfg_->name;
}

Status RGB::Init() {
  if (!cfg_->enable) {
    LOG(LL_INFO, ("'%s' is disabled", cfg_->name));
    return Status::OK();
  }
  if (in_ != nullptr) {
    handler_id_ =
        in_->AddHandler(std::bind(&RGB::InputEventHandler, this, _1, _2));
    in_->SetInvert(cfg_->in_inverted);
  }
  bool should_restore = (cfg_->initial_state == (int) InitialState::kLast);
  if (IsSoftReboot()) should_restore = true;
  if (should_restore) {
    SetOutputState("init");
  } else {
    switch (static_cast<InitialState>(cfg_->initial_state)) {
      case InitialState::kOff:
        cfg_->state = false;
        SetOutputState("init");
        break;
      case InitialState::kOn:
        cfg_->state = true;
        SetOutputState("init");
        break;
      case InitialState::kInput:
        if (in_ != nullptr &&
            cfg_->in_mode == static_cast<int>(InMode::kToggle)) {
          cfg_->state = in_->GetState();
          SetOutputState("init");
        }
        break;
      case InitialState::kLast:
      case InitialState::kMax:
        break;
    }
  }

  uint16_t iid = svc_.iid + 1;

  // Name
  AddNameChar(iid++, cfg_->name);
  // On
  auto *on_char = new mgos::hap::BoolCharacteristic(
      iid++, &kHAPCharacteristicType_On,
      std::bind(&RGB::HandleOnRead, this, _1, _2, _3),
      true /* supports_notification */,
      std::bind(&RGB::HandleOnWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_On);
  state_notify_chars_.push_back(on_char);
  AddChar(on_char);
  // Brightness
  auto *brightness_char = new mgos::hap::UInt8Characteristic(
      iid++, &kHAPCharacteristicType_Brightness, 0, 100, 1,
      std::bind(&RGB::HandleBrightnessRead, this, _1, _2, _3),
      true /* supports_notification */,
      std::bind(&RGB::HandleBrightnessWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_Brightness);
  state_notify_chars_.push_back(brightness_char);
  AddChar(brightness_char);
  // Hue
  auto *hue_char = new mgos::hap::UInt32Characteristic(
      iid++, &kHAPCharacteristicType_Hue, 0, 360, 1,
      std::bind(&RGB::HandleHueRead, this, _1, _2, _3),
      true /* supports_notification */,
      std::bind(&RGB::HandleHueWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_Hue);
  state_notify_chars_.push_back(hue_char);
  AddChar(hue_char);
  // Saturation
  auto *saturation_char = new mgos::hap::UInt32Characteristic(
      iid++, &kHAPCharacteristicType_Saturation, 0, 100, 1,
      std::bind(&RGB::HandleSaturationRead, this, _1, _2, _3),
      true /* supports_notification */,
      std::bind(&RGB::HandleSaturationWrite, this, _1, _2, _3),
      kHAPCharacteristicDebugDescription_Saturation);
  state_notify_chars_.push_back(saturation_char);
  AddChar(saturation_char);

  return Status::OK();
}

void RGB::HSVtoRGB(float h, float s, float v, float &r, float &g,
                   float &b) const {
  if (s == 0.0) {
    // if saturation is zero than all rgb hannels same as brightness
    r = g = b = v;
  } else {
    float h1 = fmod(h, 360.0f);  // jail hue into 0-359°
    float c = v * s;
    float h2 = h1 / 60.0f;
    float x = c * (1.0f - fmod(h2, 2.0f) - 1.0f);
    float m = v - c;

    switch (static_cast<int>(h2)) {
      case 0:
        r = c;
        g = x;
        b = 0;
        break;

      case 1:
        r = x;
        g = c;
        b = 0;
        break;

      case 2:
        r = 0;
        g = c;
        b = x;
        break;

      case 3:
        r = 0;
        g = x;
        b = c;
        break;

      case 4:
        r = x;
        g = 0;
        b = c;
        break;

      case 5:
        r = c;
        g = 0;
        b = x;
        break;
    }

    r += m;
    g += m;
    b += m;
  }
}

void RGB::SetOutputState(const char *source) {
  LOG(LL_INFO,
      ("state: %s, brightness: %i, hue: %i, saturation: %i", OnOff(cfg_->state),
       cfg_->brightness, cfg_->hue, cfg_->saturation));

  float h = cfg_->hue;
  float s = cfg_->saturation / 100.0f;
  float v = cfg_->brightness / 100.0f;

  float r = 0, g = 0, b = 0;

  HSVtoRGB(h, s, v, r, g, b);

  int on = cfg_->state != 0 ? 1 : 0;

  out_r_->SetStatePWM(r * on, source);
  out_g_->SetStatePWM(g * on, source);
  out_b_->SetStatePWM(b * on, source);

  if (cfg_->state && cfg_->auto_off) {
    auto_off_timer_.Reset(cfg_->auto_off_delay * 1000, 0);
  } else {
    auto_off_timer_.Clear();
  }

  for (auto *c : state_notify_chars_) {
    c->RaiseEvent();
  }
}

StatusOr<std::string> RGB::GetInfo() const {
  const_cast<RGB *>(this)->SaveState();
  return mgos::SPrintf("sta: %s, b: %i, h: %i, sa: %i", OnOff(cfg_->state),
                       cfg_->brightness, cfg_->hue, cfg_->saturation);
}

StatusOr<std::string> RGB::GetInfoJSON() const {
  return mgos::JSONPrintStringf(
      "{id: %d, type: %d, name: %Q, state: %B, "
      " brightness: %d, hue: %d, saturation: %d, "
      " in_inverted: %B, initial: %d, in_mode: %d, "
      "auto_off: %B, auto_off_delay: %.3f}",
      id(), type(), cfg_->name, cfg_->state, cfg_->brightness, cfg_->hue,
      cfg_->saturation, cfg_->in_inverted, cfg_->initial_state, cfg_->in_mode,
      cfg_->auto_off, cfg_->auto_off_delay);
}

Status RGB::SetConfig(const std::string &config_json, bool *restart_required) {
  struct mgos_config_lb cfg = *cfg_;
  int8_t in_inverted = -1;
  cfg.name = nullptr;
  cfg.in_mode = -2;
  json_scanf(config_json.c_str(), config_json.size(),
             "{name: %Q, in_mode: %d, in_inverted: %B, "
             "initial_state: %d, "
             "auto_off: %B, auto_off_delay: %lf}",
             &cfg.name, &cfg.in_mode, &in_inverted, &cfg.initial_state,
             &cfg.auto_off, &cfg.auto_off_delay);
  mgos::ScopedCPtr name_owner((void *) cfg.name);
  // Validation.
  if (cfg.name != nullptr && strlen(cfg.name) > 64) {
    return mgos::Errorf(STATUS_INVALID_ARGUMENT, "invalid %s",
                        "name (too long, max 64)");
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
  // Now copy over.
  if (cfg_->name != nullptr && strcmp(cfg_->name, cfg.name) != 0) {
    mgos_conf_set_str(&cfg_->name, cfg.name);
    *restart_required = true;
  }
  if (cfg.in_mode != -2 && cfg_->in_mode != cfg.in_mode) {
    if (cfg_->in_mode == (int) InMode::kDetached ||
        cfg.in_mode == (int) InMode::kDetached) {
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
  return Status::OK();
}

void RGB::SaveState() {
  if (!dirty_) return;
  mgos_sys_config_save(&mgos_sys_config, false /* try_once */, NULL /* msg */);
  dirty_ = false;
}

Status RGB::SetState(const std::string &state_json) {
  bool state;
  int brightness, hue, saturation;
  json_scanf(state_json.c_str(), state_json.size(),
             "{state: %B, brightness: %d, hue: %d, saturation: %d}", &state,
             &brightness, &hue, &saturation);

  if (cfg_->state != state) {
    cfg_->state = state;
    dirty_ = true;
  }
  if (cfg_->brightness != brightness) {
    cfg_->brightness = brightness;
    dirty_ = true;
  }
  if (cfg_->hue != hue) {
    cfg_->hue = hue;
    dirty_ = true;
  }
  if (cfg_->saturation != saturation) {
    cfg_->saturation = saturation;
    dirty_ = true;
  }

  if (dirty_) {
    SetOutputState("RPC");
  }

  return Status::OK();
}

void RGB::AutoOffTimerCB() {
  // Don't set state if auto off has been disabled during timer run
  if (!cfg_->auto_off) return;
  if (static_cast<InMode>(cfg_->in_mode) == InMode::kActivation &&
      in_ != nullptr && in_->GetState() && cfg_->state) {
    // Input is active, re-arm.
    LOG(LL_INFO, ("Input is active, re-arming auto off timer"));
    auto_off_timer_.Reset(cfg_->auto_off_delay * 1000, 0);
    return;
  }
  cfg_->state = false;
  SetOutputState("auto_off");
}

void RGB::InputEventHandler(Input::Event ev, bool state) {
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
            cfg_->state = !cfg_->state;
            SetOutputState("ext_mom");
          }
          break;
        case InMode::kToggle:
          cfg_->state = state;
          SetOutputState("switch");
          break;
        case InMode::kEdge:
          cfg_->state = !cfg_->state;
          SetOutputState("ext_edge");
          break;
        case InMode::kActivation:
          if (state) {
            cfg_->state = true;
            SetOutputState("ext_act");
          } else if (cfg_->state && cfg_->auto_off) {
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

HAPError RGB::HandleOnRead(HAPAccessoryServerRef *server,
                           const HAPBoolCharacteristicReadRequest *request,
                           bool *value) {
  *value = cfg_->state;
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError RGB::HandleOnWrite(HAPAccessoryServerRef *server,
                            const HAPBoolCharacteristicWriteRequest *request,
                            bool value) {
  LOG(LL_INFO, ("State %d: %s", id(), OnOff(value)));
  cfg_->state = value;
  dirty_ = true;
  SetOutputState("HAP");
  state_notify_chars_[0]->RaiseEvent();
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError RGB::HandleBrightnessRead(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicReadRequest *request, uint8_t *value) {
  LOG(LL_INFO, ("Brightness read %d: %d", id(), cfg_->brightness));
  *value = (uint8_t) cfg_->brightness;
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError RGB::HandleBrightnessWrite(
    HAPAccessoryServerRef *server,
    const HAPUInt8CharacteristicWriteRequest *request, uint8_t value) {
  LOG(LL_INFO, ("Brightness %d: %d", id(), value));
  cfg_->brightness = value;
  dirty_ = true;
  state_notify_chars_[1]->RaiseEvent();
  SetOutputState("HAP");
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError RGB::HandleHueRead(HAPAccessoryServerRef *server,
                            const HAPUInt32CharacteristicReadRequest *request,
                            uint32_t *value) {
  LOG(LL_INFO, ("HandleHueRead"));
  *value = cfg_->hue;
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError RGB::HandleHueWrite(HAPAccessoryServerRef *server,
                             const HAPUInt32CharacteristicWriteRequest *request,
                             uint32_t value) {
  LOG(LL_INFO, ("Hue %d: %i", id(), (int) value));
  if (cfg_->hue != (int) value) {
    cfg_->hue = value;
    dirty_ = true;
    state_notify_chars_[2]->RaiseEvent();
    SetOutputState("HAP");
  } else {
    LOG(LL_INFO, ("no Hue update"));
  }
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError RGB::HandleSaturationRead(
    HAPAccessoryServerRef *server,
    const HAPUInt32CharacteristicReadRequest *request, uint32_t *value) {
  LOG(LL_INFO, ("HandleSaturationRead"));
  *value = cfg_->saturation;
  (void) server;
  (void) request;
  return kHAPError_None;
}

HAPError RGB::HandleSaturationWrite(
    HAPAccessoryServerRef *server,
    const HAPUInt32CharacteristicWriteRequest *request, uint32_t value) {
  LOG(LL_INFO, ("Saturation %d: %i", id(), (int) value));
  if (cfg_->saturation != (int) value) {
    cfg_->saturation = value;
    dirty_ = true;
    state_notify_chars_[3]->RaiseEvent();
    SetOutputState("HAP");
  } else {
    LOG(LL_INFO, ("no Saturation update"));
  }
  (void) server;
  (void) request;
  return kHAPError_None;
}

}  // namespace hap
}  // namespace shelly
