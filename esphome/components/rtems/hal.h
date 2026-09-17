#pragma once

#ifdef USE_RTEMS

#include <cstdint>

#include <rtems.h>

// RTEMS places nothing in a special fast-memory section by default, and the
// BSPs that have one expose it through their own linker section attributes
// rather than through a portable spelling.  Both are no-ops until a BSP is
// found that needs otherwise.
#define IRAM_ATTR
#define PROGMEM

namespace esphome::rtems {}

namespace esphome {

/// Returns true when executing inside an interrupt handler.
///
/// RTEMS tracks this in the per-CPU control block: ISR nest level is non-zero
/// for the whole of an interrupt, including nested ones.  Unlike the FreeRTOS
/// equivalent this needs no port-specific call.
bool in_isr_context();

void yield();
void delay(uint32_t ms);
uint32_t micros();
uint32_t millis();
uint64_t millis_64();
void delayMicroseconds(uint32_t us);  // NOLINT(readability-identifier-naming)
uint32_t arch_get_cpu_cycle_count();

void arch_init();
void arch_feed_wdt();
uint32_t arch_get_cpu_freq_hz();

}  // namespace esphome

#endif  // USE_RTEMS
