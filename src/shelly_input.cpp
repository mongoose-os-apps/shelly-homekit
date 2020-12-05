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
    case Event::kMax:
      break;
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

void Input::InjectEvent(Event ev, bool state) {
  CallHandlers(ev, state, true /* injected */);
}

void Input::CallHandlers(Event ev, bool state, bool injected) {
  LOG(LL_INFO, ("Input %d: %s (state %d)%s", id(), EventName(ev), state,
                (injected ? " [injected]" : "")));
  for (auto &h : handlers_) {
    if (h != nullptr) h(ev, state);
  }
}

}  // namespace shelly
