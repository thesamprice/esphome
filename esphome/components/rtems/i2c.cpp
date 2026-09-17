#ifdef USE_RTEMS

#include "i2c.h"
#include "esphome/core/log.h"

#include <rtems.h>

#include <fcntl.h>
#include <unistd.h>

/*
 * The BSP-specific half of I2C, and the only file in the RTEMS backend that
 * has to care whether the board has a controller at all.
 *
 * It still names no chip.  A BSP with an I2C controller installs <bsp/i2c.h>
 * and defines BSP_I2C_REGISTER in it as an alias for whatever its own
 * registration function is called; the presence of that header is the feature
 * test, and the alias is the call.  So a second BSP needs no change here.
 *
 * A weak reference to the registration function would look like the obvious
 * way to do this and does not work: the BSP's drivers come from an archive,
 * and a weak undefined reference does not pull a member out of one.  It links
 * to null and reports "no I2C controller" on a board that has one.
 */
#if __has_include(<bsp/i2c.h>)
#include <bsp/i2c.h>
#endif

namespace esphome::rtems {

static const char *const TAG = "rtems.i2c";

bool register_i2c_bus(int port, const char *path) {
  // Already there, registered by an earlier bus in the same configuration or
  // by the application itself.  Registering twice fails, so ask first.
  int fd = ::open(path, O_RDWR);
  if (fd >= 0) {
    ::close(fd);
    return true;
  }

#ifdef BSP_I2C_REGISTER
  if (port < 0 || port >= BSP_I2C_BUS_COUNT) {
    ESP_LOGE(TAG, "This board has %d I2C controller(s); port %d does not exist", BSP_I2C_BUS_COUNT, port);
    return false;
  }

  rtems_status_code sc = BSP_I2C_REGISTER(path);
  if (sc != RTEMS_SUCCESSFUL) {
    ESP_LOGE(TAG, "Cannot register %s: %s", path, rtems_status_text(sc));
    return false;
  }
  return true;
#else
  (void) port;
  ESP_LOGE(TAG, "This BSP has no I2C controller driver, so %s cannot be created", path);
  return false;
#endif
}

}  // namespace esphome::rtems

#endif  // USE_RTEMS
