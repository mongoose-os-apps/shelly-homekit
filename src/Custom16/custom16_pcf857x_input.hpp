
#pragma once

#include "shelly_common.hpp"
#include "shelly_input.hpp"
#include "mgos_timers.hpp"
#include "mgos_pcf857x.h"

namespace shelly {
namespace custom16 {


class InputPCF857xPin : public Input {

public:
 public:
  static constexpr int kDefaultShortPressDurationMs = 500;
  static constexpr int kDefaultLongPressDurationMs = 1000;

  struct Config {
    int pin;
    int on_value;
    enum mgos_gpio_pull_type pull;
    bool enable_reset;
    int short_press_duration_ms;
    int long_press_duration_ms;
  };

  InputPCF857xPin(int id, struct mgos_pcf857x *d, int pin, int on_value, enum mgos_gpio_pull_type pull,
           bool enable_reset);
  InputPCF857xPin(int id, struct mgos_pcf857x *d, const Config &cfg);
  virtual ~InputPCF857xPin();

  // Input interface impl.
  bool GetState() override;
  virtual void Init() override;
  void SetInvert(bool invert) override;

 protected:
  virtual bool ReadPin();
  void HandleGPIOInt();

  const Config cfg_;
  bool invert_ = false;

 private:
  struct mgos_pcf857x *d_;

  enum class State {
    kIdle = 0,
    kWaitOffSingle = 1,
    kWaitOnDouble = 2,
    kWaitOffDouble = 3,
    kWaitOffLong = 4,
  };

  static void GPIOIntHandler(int pin, void *arg);

  void DetectReset(double now, bool cur_state);

  void HandleTimer();

  bool last_state_ = false;
  int change_cnt_ = 0;         // State change counter for reset.
  double last_change_ts_ = 0;  // Timestamp of last change (uptime).

  State state_ = State::kIdle;
  int timer_cnt_ = 0;
  mgos::Timer timer_;

  InputPCF857xPin(const InputPCF857xPin &other) = delete;
};

}  // namespace custom16
}  // namespace shelly
