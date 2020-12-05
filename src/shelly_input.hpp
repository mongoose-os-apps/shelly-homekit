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

#pragma once

#include <vector>

#include "shelly_common.hpp"

namespace shelly {

class Input {
 public:
  enum class Event {
    kChange = 0,
    kSingle = 1,
    kDouble = 2,
    kLong = 3,
    kReset = 4,
    kMax,
  };
  explicit Input(int id);
  virtual ~Input();

  static const char *EventName(Event ev);

  int id() const;
  virtual void Init() = 0;
  virtual bool GetState() = 0;
  virtual void SetInvert(bool invert) = 0;

  typedef int HandlerID;
  static constexpr HandlerID kInvalidHandlerID = -1;
  typedef std::function<void(Event ev, bool state)> HandlerFn;
  HandlerID AddHandler(HandlerFn h);
  void RemoveHandler(HandlerID hi);

  void InjectEvent(Event ev, bool state);

 protected:
  void CallHandlers(Event ev, bool state, bool injected = false);

 private:
  const int id_;
  std::vector<HandlerFn> handlers_;

  Input(const Input &other) = delete;
};

}  // namespace shelly
