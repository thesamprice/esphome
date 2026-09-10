#include "esphome/core/defines.h"

#ifdef USE_RTEMS

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

#include <rtems.h>
#include <rtems/score/percpu.h>
#include <rtems/score/isrlevel.h>

namespace esphome {

// === Mutex ===
//
// A binary semaphore with priority inheritance, not a counting one and not a
// pthread_mutex.
//
// Priority inheritance matters: ESPHome takes this in the scheduler, which
// runs at the main loop's priority, and a lower-priority task holding it while
// a higher-priority one waits would otherwise invert. RTEMS spells that
// RTEMS_INHERIT_PRIORITY, which requires RTEMS_BINARY_SEMAPHORE and
// RTEMS_PRIORITY.
//
// try_lock() is the reason this is not NASA OSAL: OSAL's mutex API has take
// and give and no non-blocking form, and ESPHome's contract requires one. See
// docs/architecture.md.
Mutex::Mutex() {
  rtems_id id = RTEMS_INVALID_ID;
  rtems_semaphore_create(rtems_build_name('E', 'S', 'P', 'M'), 1,
                         RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY | RTEMS_INHERIT_PRIORITY, 0, &id);
  this->handle_ = reinterpret_cast<void *>(static_cast<uintptr_t>(id));
}

Mutex::~Mutex() {
  rtems_semaphore_delete(static_cast<rtems_id>(reinterpret_cast<uintptr_t>(this->handle_)));
}

void Mutex::lock() {
  rtems_semaphore_obtain(static_cast<rtems_id>(reinterpret_cast<uintptr_t>(this->handle_)), RTEMS_WAIT,
                         RTEMS_NO_TIMEOUT);
}

bool Mutex::try_lock() {
  return rtems_semaphore_obtain(static_cast<rtems_id>(reinterpret_cast<uintptr_t>(this->handle_)), RTEMS_NO_WAIT,
                                0) == RTEMS_SUCCESSFUL;
}

void Mutex::unlock() {
  rtems_semaphore_release(static_cast<rtems_id>(reinterpret_cast<uintptr_t>(this->handle_)));
}

// === InterruptLock ===
//
// RTEMS' own interrupt disable/enable, which is nestable: the saved level is
// restored rather than interrupts being unconditionally enabled, so a nested
// lock does not re-enable them when the inner one exits.
InterruptLock::InterruptLock() {
  rtems_interrupt_level level;
  rtems_interrupt_disable(level);
  this->state_ = level;
}

InterruptLock::~InterruptLock() {
  rtems_interrupt_level level = this->state_;
  rtems_interrupt_enable(level);
}

// === ISR context ===
//
// RTEMS keeps the ISR nest level in the per-CPU control block and it is
// non-zero for the whole of an interrupt, including nested ones. Unlike the
// FreeRTOS equivalent this needs no port-specific call.
bool in_isr_context() { return _ISR_Is_in_progress(); }

}  // namespace esphome

#endif  // USE_RTEMS
