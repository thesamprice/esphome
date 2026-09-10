#ifdef USE_RTEMS

#include "uart_component_rtems.h"
#include "esphome/components/rtems/uart.h"
#include "esphome/core/log.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace esphome::uart {

static const char *const TAG = "uart";

RTEMSUartComponent::~RTEMSUartComponent() {
  if (this->file_descriptor_ != -1) {
    ::close(this->file_descriptor_);
    this->file_descriptor_ = -1;
  }
}

/// Map the configured baud rate onto the speed_t termios wants.  RTEMS accepts
/// only the standard rates through cfsetspeed, so an unusual one has to be
/// refused here rather than silently rounded to whatever B-constant is nearest.
static bool baud_to_speed(uint32_t baud, speed_t *out) {
  switch (baud) {
    case 1200: *out = B1200; return true;
    case 2400: *out = B2400; return true;
    case 4800: *out = B4800; return true;
    case 9600: *out = B9600; return true;
    case 19200: *out = B19200; return true;
    case 38400: *out = B38400; return true;
    case 57600: *out = B57600; return true;
    case 115200: *out = B115200; return true;
    case 230400: *out = B230400; return true;
    case 460800: *out = B460800; return true;
    case 921600: *out = B921600; return true;
    default: return false;
  }
}

bool RTEMSUartComponent::apply_termios_() {
  struct termios term;
  speed_t speed;

  if (::tcgetattr(this->file_descriptor_, &term) != 0) {
    ESP_LOGE(TAG, "tcgetattr: %s", ::strerror(errno));
    return false;
  }

  // Raw, and a read that returns what is there rather than waiting for a line.
  // Anything else and bytes are mangled by the line discipline rather than on
  // the wire, which is not what a UART component wants of a serial port.
  ::cfmakeraw(&term);
  term.c_cc[VMIN] = 0;
  term.c_cc[VTIME] = 0;

  if (!baud_to_speed(this->baud_rate_, &speed)) {
    ESP_LOGE(TAG, "%" PRIu32 " baud is not one of the rates termios can express", this->baud_rate_);
    return false;
  }
  ::cfsetispeed(&term, speed);
  ::cfsetospeed(&term, speed);

  term.c_cflag &= ~CSIZE;
  switch (this->data_bits_) {
    case 5: term.c_cflag |= CS5; break;
    case 6: term.c_cflag |= CS6; break;
    case 7: term.c_cflag |= CS7; break;
    default: term.c_cflag |= CS8; break;
  }

  if (this->stop_bits_ == 2) {
    term.c_cflag |= CSTOPB;
  } else {
    term.c_cflag &= ~CSTOPB;
  }

  switch (this->parity_) {
    case UART_CONFIG_PARITY_EVEN:
      term.c_cflag |= PARENB;
      term.c_cflag &= ~PARODD;
      break;
    case UART_CONFIG_PARITY_ODD:
      term.c_cflag |= PARENB | PARODD;
      break;
    default:
      term.c_cflag &= ~(PARENB | PARODD);
      break;
  }

  if (::tcsetattr(this->file_descriptor_, TCSANOW, &term) != 0) {
    ESP_LOGE(TAG, "tcsetattr: %s", ::strerror(errno));
    return false;
  }

  return true;
}

void RTEMSUartComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up UART...");

  if (!rtems::register_uart(this->port_, this->device_.c_str(), this->baud_rate_,
                            this->rx_buffer_size_)) {
    // register_uart() has already said which of the several reasons it was.
    this->mark_failed();
    return;
  }

  this->file_descriptor_ = ::open(this->device_.c_str(), O_RDWR | O_NONBLOCK);
  if (this->file_descriptor_ == -1) {
    ESP_LOGE(TAG, "Cannot open %s: %s", this->device_.c_str(), ::strerror(errno));
    this->mark_failed();
    return;
  }

  if (!this->apply_termios_()) {
    this->mark_failed();
  }
}

void RTEMSUartComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "UART:");
  ESP_LOGCONFIG(TAG, "  Port: %d (%s)", this->port_, this->device_.c_str());
  if (this->file_descriptor_ == -1) {
    ESP_LOGCONFIG(TAG, "  Port status: Not opened");
    return;
  }
  ESP_LOGCONFIG(TAG,
                "  Port status: opened\n"
                "  Baud Rate: %" PRIu32 "\n"
                "  Data Bits: %d\n"
                "  Parity: %s\n"
                "  Stop Bits: %d",
                this->baud_rate_, this->data_bits_,
                this->parity_ == UART_CONFIG_PARITY_NONE   ? "None"
                : this->parity_ == UART_CONFIG_PARITY_EVEN ? "Even"
                                                           : "Odd",
                this->stop_bits_);
}

void RTEMSUartComponent::check_logger_conflict() {
  // Nothing to check.  On this platform the log goes to the BSP's console,
  // which is chosen when the BSP is built and is not one of the ports this
  // component can be pointed at -- so the conflict the other platforms guard
  // against cannot arise from anything ESPHome can see.
}

void RTEMSUartComponent::write_array(const uint8_t *data, size_t len) {
  if (this->file_descriptor_ == -1) {
    return;
  }

  size_t written = 0;
  while (written < len) {
    ssize_t n = ::write(this->file_descriptor_, data + written, len - written);
    if (n > 0) {
      written += static_cast<size_t>(n);
      continue;
    }
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // The port is open non-blocking so that read() does not stall the main
      // loop; a partial write still has to be finished rather than dropped.
      continue;
    }
    ESP_LOGE(TAG, "write: %s", ::strerror(errno));
    return;
  }

#ifdef USE_UART_DEBUGGER
  for (size_t i = 0; i < len; i++) {
    this->debug_callback_.call(UART_DIRECTION_TX, data[i]);
  }
#endif
}

bool RTEMSUartComponent::peek_byte(uint8_t *data) {
  if (this->file_descriptor_ == -1) {
    return false;
  }
  if (!this->has_peek_) {
    if (!this->check_read_timeout_()) {
      return false;
    }
    if (::read(this->file_descriptor_, &this->peek_byte_, 1) != 1) {
      return false;
    }
    this->has_peek_ = true;
  }
  *data = this->peek_byte_;
  return true;
}

bool RTEMSUartComponent::read_array(uint8_t *data, size_t len) {
  if (this->file_descriptor_ == -1 || len == 0) {
    return false;
  }
  if (!this->check_read_timeout_(len)) {
    return false;
  }

  size_t got = 0;
  if (this->has_peek_) {
    data[0] = this->peek_byte_;
    this->has_peek_ = false;
    got = 1;
  }

  // A non-blocking read returns what has arrived, which may be less than
  // asked for even though check_read_timeout_() has said enough is available:
  // termios hands over one buffer at a time.  Loop rather than report a short
  // read as a failure.
  while (got < len) {
    ssize_t n = ::read(this->file_descriptor_, data + got, len - got);
    if (n > 0) {
      got += static_cast<size_t>(n);
      continue;
    }
    if (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
      ESP_LOGE(TAG, "read: %s", ::strerror(errno));
      return false;
    }
    if (!this->check_read_timeout_(len - got)) {
      return false;
    }
  }

#ifdef USE_UART_DEBUGGER
  for (size_t i = 0; i < len; i++) {
    this->debug_callback_.call(UART_DIRECTION_RX, data[i]);
  }
#endif
  return true;
}

size_t RTEMSUartComponent::available() {
  if (this->file_descriptor_ == -1) {
    return 0;
  }

  int pending = 0;
  if (::ioctl(this->file_descriptor_, FIONREAD, &pending) == -1) {
    return this->has_peek_ ? 1 : 0;
  }

  size_t result = static_cast<size_t>(pending);
  if (this->has_peek_) {
    ++result;
  }
  return result;
}

UARTFlushResult RTEMSUartComponent::flush() {
  if (this->file_descriptor_ == -1) {
    return UARTFlushResult::UART_FLUSH_RESULT_ASSUMED_SUCCESS;
  }
  if (::tcdrain(this->file_descriptor_) == -1) {
    ESP_LOGW(TAG, "tcdrain: %s", ::strerror(errno));
    return UARTFlushResult::UART_FLUSH_RESULT_FAILED;
  }
  return UARTFlushResult::UART_FLUSH_RESULT_SUCCESS;
}

}  // namespace esphome::uart

#endif  // USE_RTEMS
