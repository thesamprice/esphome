#ifdef USE_RTEMS
#include "logger.h"

#include <rtems.h>

#include <cstdio>

namespace esphome::logger {

void HOT Logger::write_msg_(const char *msg, uint16_t len) {
  // Straight to stdout, which RTEMS points at the console device configured by
  // the platform (CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER). No
  // timestamp is prepended, unlike the host backend: RTEMS' wall clock starts
  // at an arbitrary epoch until something sets it, so a timestamp here would be
  // confidently wrong rather than absent.
  fwrite(msg, 1, len, stdout);
  fflush(stdout);
}

const char *HOT Logger::get_thread_name_(std::span<char> buff) {
  const rtems_id current = rtems_task_self();
  if (current == this->main_task_id_) {
    return nullptr;  // Main task
  }
  // RTEMS copies the name out rather than returning a pointer into the object,
  // so the caller's buffer is the only thing with a safe lifetime. A task
  // created without a name yields an empty string, which is worth reporting as
  // "no name" rather than as an empty one.
  if (rtems_object_get_name(current, buff.size(), buff.data()) == nullptr) {
    return nullptr;
  }
  return buff[0] == '\0' ? nullptr : buff.data();
}

void Logger::pre_setup() { global_logger = this; }

}  // namespace esphome::logger

#endif
