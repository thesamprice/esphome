#pragma once
#ifdef USE_RTEMS

#include "esphome/core/preference_backend.h"

#include <cstring>
#include <map>
#include <vector>

namespace esphome::rtems {

/// Preferences held in RAM for the lifetime of one boot.
///
/// They do NOT survive a restart. That is a real limitation rather than a
/// simplification, and it is deliberate rather than overlooked: persisting
/// them needs somewhere to persist to, and the first target BSP
/// (riscv/esp32c3db) configures no filesystem and has no flash driver. A
/// backend that wrote nowhere while reporting success would be worse than one
/// that says what it does.
///
/// Within a single run this behaves correctly: a value saved is a value
/// loaded, which is what components restoring their own state during a session
/// need. What breaks is restore-across-reboot.
class RTEMSPreferences final : public PreferencesMixin<RTEMSPreferences> {
 public:
  using PreferencesMixin<RTEMSPreferences>::make_preference;

  /// Nothing to flush: the store is already the authoritative copy.
  /// Reports success because the caller's data is safe as far as this backend
  /// promises, not because it reached storage.
  bool sync() { return true; }

  /// Drops everything, which is the whole of "factory conditions" for a store
  /// that never reaches storage.
  bool reset() {
    this->data_.clear();
    return true;
  }

  ESPPreferenceObject make_preference(size_t length, uint32_t type, bool in_flash);
  ESPPreferenceObject make_preference(size_t length, uint32_t type) { return make_preference(length, type, false); }

  bool save(uint32_t key, const uint8_t *data, size_t len) {
    if (len > 255)
      return false;
    this->data_[key] = std::vector<uint8_t>(data, data + len);
    return true;
  }

  /// One-shot read of a stored preference by key, without allocating a backend
  bool load_from_key(uint32_t type, uint8_t *data, size_t len) { return this->load(type, data, len); }

  bool load(uint32_t key, uint8_t *data, size_t len) {
    if (len > 255)
      return false;
    auto it = this->data_.find(key);
    if (it == this->data_.end())
      return false;
    const auto &vec = it->second;
    // A length mismatch means the stored value is not the thing being asked
    // for -- a changed struct, or a key collision -- so refuse rather than
    // hand back a partial read.
    if (vec.size() != len)
      return false;
    std::memcpy(data, vec.data(), len);
    return true;
  }

 protected:
  std::map<uint32_t, std::vector<uint8_t>> data_{};
};

void setup_preferences();
extern RTEMSPreferences *rtems_preferences;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace esphome::rtems

DECLARE_PREFERENCE_ALIASES(esphome::rtems::RTEMSPreferences)

#endif  // USE_RTEMS
