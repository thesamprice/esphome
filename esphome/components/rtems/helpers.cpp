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

#ifdef USE_RTEMS

/*
 * lwIP core locking.
 *
 * rtems-lwip leaves LWIP_TCPIP_CORE_LOCKING at its upstream default of 1, so
 * the lock is real: lwIP's APIs assert LWIP_ASSERT_CORE_LOCKED() and a caller
 * without it halts the run with no message on the console an application is
 * watching.  ESPHome's default for a platform it does not know is a no-op
 * LwIPLock, which is the wrong half of the choice here.
 */
#if __has_include(<lwip/tcpip.h>)
#include <lwip/tcpip.h>
#define ESPHOME_RTEMS_HAS_LWIP_LOCK 1
#endif

namespace esphome {

#ifdef ESPHOME_RTEMS_HAS_LWIP_LOCK
/*
 * Who holds the lock, and how deep.
 *
 * ESP-IDF answers this with sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER),
 * which is its own extension; upstream lwIP has no holder query, so the
 * bookkeeping is here.  It is needed because nested LwIPLocks are legitimate --
 * a component takes one and calls something that takes another -- and the lwIP
 * mutex is not recursive, so taking it twice on one task would deadlock.
 *
 * Reading holder without the lock is safe: it is written only by the task that
 * holds the lock, and cleared before that task releases it, so another task can
 * see a stale value but never its own id.
 */
static rtems_id lwip_lock_holder = 0;
static unsigned lwip_lock_depth = 0;
#endif

LwIPLock::LwIPLock() {
#ifdef ESPHOME_RTEMS_HAS_LWIP_LOCK
  rtems_id self = rtems_task_self();

  if (lwip_lock_holder == self) {
    ++lwip_lock_depth;
    return;
  }

  LOCK_TCPIP_CORE();
  lwip_lock_holder = self;
  lwip_lock_depth = 1;
#endif
}

LwIPLock::~LwIPLock() {
#ifdef ESPHOME_RTEMS_HAS_LWIP_LOCK
  if (lwip_lock_holder != rtems_task_self()) {
    // Not ours to release.  Reached only if a lock outlived its scope, which
    // would be a bug elsewhere; dropping someone else's lock would turn it
    // into a much harder one.
    return;
  }

  if (--lwip_lock_depth == 0) {
    lwip_lock_holder = 0;
    UNLOCK_TCPIP_CORE();
  }
#endif
}

}  // namespace esphome

#endif  // USE_RTEMS
