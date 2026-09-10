#include "esphome/core/defines.h"

#ifdef USE_RTEMS

#include "preferences.h"

#include "esphome/core/preferences.h"

namespace esphome::rtems {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
RTEMSPreferences *rtems_preferences;

ESPPreferenceObject RTEMSPreferences::make_preference(size_t length, uint32_t type, bool in_flash) {
  // in_flash is ignored: there is no flash to put anything in on this platform,
  // and pretending otherwise would only move the surprise later.
  (void) length;
  (void) in_flash;
  return ESPPreferenceObject(new RTEMSPreferenceBackend(type));
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static RTEMSPreferences s_preferences;

RTEMSPreferences *get_preferences() { return &s_preferences; }

void setup_preferences() {
  rtems_preferences = &s_preferences;
  global_preferences = &s_preferences;
}

bool RTEMSPreferenceBackend::save(const uint8_t *data, size_t len) const {
  return get_preferences()->save(this->key_, data, len);
}

bool RTEMSPreferenceBackend::load(uint8_t *data, size_t len) const {
  return get_preferences()->load(this->key_, data, len);
}

}  // namespace esphome::rtems

namespace esphome {

// The definition, not just the assignment in setup_preferences(). Every
// platform backend owns this: core/preferences.h only declares it extern.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
ESPPreferences *global_preferences;

}  // namespace esphome

#endif  // USE_RTEMS
