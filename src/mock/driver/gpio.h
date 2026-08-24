// Mock driver/gpio.h for Ubuntu build
#pragma once

typedef int gpio_num_t;

static inline int gpio_hold_en(gpio_num_t gpio_num) {
  (void) gpio_num;
  return 0;
}

static inline int gpio_hold_dis(gpio_num_t gpio_num) {
  (void) gpio_num;
  return 0;
}
