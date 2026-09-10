#pragma once

#ifdef USE_RTEMS

#include "esphome/core/hal.h"

namespace esphome::rtems {

/// A pin driven through RTEMS' BSP GPIO API, <bsp/gpio.h>.
///
/// Nothing here knows which chip it is running on.  Every register is reached
/// through the BSP, which is where a difference between one board and the next
/// belongs; a pin number on this platform means whatever the BSP says it
/// means.
class RTEMSGPIOPin final : public InternalGPIOPin {
 public:
  void set_pin(uint8_t pin) { pin_ = pin; }
  void set_inverted(bool inverted) { inverted_ = inverted; }
  void set_flags(gpio::Flags flags) { flags_ = flags; }

  void setup() override { pin_mode(flags_); }
  void pin_mode(gpio::Flags flags) override;
  bool digital_read() override;
  void digital_write(bool value) override;
  size_t dump_summary(char *buffer, size_t len) const override;
  void detach_interrupt() const override;
  ISRInternalGPIOPin to_isr() const override;
  uint8_t get_pin() const override { return pin_; }
  gpio::Flags get_flags() const override { return flags_; }
  bool is_inverted() const override { return inverted_; }

 protected:
  void attach_interrupt(void (*func)(void *), void *arg, gpio::InterruptType type) const override;

  uint8_t pin_;
  bool inverted_{};
  gpio::Flags flags_{};
  /// Whether this pin currently holds a request against the shared layer,
  /// which has to be released before its function can be changed.
  bool requested_{};
};

}  // namespace esphome::rtems

#endif  // USE_RTEMS
