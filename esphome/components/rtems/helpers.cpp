#include "esphome/core/defines.h"

#ifdef USE_RTEMS

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

#include <rtems.h>
#include <rtems/score/percpu.h>
#include <rtems/score/isrlevel.h>

#ifdef USE_RTEMS_MUTEX_RECURSION_CHECK
#include <rtems/bspIo.h>
#endif

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
//
// It is recursive, and on ESP32 and LibreTiny it is not: those use
// xSemaphoreCreateMutex(), which FreeRTOS spells non-recursive. RTEMS permits
// the owner to re-take a binary semaphore under priority inheritance.
//
// That divergence is the wrong way round. A component that takes this twice on
// one path -- directly, or through a helper that locks -- deadlocks on the ESP
// and runs fine here, so the cheap emulated lane is the one guaranteed to miss
// it. Same for try_lock() used as an "is it already held" probe, which answers
// opposite on the two platforms.
//
// So rather than change the primitive, detect the difference. Keeping
// RTEMS_INHERIT_PRIORITY is not negotiable -- the scheduler takes this at main
// loop priority and inverting there is a real failure (#44) -- and the
// alternatives give that up or rewrite this on pthreads. Recording the owner
// costs one word and one comparison, and turns the lane that would have missed
// the bug into the one that reports it.
Mutex::Mutex() {
  rtems_id id = RTEMS_INVALID_ID;
  rtems_semaphore_create(rtems_build_name('E', 'S', 'P', 'M'), 1,
                         RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY | RTEMS_INHERIT_PRIORITY, 0, &id);
  this->handle_ = reinterpret_cast<void *>(static_cast<uintptr_t>(id));
}

Mutex::~Mutex() {
  rtems_semaphore_delete(static_cast<rtems_id>(reinterpret_cast<uintptr_t>(this->handle_)));
}

#ifdef USE_RTEMS_MUTEX_RECURSION_CHECK
// printk(), not ESP_LOGE(): the logger takes a mutex, so reporting through it
// from inside the mutex is how a diagnostic becomes a deadlock. printk() goes
// straight to the BSP console and takes nothing.
static void report_recursive_take(const char *how) {
  printk("esphome: Mutex::%s() re-taken by the task that holds it.\n", how);
  printk("  This deadlocks on ESP32 and LibreTiny, whose mutex is not recursive.\n");
}
#endif

void Mutex::lock() {
#ifdef USE_RTEMS_MUTEX_RECURSION_CHECK
  // Read before obtaining. Only the owner writes owner_, and only while
  // holding, so a reader comparing against its own id cannot see a torn or
  // stale value that names itself.
  if (this->owner_ == rtems_task_self()) {
    report_recursive_take("lock");
  }
#endif
  rtems_semaphore_obtain(static_cast<rtems_id>(reinterpret_cast<uintptr_t>(this->handle_)), RTEMS_WAIT,
                         RTEMS_NO_TIMEOUT);
#ifdef USE_RTEMS_MUTEX_RECURSION_CHECK
  this->owner_ = rtems_task_self();
#endif
}

bool Mutex::try_lock() {
#ifdef USE_RTEMS_MUTEX_RECURSION_CHECK
  const bool held_by_us = this->owner_ == rtems_task_self();
#endif
  const bool got = rtems_semaphore_obtain(static_cast<rtems_id>(reinterpret_cast<uintptr_t>(this->handle_)),
                                          RTEMS_NO_WAIT, 0) == RTEMS_SUCCESSFUL;
#ifdef USE_RTEMS_MUTEX_RECURSION_CHECK
  if (got && held_by_us) {
    // The probe case: on the ESP this returns false and here it returns true,
    // so a caller using it to ask "is this already held" gets the opposite
    // answer on the two platforms.
    report_recursive_take("try_lock");
  }
  if (got) {
    this->owner_ = rtems_task_self();
  }
#endif
  return got;
}

void Mutex::unlock() {
#ifdef USE_RTEMS_MUTEX_RECURSION_CHECK
  // Cleared before the release, or a task that acquires the moment it is freed
  // would overwrite owner_ and then have it reset to zero underneath it.
  this->owner_ = 0;
#endif
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
