#include "esphome/core/defines.h"

#ifdef USE_RTEMS

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

#include <rtems.h>
#include <rtems/counter.h>
#include <unistd.h>
#include <time.h>

#include <cstdlib>
#include <cstring>

namespace esphome {

// === Time ===
//
// rtems_clock_get_uptime_nanoseconds() is a 64-bit monotonic count from boot,
// so it is the source for all of these. That is also why this platform defines
// USE_NATIVE_64BIT_TIME: ESPHome's fallback tracks 32-bit rollover with static
// state and a mutex taken every 49.7 days, and none of that is needed when the
// clock is already 64-bit.
//
// Resolution follows the clock driver, which on riscv/esp32c3db is the
// SYSTIMER at 16 MHz -- 62.5 ns, comfortably finer than a microsecond.

uint32_t millis() { return static_cast<uint32_t>(rtems_clock_get_uptime_nanoseconds() / 1000000ULL); }

uint64_t millis_64() { return rtems_clock_get_uptime_nanoseconds() / 1000000ULL; }

uint32_t micros() { return static_cast<uint32_t>(rtems_clock_get_uptime_nanoseconds() / 1000ULL); }

void delay(uint32_t ms) {
  if (ms == 0) {
    // Zero must still give the scheduler a chance to run something else.
    yield();
    return;
  }
  // clock_nanosleep on CLOCK_MONOTONIC, not rtems_task_wake_after().
  //
  // wake_after() counts clock ticks, and RTEMS counts the partial tick the
  // caller is already inside as the first one, so wake_after(n) returns
  // somewhere between (n-1) and n tick periods.  Getting a lower bound out of
  // it meant rounding the request up to whole ticks and then adding one more,
  // which made delay(50) sleep for up to 60ms on a 10ms tick.
  //
  // clock_nanosleep has neither problem.  RTEMS enqueues it on the per-CPU
  // MONOTONIC watchdog rather than the tick watchdog, so the deadline is an
  // exact timespec instead of a rounded tick count, and the sleep is a real
  // lower bound with nothing to correct for.
  //
  // It is not finer *resolution*, and it would be easy to assume it is.
  // _Watchdog_Tick() services the monotonic header as well as the tick header,
  // so a nanosecond deadline is still only examined once per clock tick: the
  // sleep ends at the first tick at or after the deadline.  delay(50) on a
  // 10ms tick therefore returns somewhere in 50..60ms -- the same spread as
  // before, but now as a consequence of the tick rather than of arithmetic
  // here, and never below the request.
  //
  // Sub-tick sleeps would need a BSP whose clock driver programs a one-shot
  // timer from the watchdog deadline.  This one is a periodic tick.
  struct timespec req;
  req.tv_sec = static_cast<time_t>(ms / 1000U);
  req.tv_nsec = static_cast<long>((ms % 1000U) * 1000000UL);
  // Relative sleep. EINTR is the only failure worth retrying, and ESPHome does
  // not deliver signals to the main loop, so a single call is enough.
  clock_nanosleep(CLOCK_MONOTONIC, 0, &req, nullptr);
}

void delayMicroseconds(uint32_t us) {  // NOLINT(readability-identifier-naming)
  // Busy wait rather than a task delay: a microsecond is far below the tick
  // period, so sleeping would overshoot by orders of magnitude.
  rtems_counter_delay_nanoseconds(static_cast<uint32_t>(us) * 1000U);
}

void yield() { rtems_task_wake_after(RTEMS_YIELD_PROCESSOR); }

// === Architecture ===

void arch_init() {}

void arch_feed_wdt() {
  // The BSP disables the RTC and timer-group watchdogs during bsp_start(), so
  // there is nothing to feed. A BSP that leaves one running wants this to
  // become a per-BSP hook rather than a no-op.
}

void arch_restart() {
  // rtems_shutdown_executive() unwinds through the fatal path, which is what
  // reaches bsp_reset(). It does not return; the loop is here because the
  // declaration is noreturn and the compiler cannot see that.
  rtems_shutdown_executive(0);
  while (true) {
  }
}

uint32_t arch_get_cpu_cycle_count() { return static_cast<uint32_t>(rtems_counter_read()); }

uint32_t arch_get_cpu_freq_hz() {
  // The frequency of the counter arch_get_cpu_cycle_count() reads, which is
  // what a caller converting those ticks to time needs. On riscv/esp32c3db
  // that is the 16 MHz SYSTIMER rather than the 160 MHz core clock, so this is
  // deliberately not "the CPU clock" -- the two are only the same on a part
  // whose cycle counter is the CPU's.
  return rtems_counter_frequency();
}

// === Entropy ===

bool random_bytes(uint8_t *data, size_t len) {
  // getentropy() is POSIX and RTEMS provides it; the BSP backs it with the CPU
  // counter (bsps/shared/dev/getentropy/getentropy-cpucounter.c), which is not
  // a cryptographic source. Anything here that needs real entropy has to say
  // so and get a hardware RNG from the BSP.
  return getentropy(data, len) == 0;
}

// === MAC address ===

void get_mac_address_raw(uint8_t *mac) {  // NOLINT(readability-non-const-parameter)
  // The ESP32-C3 has no Ethernet MAC in silicon and its WiFi is not emulated,
  // so there is no address to read. Report a fixed locally administered one --
  // the 0x02 in the first octet is the locally-administered bit -- rather than
  // inventing something that looks globally unique.
  //
  // A BSP with a real interface should read it from there; when one exists this
  // needs to become a per-BSP hook rather than a constant.
  static const uint8_t LOCALLY_ADMINISTERED[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
  std::memcpy(mac, LOCALLY_ADMINISTERED, sizeof(LOCALLY_ADMINISTERED));
}

}  // namespace esphome

#endif  // USE_RTEMS
