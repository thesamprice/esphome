#pragma once

#include "esphome/core/defines.h"

#ifdef USE_RTEMS

#include "esphome/core/hal.h"

namespace esphome {

/// RTEMS: wakes the main loop by releasing a counting semaphore.
/// Thread- and ISR-safe. Defined in wake_rtems.cpp.
void wake_loop_threadsafe();

inline void wake_loop_any_context() { wake_loop_threadsafe(); }

/// ISR-safe: no task_woken argument, because rtems_semaphore_release() is
/// callable from an ISR and defers the resulting dispatch itself -- RTEMS runs
/// the scheduler when the outermost interrupt exits. Forwards to
/// wake_loop_threadsafe().
inline void wake_loop_isrsafe() { wake_loop_threadsafe(); }

namespace internal {
/// Obtains the same semaphore with a timeout -- defined in wake_rtems.cpp.
void wakeable_delay(uint32_t ms);
}  // namespace internal

}  // namespace esphome

#endif  // USE_RTEMS
