#ifdef USE_RTEMS

#include "uart.h"
#include "esphome/core/log.h"

#include <rtems.h>
#include <rtems/termiostypes.h>

#include <fcntl.h>
#include <unistd.h>

/*
 * The BSP-specific half of UART, and like i2c.cpp it names no chip: a BSP with
 * general-purpose serial ports installs <bsp/uart.h> and defines
 * BSP_UART_REGISTER in it as an alias for its own function, so the header's
 * presence is the feature test and the alias is the call.
 */
#if __has_include(<bsp/uart.h>)
#include <bsp/uart.h>
#endif

namespace esphome::rtems {

static const char *const TAG = "rtems.uart";

bool register_uart(int port, const char *path, uint32_t baud, size_t rx_buffer_size) {
  // Already there, registered by an earlier port in the same configuration or
  // by the application.  Registering twice fails, so ask first.
  int fd = ::open(path, O_RDWR);
  if (fd >= 0) {
    ::close(fd);
    return true;
  }

  /*
   * Termios, not the driver, is what holds received bytes for the application,
   * and its default is 256.  A reply longer than that arriving faster than the
   * main loop reads it is silently truncated -- so rx_buffer_size has to reach
   * here, and it has to be set before the first open, which is when termios
   * allocates.
   *
   * It is a global rather than a per-port setting, which is what RTEMS offers.
   * Ports opened afterwards get the largest size any of them asked for; the
   * console, opened long before this, keeps the default.
   */
  if (rx_buffer_size > 0) {
    static size_t largest = 0;
    if (rx_buffer_size > largest) {
      largest = rx_buffer_size;
      rtems_termios_bufsize(256, largest, 256);
    }
  }

#ifdef BSP_UART_REGISTER
  if (port < 0 || port >= BSP_UART_PORT_COUNT) {
    ESP_LOGE(TAG, "This board has %d UART(s); port %d does not exist", BSP_UART_PORT_COUNT, port);
    return false;
  }

  rtems_status_code sc = BSP_UART_REGISTER(path, static_cast<unsigned>(port), baud);
  if (sc != RTEMS_SUCCESSFUL) {
    ESP_LOGE(TAG, "Cannot register %s on port %d: %s", path, port, rtems_status_text(sc));
    return false;
  }
  return true;
#else
  (void) port;
  (void) baud;
  (void) rx_buffer_size;
  ESP_LOGE(TAG, "This BSP has no UART driver, so %s cannot be created", path);
  return false;
#endif
}

}  // namespace esphome::rtems

#endif  // USE_RTEMS
