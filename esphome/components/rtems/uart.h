#pragma once

#ifdef USE_RTEMS

#include <cstddef>
#include <cstdint>

namespace esphome::rtems {

/// Bring up a serial port and make it available at @a path.
///
/// The same shape as register_i2c_bus(), and for the same reason: RTEMS'
/// termios layer gives a serial port that is a device file and nothing more,
/// but has no convention for *creating* one that is not the console. Each BSP
/// names that call after its own chip, so this is the one place it belongs.
///
/// @return true if @a path can now be opened.
bool register_uart(int port, const char *path, uint32_t baud, size_t rx_buffer_size);

}  // namespace esphome::rtems

#endif  // USE_RTEMS
