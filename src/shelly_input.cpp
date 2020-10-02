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

#include "shelly_input.hpp"

#include "mgos.hpp"

namespace shelly {

// static
constexpr Input::HandlerID Input::kInvalidHandlerID;

Input::Input(int id) : id_(id) {
}

Input::~Input() {
}

// static
const char *Input::EventName(Event ev) {
  switch (ev) {
    case Event::kChange:
      return "change";
    case Event::kSingle:
      return "single";
    case Event::kDouble:
      return "double";
    case Event::kLong:
      return "long";
    case Event::kReset:
      return "reset";
  }
  return "";
}

int Input::id() const {
  return id_;
}

Input::HandlerID Input::AddHandler(HandlerFn h) {
  int i;
  for (i = 0; i < (int) handlers_.size(); i++) {
    if (handlers_[i] == nullptr) {
      handlers_[i] = h;
      return i;
    }
  }
  handlers_.push_back(h);
  return i;
}

void Input::RemoveHandler(HandlerID hi) {
  if (hi < 0) return;
  handlers_[hi] = nullptr;
}

void Input::CallHandlers(Event ev, bool state) {
  LOG(LL_INFO, ("Input %d: %s (state %d)", id(), EventName(ev), state));
  for (auto &h : handlers_) {
    h(ev, state);
  }
}

InputPin::InputPin(int id, int pin, int on_value, enum mgos_gpio_pull_type pull,
                   bool enable_reset)
    : Input(id), pin_(pin), on_value_(on_value), enable_reset_(enable_reset) {
  mgos_gpio_setup_input(pin_, pull);
  mgos_gpio_set_button_handler(pin_, pull, MGOS_GPIO_INT_EDGE_ANY, 20,
                               GPIOIntHandler, this);
}

InputPin::~InputPin() {
  mgos_gpio_remove_int_handler(pin_, nullptr, nullptr);
  ClearTimer();
}

bool InputPin::GetState() {
  return (mgos_gpio_read(pin_) == on_value_);
}

// static
void InputPin::GPIOIntHandler(int pin, void *arg) {
  static_cast<InputPin *>(arg)->HandleGPIOInt();
  (void) pin;
}

// static
void InputPin::TimerCB(void *arg) {
  static_cast<InputPin *>(arg)->HandleTimer();
}

void InputPin::SetTimer(int ms) {
  ClearTimer();
  timer_id_ = mgos_set_timer(ms, 0, &InputPin::TimerCB, this);
}

void InputPin::ClearTimer() {
  mgos_clear_timer(timer_id_);
  timer_id_ = MGOS_INVALID_TIMER_ID;
}

void InputPin::DetectReset(double now, bool cur_state) {
  if (enable_reset_ && now < 30) {
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
  bool cur_state = GetState();
  LOG(LL_DEBUG, ("Input %d: %s (%d), st %d", id(), OnOff(cur_state),
                 mgos_gpio_read(pin_), (int) state_));
  CallHandlers(Event::kChange, cur_state);
  double now = mgos_uptime();
  DetectReset(now, cur_state);
  switch (state_) {
    case State::kIdle:
      if (cur_state) {
        SetTimer(kLongPressDurationMs / 2);
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
        SetTimer(kLongPressDurationMs / 2);
        state_ = State::kWaitOffDouble;
        timer_cnt_ = 0;
      }
      break;
    case State::kWaitOffDouble:
      if (!cur_state) {
        ClearTimer();
        CallHandlers(Event::kDouble, cur_state);
        state_ = State::kIdle;
      }
      break;
    case State::kWaitOffLong:
      if (!cur_state) {
        ClearTimer();
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
  timer_id_ = MGOS_INVALID_TIMER_ID;
  timer_cnt_++;
  bool cur_state = GetState();
  LOG(LL_DEBUG, ("Input %d: timer, st %d", id(), (int) state_));
  switch (state_) {
    case State::kIdle:
      break;
    case State::kWaitOffSingle:
    case State::kWaitOffDouble:
      SetTimer(kLongPressDurationMs / 2);
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
