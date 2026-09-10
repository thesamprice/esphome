#ifdef USE_RTEMS

#include "gpio.h"
#include "esphome/core/log.h"

/*
 * <bsp/gpio.h> is not universally usable.  It #errors at preprocess time
 * unless the BSP defines BSP_GPIO_PIN_COUNT and BSP_GPIO_PINS_PER_BANK, and
 * most do not -- arm/xilinx_zynq_a9_qemu among them.  So the macros are the
 * feature test, and they come from <bsp.h>, which has to be included first.
 *
 * A BSP without them still gets a RTEMSGPIOPin that links; it reports at
 * runtime that the board has no GPIO through this API.  The alternative is a
 * platform that cannot be built at all for such a board, which is worse: every
 * other component would work there.
 */
#include <bsp.h>

#if defined(BSP_GPIO_PIN_COUNT) && defined(BSP_GPIO_PINS_PER_BANK)
#define ESPHOME_RTEMS_HAS_BSP_GPIO 1
#include <bsp/gpio.h>
#endif

namespace esphome {
namespace rtems {

static const char *const TAG = "rtems.gpio";

#ifdef ESPHOME_RTEMS_HAS_BSP_GPIO
/// The shared layer numbers pins across banks; the BSP hooks take a bank and a
/// pin within it.  BSP_GPIO_PINS_PER_BANK is the only thing needed to convert,
/// and it is part of the BSP's public interface.
static inline uint32_t gpio_bank(uint8_t pin) { return pin / BSP_GPIO_PINS_PER_BANK; }
static inline uint32_t gpio_pin(uint8_t pin) { return pin % BSP_GPIO_PINS_PER_BANK; }
#endif

struct ISRPinArg {
  uint32_t bank;
  uint32_t pin;
  bool inverted;
};

#ifndef ESPHOME_RTEMS_HAS_BSP_GPIO
// Values the no-GPIO build needs so the ISR helpers below still compile.
enum { IRQ_HANDLED = 0 };
#endif

/*
 * Two layers, on purpose.
 *
 * Configuration -- requesting a pin, its pull resistor, its interrupt -- goes
 * through rtems_gpio_*(), which keeps the bookkeeping that makes a second
 * component asking for the same pin an error rather than a silent conflict.
 *
 * Reading and writing go straight to rtems_gpio_bsp_*().  Two reasons, and
 * either alone would be enough: the shared layer takes a bank mutex per call,
 * which digital_write() cannot do because ESPHome calls it from interrupt
 * context; and rtems_gpio_get_value() refuses to read a pin it has been told
 * is an output, which ESPHome does routinely.  The BSP hooks are plain
 * register accesses with no locking, so both are fine there.
 */

void RTEMSGPIOPin::pin_mode(gpio::Flags flags) {
#ifndef ESPHOME_RTEMS_HAS_BSP_GPIO
  (void) flags;
  ESP_LOGE(TAG, "This BSP does not provide <bsp/gpio.h>, so GPIO%u cannot be configured", this->pin_);
#else
  rtems_status_code sc;

  // Idempotent -- it returns immediately on an atomic flag once it has run --
  // so this is how the shared layer is brought up without every RTEMS build
  // linking it in whether or not the configuration has a pin in it.
  rtems_gpio_initialize();

  // The shared layer will not re-purpose a pin that is still held.
  if (this->requested_) {
    rtems_gpio_release_pin(this->pin_);
    this->requested_ = false;
  }

  const bool output = (flags & gpio::FLAG_OUTPUT) != 0;
  sc = rtems_gpio_request_pin(this->pin_, output ? DIGITAL_OUTPUT : DIGITAL_INPUT, output, this->inverted_, nullptr);
  if (sc != RTEMS_SUCCESSFUL) {
    ESP_LOGE(TAG, "GPIO%u: cannot request as %s: %s", this->pin_, output ? "output" : "input",
             rtems_status_text(sc));
    return;
  }
  this->requested_ = true;
  this->flags_ = flags;

  rtems_gpio_pull_mode pull = NO_PULL_RESISTOR;
  if (flags & gpio::FLAG_PULLUP) {
    pull = PULL_UP;
  } else if (flags & gpio::FLAG_PULLDOWN) {
    pull = PULL_DOWN;
  }
  sc = rtems_gpio_resistor_mode(this->pin_, pull);
  if (sc != RTEMS_SUCCESSFUL) {
    ESP_LOGW(TAG, "GPIO%u: cannot set the pull resistor: %s", this->pin_, rtems_status_text(sc));
  }

  if (flags & gpio::FLAG_OPEN_DRAIN) {
    // <bsp/gpio.h> has no open-drain mode.  Say so rather than configuring a
    // push-pull output and letting a bus with two drivers on it find out.
    ESP_LOGW(TAG, "GPIO%u: open drain is not available on RTEMS; the pin is push-pull", this->pin_);
  }
#endif
}

bool RTEMSGPIOPin::digital_read() {
#ifndef ESPHOME_RTEMS_HAS_BSP_GPIO
  return this->inverted_;
#else
  const bool level = rtems_gpio_bsp_get_value(gpio_bank(this->pin_), gpio_pin(this->pin_)) != 0;
  return level != this->inverted_;
#endif
}

void RTEMSGPIOPin::digital_write(bool value) {
#ifndef ESPHOME_RTEMS_HAS_BSP_GPIO
  (void) value;
#else
  const uint32_t bank = gpio_bank(this->pin_);
  const uint32_t pin = gpio_pin(this->pin_);

  if (value != this->inverted_) {
    rtems_gpio_bsp_set(bank, pin);
  } else {
    rtems_gpio_bsp_clear(bank, pin);
  }
#endif
}

size_t RTEMSGPIOPin::dump_summary(char *buffer, size_t len) const {
  return snprintf(buffer, len, "GPIO%u", this->pin_);
}

ISRInternalGPIOPin RTEMSGPIOPin::to_isr() const {
  auto *arg = new ISRPinArg{};  // NOLINT(cppcoreguidelines-owning-memory)
#ifdef ESPHOME_RTEMS_HAS_BSP_GPIO
  arg->bank = gpio_bank(this->pin_);
  arg->pin = gpio_pin(this->pin_);
#endif
  arg->inverted = this->inverted_;
  return ISRInternalGPIOPin((void *) arg);
}

/// Adapt ESPHome's handler, which returns nothing, to the shared layer's,
/// which reports whether the interrupt was its.  One pin, one handler, so it
/// always was.
#ifdef ESPHOME_RTEMS_HAS_BSP_GPIO
static rtems_gpio_irq_state gpio_isr_trampoline(void *arg) {
  auto *thunk = reinterpret_cast<std::pair<void (*)(void *), void *> *>(arg);
  thunk->first(thunk->second);
  return IRQ_HANDLED;
}
#endif

void RTEMSGPIOPin::attach_interrupt(void (*func)(void *), void *arg, gpio::InterruptType type) const {
#ifndef ESPHOME_RTEMS_HAS_BSP_GPIO
  (void) func;
  (void) arg;
  (void) type;
  ESP_LOGE(TAG, "This BSP does not provide <bsp/gpio.h>, so GPIO%u cannot interrupt", this->pin_);
#else
  rtems_gpio_interrupt trigger;

  switch (type) {
    case gpio::INTERRUPT_RISING_EDGE:
      trigger = RISING_EDGE;
      break;
    case gpio::INTERRUPT_FALLING_EDGE:
      trigger = FALLING_EDGE;
      break;
    case gpio::INTERRUPT_ANY_EDGE:
      trigger = BOTH_EDGES;
      break;
    case gpio::INTERRUPT_LOW_LEVEL:
      trigger = LOW_LEVEL;
      break;
    case gpio::INTERRUPT_HIGH_LEVEL:
      trigger = HIGH_LEVEL;
      break;
    default:
      ESP_LOGE(TAG, "GPIO%u: unknown interrupt type %u", this->pin_, (unsigned) type);
      return;
  }

  // Outlives the call: the shared layer keeps the pointer for as long as the
  // interrupt is enabled, and detach_interrupt() is what ends that.
  auto *thunk = new std::pair<void (*)(void *), void *>(func, arg);  // NOLINT(cppcoreguidelines-owning-memory)

  // false: run the handler in interrupt context.  Threaded handling would put
  // it behind an interrupt server task, which is a scheduling decision ESPHome
  // has already made for the caller -- its ISRs are written to be short and to
  // hand work to the main loop themselves.
  rtems_status_code sc =
      rtems_gpio_enable_interrupt(this->pin_, trigger, UNIQUE_HANDLER, false, gpio_isr_trampoline, thunk);
  if (sc != RTEMS_SUCCESSFUL) {
    ESP_LOGE(TAG, "GPIO%u: cannot enable the interrupt: %s", this->pin_, rtems_status_text(sc));
    delete thunk;  // NOLINT(cppcoreguidelines-owning-memory)
  }
#endif
}

void RTEMSGPIOPin::detach_interrupt() const {
#ifdef ESPHOME_RTEMS_HAS_BSP_GPIO
  rtems_status_code sc = rtems_gpio_disable_interrupt(this->pin_);
  if (sc != RTEMS_SUCCESSFUL) {
    ESP_LOGW(TAG, "GPIO%u: cannot disable the interrupt: %s", this->pin_, rtems_status_text(sc));
  }
#endif
}

}  // namespace rtems

using namespace rtems;

bool IRAM_ATTR ISRInternalGPIOPin::digital_read() {
  auto *arg = reinterpret_cast<ISRPinArg *>(arg_);
#ifdef ESPHOME_RTEMS_HAS_BSP_GPIO
  return (rtems_gpio_bsp_get_value(arg->bank, arg->pin) != 0) != arg->inverted;
#else
  return arg->inverted;
#endif
}

void IRAM_ATTR ISRInternalGPIOPin::digital_write(bool value) {
  auto *arg = reinterpret_cast<ISRPinArg *>(arg_);
#ifndef ESPHOME_RTEMS_HAS_BSP_GPIO
  (void) value;
#else
  if (value != arg->inverted) {
    rtems_gpio_bsp_set(arg->bank, arg->pin);
  } else {
    rtems_gpio_bsp_clear(arg->bank, arg->pin);
  }
#endif
}

void IRAM_ATTR ISRInternalGPIOPin::clear_interrupt() {
  // Nothing to do.  The shared layer's own dispatcher reads and clears the
  // bank's event line before it calls any per-pin handler, so by the time this
  // could run the pin has already been acknowledged.  Clearing again here
  // would discard an edge that arrived in between.
}

}  // namespace esphome

#endif  // USE_RTEMS
