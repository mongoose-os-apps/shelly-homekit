
#include "custom16_pcf857x_input.hpp"

#include "mgos.hpp"
#include "mgos_pcf857x.h"

namespace shelly {
namespace custom16 {


InputPCF857xPin::InputPCF857xPin(int id, struct mgos_pcf857x *d, int pin, int on_value, enum mgos_gpio_pull_type pull,
                   bool enable_reset)
    : InputPCF857xPin(id, d, {.pin = pin,
                    .on_value = on_value,
                    .pull = pull,
                    .enable_reset = enable_reset,
                    .short_press_duration_ms = kDefaultShortPressDurationMs,
                    .long_press_duration_ms = kDefaultLongPressDurationMs}) {
}

InputPCF857xPin::InputPCF857xPin(int id, struct mgos_pcf857x *d, const Config &cfg)
    : Input(id), cfg_(cfg), d_(d), timer_(std::bind(&InputPCF857xPin::HandleTimer, this)) {
}

void InputPCF857xPin::Init() {
  mgos_pcf857x_gpio_setup_input(d_, cfg_.pin, cfg_.pull);
  mgos_pcf857x_gpio_set_button_handler(d_, cfg_.pin, cfg_.pull, MGOS_GPIO_INT_EDGE_ANY, 20,
                               GPIOIntHandler, this);
  bool state = GetState();
  LOG(LL_INFO, ("InputPCF857xPin %d: pin %d, on_value %d, state %s", id(), cfg_.pin,
                cfg_.on_value, OnOff(state)));
}

void InputPCF857xPin::SetInvert(bool invert) {
  invert_ = invert;
  GetState();
}

InputPCF857xPin::~InputPCF857xPin() {
  mgos_pcf857x_gpio_remove_int_handler(d_, cfg_.pin, nullptr, nullptr);
}

bool InputPCF857xPin::ReadPin() {
  return mgos_pcf857x_gpio_read(d_, cfg_.pin);
}

bool InputPCF857xPin::GetState() {
  last_state_ = (ReadPin() == cfg_.on_value) ^ invert_;
  return last_state_;
}

// static
void InputPCF857xPin::GPIOIntHandler(int pin, void *arg) {
  static_cast<InputPCF857xPin *>(arg)->HandleGPIOInt();
  (void) pin;
}

void InputPCF857xPin::DetectReset(double now, bool cur_state) {
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

void InputPCF857xPin::HandleGPIOInt() {
  bool last_state = last_state_;
  bool cur_state = GetState();
  if (cur_state == last_state) return;  // Noise
  LOG(LL_DEBUG, ("Input %d: %s (%d), st %d", id(), OnOff(cur_state),
                 mgos_pcf857x_gpio_read(d_, cfg_.pin), (int) state_));
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

void InputPCF857xPin::HandleTimer() {
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
}  // namespace custom16
}  // namespace shelly
