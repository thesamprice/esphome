#pragma once
#ifdef USE_RTEMS

#include <cstddef>
#include <cstdint>

namespace esphome::rtems {

class RTEMSPreferenceBackend final {
 public:
  explicit RTEMSPreferenceBackend(uint32_t key) : key_(key) {}

  bool save(const uint8_t *data, size_t len) const;
  bool load(uint8_t *data, size_t len) const;

 protected:
  uint32_t key_{};
};

class RTEMSPreferences;
RTEMSPreferences *get_preferences();

}  // namespace esphome::rtems

namespace esphome {
using PreferenceBackend = rtems::RTEMSPreferenceBackend;
}  // namespace esphome

#endif  // USE_RTEMS
