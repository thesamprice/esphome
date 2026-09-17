#pragma once

#ifdef USE_RTEMS

#include "esphome/core/component.h"
#include "i2c_bus.h"

#include <string>

namespace esphome::i2c {

/// An I2C bus reached through RTEMS' <dev/i2c/i2c.h> framework.
///
/// That framework is deliberately Linux-compatible -- the same `i2c_msg`, the
/// same `I2C_RDWR` ioctl -- so this is close to the host bus and for the same
/// reason: neither knows anything about the controller underneath.  The
/// difference is that on RTEMS the device file does not already exist, so
/// setup() asks the platform to create it first.
class RTEMSI2CBus final : public InternalI2CBus, public Component {
 public:
  ~RTEMSI2CBus() override;

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  ErrorCode write_readv(uint8_t address, const uint8_t *write_buffer, size_t write_count, uint8_t *read_buffer,
                        size_t read_count) override;

  int get_port() const override { return this->port_; }

  void set_port(int port) { this->port_ = port; }
  void set_device(const std::string &device) { this->device_ = device; }
  void set_scan(bool scan) { this->scan_ = scan; }
  void set_frequency(uint32_t frequency) { this->frequency_ = frequency; }

 protected:
  std::string device_;
  uint32_t frequency_{100000};
  int port_{0};
  int file_descriptor_{-1};
};

}  // namespace esphome::i2c

#endif  // USE_RTEMS
