
#pragma once

#include "shelly_common.hpp"
#include "shelly_output.hpp"
#include "mgos_pcf857x.h"

namespace shelly {
namespace custom16 {


class OutputPCF857xPin : public Output {

public:
  OutputPCF857xPin(int id, struct mgos_pcf857x *d, int pin, int on_value);
  virtual ~OutputPCF857xPin();

  // Output interface impl.
  bool GetState() override;
  Status SetState(bool on, const char *source) override;
  Status SetStatePWM(float duty, const char *source) override;
  Status Pulse(bool on, int duration_ms, const char *source) override;
  int pin() const;
  void SetInvert(bool out_invert) override;

 protected:
  bool out_invert_ = false;

 private:
  struct mgos_pcf857x *d_;

  const int pin_;
  const int on_value_;

  OutputPCF857xPin(const OutputPCF857xPin &other) = delete;
};

}  // namespace custom16
}  // namespace shelly
