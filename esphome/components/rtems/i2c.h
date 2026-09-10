#pragma once

#ifdef USE_RTEMS

#include <cstdint>

namespace esphome::rtems {

/// Bring up an I2C controller and make it available at @a path.
///
/// RTEMS' I2C framework is a POSIX one: a bus is a device file, and everything
/// above it is `open`, `ioctl` and `close` with no knowledge of the hardware.
/// What that framework does not have is a convention for *creating* the device
/// file -- unlike the console, an I2C bus is registered by whoever wants it,
/// with a BSP-specific call.
///
/// So this is the one place that call belongs.  `components/i2c` opens a path
/// and nothing more; which controller that path names, and how it is brought
/// up, is the platform's business.
///
/// @return true if @a path can now be opened.
bool register_i2c_bus(int port, const char *path);

}  // namespace esphome::rtems

#endif  // USE_RTEMS
