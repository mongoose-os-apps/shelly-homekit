
#include "custom16_pcf857x_output.hpp"

#include "mgos.hpp"
#include "mgos_pcf857x.h"

namespace shelly {
namespace custom16 {

OutputPCF857xPin::OutputPCF857xPin(int id, struct mgos_pcf857x *d, int pin, int on_value) 
   : Output(id),
      d_(d),
      pin_(pin),
      on_value_(on_value) {
  // TODO check d
  //mgos_pcf857x_gpio_set_mode(d_, pin_, MGOS_GPIO_MODE_OUTPUT);
  LOG(LL_INFO, ("OutputPCF857xPin %d: pin %d, on_value %d, state %s", id, pin,
                on_value, OnOff(GetState())));
}

OutputPCF857xPin::~OutputPCF857xPin() {
}

bool OutputPCF857xPin::GetState() {
  //LOG(LL_INFO, ("READ[%d]: %d, on_value: %d", pin_, mgos_pcf857x_gpio_read(d_, pin_), on_value_));
  return (mgos_pcf857x_gpio_read(d_, pin_) && on_value_ > 0) ^ out_invert_;
}

int OutputPCF857xPin::pin() const {
  return pin_;
}

Status OutputPCF857xPin::SetState(bool on, const char *source) {
  bool cur_state = GetState();
  mgos_pcf857x_gpio_write(d_, pin_, ((on ^ out_invert_) ? on_value_ : !on_value_));
  if (on == cur_state) return Status::OK();
  if (source == nullptr) source = "";
  mgos_pcf857x_print_state(d_);
  bool new_state = GetState();
  LOG(LL_INFO,
      ("Output %d: %s -> %s [%s] (%s)", id(), OnOff(cur_state), OnOff(on),  OnOff(new_state), source));
  return Status::OK();
}

Status OutputPCF857xPin::SetStatePWM(float duty, const char *source) {
  
  return Status::OK();
}

Status OutputPCF857xPin::Pulse(bool on, int duration_ms, const char *source) {
  
  return Status::OK();
}

void OutputPCF857xPin::SetInvert(bool out_invert) {
  out_invert_ = out_invert;
  GetState();
}


}  // namespace custom16
}  // namespace shelly
