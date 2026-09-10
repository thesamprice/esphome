#include "esphome/core/defines.h"

#ifdef USE_RTEMS

#include "esphome/core/hal.h"
#include "esphome/core/wake.h"

#include <rtems.h>

#ifdef ESPHOME_THREAD_MULTI_ATOMICS
#include <atomic>
#endif

namespace esphome {

namespace {

// Created on first use rather than from a constructor: RTEMS object creation
// needs the kernel running, and a static constructor may run before that.
// Every caller reaches the semaphore through this, so there is no path that
// uses an uncreated id.
rtems_id get_wake_semaphore() {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static rtems_id sem = RTEMS_INVALID_ID;
  if (sem == RTEMS_INVALID_ID) [[unlikely]] {
    // Counting, not binary: a producer that signals while the loop is running
    // rather than waiting must not have its wake swallowed. The count is
    // consumed by the next wakeable_delay(), so the loop runs once more, which
    // is the intended behaviour.
    //
    // No priority inheritance and no priority ceiling: this is a signal rather
    // than a lock, it is never held across a critical section, and both
    // protocols are illegal on a counting semaphore in RTEMS anyway.
    rtems_semaphore_create(rtems_build_name('E', 'S', 'P', 'W'), 0,
                           RTEMS_COUNTING_SEMAPHORE | RTEMS_FIFO, 0, &sem);
  }
  return sem;
}

}  // namespace

// === Wake-requested flag storage ===
// Both forms, because the thread model is a property of the BSP rather than of
// RTEMS: a CPU with atomic read-modify-write wants MULTI_ATOMICS, and one
// without -- riscv/esp32c3db is -march=rv32imc, no A extension -- may want the
// other. The declaration in wake.h switches on the same define, so this has to
// follow it or the two disagree at link time.
//
// The non-atomic form is sound for the same reason it is on Zephyr: an aligned
// 8-bit store cannot tear on any CPU RTEMS supports, and every producer pairs
// the store with rtems_semaphore_release() while the consumer pairs the load
// with rtems_semaphore_obtain(), which supply the ordering.
#ifdef ESPHOME_THREAD_MULTI_ATOMICS
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<uint8_t> g_wake_requested{0};
#else
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile uint8_t g_wake_requested = 0;
#endif

void wake_loop_threadsafe() {
  wake_request_set();
  rtems_semaphore_release(get_wake_semaphore());
}

namespace internal {

void wakeable_delay(uint32_t ms) {
  if (ms == 0) [[unlikely]] {
    yield();
    return;
  }

  rtems_interval timeout;
  if (ms == UINT32_MAX) {
    timeout = RTEMS_NO_TIMEOUT;
  } else {
    // Round up. Truncating would return early on any tick period that does not
    // divide the request, turning a delay into a busy loop at the sub-tick
    // scale -- a 1 ms request on a 10 ms tick would not wait at all.
    const uint32_t per_second = rtems_clock_get_ticks_per_second();
    const uint64_t ticks = ((uint64_t) ms * per_second + 999U) / 1000U;
    timeout = ticks == 0 ? 1 : (rtems_interval) ticks;
  }

  rtems_semaphore_obtain(get_wake_semaphore(), RTEMS_WAIT, timeout);
}

}  // namespace internal

}  // namespace esphome

#endif  // USE_RTEMS
