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

#include "shelly_input_pin.hpp"

#include "mgos.hpp"

namespace shelly {

InputPin::InputPin(int id, int pin, int on_value, enum mgos_gpio_pull_type pull,
                   bool enable_reset)
    : InputPin(id, {.pin = pin,
                    .on_value = on_value,
                    .pull = pull,
                    .enable_reset = enable_reset,
                    .short_press_duration_ms = kDefaultShortPressDurationMs,
                    .long_press_duration_ms = kDefaultLongPressDurationMs}) {
}

InputPin::InputPin(int id, const Config &cfg)
    : Input(id), cfg_(cfg), timer_(std::bind(&InputPin::HandleTimer, this)) {
}

void InputPin::Init() {
  mgos_gpio_setup_input(cfg_.pin, cfg_.pull);
  mgos_gpio_set_button_handler(cfg_.pin, cfg_.pull, MGOS_GPIO_INT_EDGE_ANY, 20,
                               GPIOIntHandler, this);
  bool state = GetState();
  LOG(LL_INFO, ("%s %d: pin %d, on_value %d, state %s", "InputPin", id(),
                cfg_.pin, cfg_.on_value, OnOff(state)));
}

void InputPin::SetInvert(bool invert) {
  invert_ = invert;
  GetState();
}

InputPin::~InputPin() {
  mgos_gpio_remove_int_handler(cfg_.pin, nullptr, nullptr);
}

bool InputPin::ReadPin() {
  return mgos_gpio_read(cfg_.pin);
}

bool InputPin::GetState() {
  last_state_ = (ReadPin() == cfg_.on_value) ^ invert_;
  return last_state_;
}

// static
void InputPin::GPIOIntHandler(int pin, void *arg) {
  static_cast<InputPin *>(arg)->HandleGPIOInt();
  (void) pin;
}

void InputPin::DetectReset(double now, bool cur_state) {
  if (cfg_.enable_reset && now < 30) {
    if (now - last_change_ts_ > 5) {
      change_cnt_ = 0;
    }
    change_cnt_++;
    if (change_cnt_ >= 10) {
      change_cnt_ = 0;
      CallHandlers(Event::kReset, cur_state);
    }
  }
}

void InputPin::HandleGPIOInt() {
  bool last_state = last_state_;
  bool cur_state = GetState();
  if (cur_state == last_state) return;  // Noise
  LOG(LL_DEBUG, ("Input %d: %s (%d), st %d", id(), OnOff(cur_state),
                 mgos_gpio_read(cfg_.pin), (int) state_));
  CallHandlers(Event::kChange, cur_state);
  double now = mgos_uptime();
  DetectReset(now, cur_state);
  switch (state_) {
    case State::kIdle:
      if (cur_state) {
        timer_.Reset(cfg_.short_press_duration_ms, 0);
        state_ = State::kWaitOffSingle;
        timer_cnt_ = 0;
      }
      break;
    case State::kWaitOffSingle:
      if (!cur_state) {
        state_ = State::kWaitOnDouble;
      }
      break;
    case State::kWaitOnDouble:
      if (cur_state) {
        timer_.Reset(cfg_.short_press_duration_ms, 0);
        state_ = State::kWaitOffDouble;
        timer_cnt_ = 0;
      }
      break;
    case State::kWaitOffDouble:
      if (!cur_state) {
        timer_.Clear();
        CallHandlers(Event::kDouble, cur_state);
        state_ = State::kIdle;
      }
      break;
    case State::kWaitOffLong:
      if (!cur_state) {
        timer_.Clear();
        if (timer_cnt_ == 1) {
          CallHandlers(Event::kSingle, cur_state);
        }
        state_ = State::kIdle;
      }
      break;
  }
  last_change_ts_ = now;
}

void InputPin::HandleTimer() {
  timer_cnt_++;
  bool cur_state = GetState();
  LOG(LL_DEBUG, ("Input %d: timer, st %d", id(), (int) state_));
  switch (state_) {
    case State::kIdle:
      break;
    case State::kWaitOffSingle:
    case State::kWaitOffDouble:
      timer_.Reset(cfg_.long_press_duration_ms - cfg_.short_press_duration_ms,
                   0);
      state_ = State::kWaitOffLong;
      break;
    case State::kWaitOnDouble:
      CallHandlers(Event::kSingle, cur_state);
      state_ = State::kIdle;
      break;
    case State::kWaitOffLong:
      if (timer_cnt_ == 2) {
        CallHandlers(Event::kLong, cur_state);
      }
      break;
  }
}

}  // namespace shelly
