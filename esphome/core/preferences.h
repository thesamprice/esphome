#pragma once

#include "esphome/core/preference_backend.h"

// Include the concrete preferences manager for the active platform.
// Each header defines its manager class and provides the Preferences,
// ESPPreferences, and global_preferences declarations.
#ifdef USE_ESP32
#include "esphome/components/esp32/preferences.h"
#elif defined(USE_ESP8266)
#include "esphome/components/esp8266/preferences.h"
#elif defined(USE_RP2)
#include "esphome/components/rp2/preferences.h"
#elif defined(USE_LIBRETINY)
#include "esphome/components/libretiny/preferences.h"
#elif defined(USE_HOST)
#include "esphome/components/host/preferences.h"
#elif defined(USE_ZEPHYR) && defined(CONFIG_SETTINGS)
#include "esphome/components/zephyr/preferences.h"
#elif defined(USE_RTEMS)
#include "esphome/components/rtems/preferences.h"
#else
namespace esphome {
struct Preferences : public PreferencesMixin<Preferences> {
  using PreferencesMixin<Preferences>::make_preference;
  ESPPreferenceObject make_preference(size_t, uint32_t, bool) { return {}; }
  ESPPreferenceObject make_preference(size_t, uint32_t) { return {}; }
  bool load_from_key(uint32_t, uint8_t *, size_t) { return false; }

  /**
   * Commit pending writes to flash.
   *
   * @return true if write is successful.
   */
  bool sync() { return false; }

  /**
   * Forget all unsaved changes and re-initialize the permanent preferences storage.
   * Usually followed by a restart which moves the system to "factory" conditions
   *
   * @return true if operation is successful.
   */
  bool reset() { return false; }
};
using ESPPreferences = Preferences;

// Defined here, not merely declared.  Every other branch of this #if defines
// global_preferences in its platform's preferences.cpp; this branch has no
// .cpp anywhere, so an extern declaration would compile and then fail to link
// with "undefined reference to esphome::global_preferences" -- on the one path
// that exists precisely for a platform nobody has written a backend for.
//
// It points at a real instance rather than being null, because the no-op
// manager above is the whole offer of this branch: a null pointer would link
// and then fault on first use, which is a worse failure than the one it
// replaces.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline Preferences no_backend_preferences;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline ESPPreferences *global_preferences{&no_backend_preferences};
}  // namespace esphome
#endif

namespace esphome {
static_assert(PreferencesContract<ESPPreferences>,
              "The platform's preferences manager is missing part of the ESPPreferences surface "
              "(esphome/core/preference_backend.h)");
}  // namespace esphome

#ifdef USE_PREFERENCE_KEY_LOOKUP
namespace esphome {
static_assert(PreferencesKeyLookupContract<ESPPreferences>,
              "This platform emits USE_PREFERENCE_KEY_LOOKUP but its preferences manager does not provide "
              "load_from_key() (esphome/core/preference_backend.h)");
}  // namespace esphome
#endif  // USE_PREFERENCE_KEY_LOOKUP
