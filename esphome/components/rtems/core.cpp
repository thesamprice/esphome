#include "esphome/core/defines.h"

#ifdef USE_RTEMS

#include "preferences.h"

#include <rtems.h>

// ESPHome generates these at global scope in main.cpp, Arduino-style. RTEMS
// has no equivalent convention, so the platform supplies the entry point that
// calls them.
void setup();  // NOLINT(readability-identifier-naming)
void loop();   // NOLINT(readability-identifier-naming)

extern "C" rtems_task Init(rtems_task_argument arg) {  // NOLINT
  (void) arg;
  // Before setup(): components make preferences during their own setup, so the
  // manager has to exist first.
  esphome::rtems::setup_preferences();
  setup();
  while (true) {
    loop();
  }
}

// === RTEMS application configuration ===
//
// An RTEMS application is configured by including <rtems/confdefs.h> in
// exactly one translation unit; that is what generates the scheduler table,
// the object limits and the driver table. Without it the link fails on
// _Scheduler_Table, which is the least obvious symptom of a missing config
// this port is likely to meet.
//
// This lives in the platform component rather than in generated code because
// the values are properties of the platform, not of the user's YAML. Where a
// value should follow the configuration -- task counts once components create
// tasks, the heap once its size is tuned -- it will have to be generated
// instead, and that is a reason to move this file, not to spread #defines
// through main.cpp.

// RTEMS defaults to a 10 ms tick, and every blocking wait with a timeout is
// quantised to it -- rtems_semaphore_obtain takes ticks and has no timespec
// form, so internal::wakeable_delay() rounds up to one.  ESPHome's default
// loop interval is 16 ms, which lands on 20 ms at 100 Hz: measured 19633us
// against 16041us at 1 kHz, so the loop ran 22% slower than configured on
// this platform and nowhere else.
//
// 1 kHz costs ten times the timer interrupts, which is a few tenths of a
// percent of a 160 MHz part and is the cheaper side of the trade.  It is not
// the general fix for sub-tick timing -- that is #49, and needs a one-shot
// clock driver -- but it bounds the error at 1 ms, which is below anything
// ESPHome schedules.
#define CONFIGURE_MICROSECONDS_PER_TICK 1000
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_SIMPLE_CONSOLE_DRIVER

// The console is a device, so the file descriptors have to exist.
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 8

// One Init task plus headroom. ESPHome's own components create tasks on other
// platforms; when they do so here this stops being a constant.
#define CONFIGURE_MAXIMUM_TASKS 8

// wake_rtems.cpp creates one counting semaphore. Components will want more.
#define CONFIGURE_MAXIMUM_SEMAPHORES 16
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES 8

// ESPHome is C++ with a std::string-heavy core and allocates during setup, so
// the Init task gets a real stack rather than the minimum.
#define CONFIGURE_INIT_TASK_STACK_SIZE (16 * 1024)

// Floating point, because ESPHome sensors are floats and the generated
// lambdas do arithmetic on them in task context.
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT

#define CONFIGURE_UNLIMITED_OBJECTS
#define CONFIGURE_UNIFIED_WORK_AREAS

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT
#include <rtems/confdefs.h>

#endif  // USE_RTEMS
