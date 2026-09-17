#pragma once

#ifdef USE_RTEMS

#include "esphome/core/component.h"
#include "uart_component.h"

#include <string>

namespace esphome::uart {

/// A serial port reached through RTEMS' termios layer.
///
/// Close to the host component, because RTEMS' termios is the POSIX one: the
/// port is a device file and everything here is open, read, write, tcsetattr
/// and FIONREAD, with no knowledge of the controller underneath. The
/// difference is that the device file does not already exist, so setup() asks
/// the platform to create it first.
class RTEMSUartComponent final : public UARTComponent, public Component {
 public:
  ~RTEMSUartComponent();

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  void write_array(const uint8_t *data, size_t len) override;
  bool peek_byte(uint8_t *data) override;
  bool read_array(uint8_t *data, size_t len) override;
  size_t available() override;
  UARTFlushResult flush() override;

  void set_port(int port) { this->port_ = port; }
  void set_device(const std::string &device) { this->device_ = device; }

 protected:
  void check_logger_conflict() override;
  bool apply_termios_();

  std::string device_;
  int port_{0};
  int file_descriptor_{-1};
  bool has_peek_{false};
  uint8_t peek_byte_{0};
};

}  // namespace esphome::uart

#endif  // USE_RTEMS
