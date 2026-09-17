#ifdef USE_RTEMS

#include "i2c_bus_rtems.h"
#include "esphome/components/rtems/i2c.h"
#include "esphome/core/log.h"

#include <dev/i2c/i2c.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace esphome::i2c {

static const char *const TAG = "i2c";

RTEMSI2CBus::~RTEMSI2CBus() {
  if (this->file_descriptor_ != -1) {
    ::close(this->file_descriptor_);
    this->file_descriptor_ = -1;
  }
}

void RTEMSI2CBus::setup() {
  ESP_LOGCONFIG(TAG, "Setting up I2C bus...");

  if (!rtems::register_i2c_bus(this->port_, this->device_.c_str())) {
    // register_i2c_bus() has already said which of the several reasons it was.
    this->mark_failed();
    return;
  }

  this->file_descriptor_ = ::open(this->device_.c_str(), O_RDWR);
  if (this->file_descriptor_ == -1) {
    ESP_LOGE(TAG, "Cannot open %s: %s", this->device_.c_str(), ::strerror(errno));
    this->mark_failed();
    return;
  }

  // The driver comes up at 100 kHz; anything else has to be asked for.  A
  // failure here is not fatal -- the bus still works, just not at the
  // requested speed -- so it warns and carries on.
  if (::ioctl(this->file_descriptor_, I2C_BUS_SET_CLOCK, this->frequency_) != 0) {
    ESP_LOGW(TAG, "Cannot set the bus to %" PRIu32 " Hz: %s", this->frequency_, ::strerror(errno));
  }

  if (this->scan_) {
    this->i2c_scan_();
  }
}

void RTEMSI2CBus::dump_config() {
  ESP_LOGCONFIG(TAG, "I2C Bus:");
  ESP_LOGCONFIG(TAG, "  Device: %s", this->device_.c_str());
  ESP_LOGCONFIG(TAG, "  Frequency: %" PRIu32 " Hz", this->frequency_);

  if (this->scan_) {
    ESP_LOGI(TAG, "  Scan Results:");
    for (const auto &s : this->scan_results_) {
      if (s.second) {
        ESP_LOGI(TAG, "    0x%02X: Found", s.first);
      }
    }
  }
}

ErrorCode RTEMSI2CBus::write_readv(uint8_t address, const uint8_t *write_buffer, size_t write_count,
                                   uint8_t *read_buffer, size_t read_count) {
  if (this->file_descriptor_ == -1) {
    return ERROR_NOT_INITIALIZED;
  }

  // Both counts zero is not a mistake: it is how i2c_scan_() asks whether
  // anything lives at an address.  Sent as a zero-length write, it puts the
  // address byte on the bus and nothing else, so the answer is the ACK.

  // Both parts go in one ioctl so that the driver puts a repeated start
  // between them rather than a stop.  Splitting them would release the bus in
  // the middle, which for a register read lets another master change the
  // device's pointer between the write and the read.
  i2c_msg msgs[2];
  uint32_t count = 0;

  if (write_count > 0 || read_count == 0) {
    msgs[count].addr = address;
    msgs[count].flags = 0;
    msgs[count].len = static_cast<uint16_t>(write_count);
    msgs[count].buf = const_cast<uint8_t *>(write_buffer);
    ++count;
  }
  if (read_count > 0) {
    msgs[count].addr = address;
    msgs[count].flags = I2C_M_RD;
    msgs[count].len = static_cast<uint16_t>(read_count);
    msgs[count].buf = read_buffer;
    ++count;
  }

  i2c_rdwr_ioctl_data payload;
  payload.msgs = msgs;
  payload.nmsgs = count;

  if (::ioctl(this->file_descriptor_, I2C_RDWR, &payload) == 0) {
    return ERROR_OK;
  }

  switch (errno) {
    case EIO:
      // What a NACK arrives as, which is also what a device that is simply not
      // there looks like -- so this is the ordinary answer during a bus scan,
      // not necessarily a fault.
      return ERROR_NOT_ACKNOWLEDGED;
    case ETIMEDOUT:
      return ERROR_TIMEOUT;
    case EINVAL:
      return ERROR_INVALID_ARGUMENT;
    case ENOSPC:
    case E2BIG:
      return ERROR_TOO_LARGE;
    default:
      return ERROR_UNKNOWN;
  }
}

}  // namespace esphome::i2c

#endif  // USE_RTEMS
