#include "esphome/core/defines.h"

#ifdef USE_RTEMS

#include "spi.h"
#include "esphome/core/log.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <dev/spi/spi.h>

/*
 * Hardware SPI on RTEMS, through the bus device the BSP registers.
 *
 * A BSP with a controller installs <bsp/spi.h> and defines BSP_SPI_REGISTER in
 * it, exactly as <bsp/i2c.h> does for I2C -- the presence of the header is the
 * feature test and the alias is the call, so a second BSP needs no change
 * here.  A weak reference would not work for the same reason it did not for
 * I2C: the driver comes from an archive, and a weak undefined reference does
 * not pull a member out of one.
 */
#if __has_include(<bsp/spi.h>)
#include <bsp/spi.h>
#endif

namespace esphome::spi {

static const char *const TAG = "spi.rtems";

/*
 * Chip select belongs to the controller here, not to this delegate.
 *
 * ESPHome's SPIDelegate drives cs_pin_ as a GPIO around each transaction,
 * which is right on a platform where the controller does not have a chip
 * select of its own.  RTEMS' SPI framework has one: spi_ioc_transfer carries
 * cs_change, and the driver asserts and releases around the message.  Driving
 * a second pad from here as well would select nothing and deselect nothing,
 * and on a board where cs_pin happens to name a real pad it would fight the
 * controller for it.
 *
 * So the transaction hooks are overridden to do nothing. A configuration that
 * needs a GPIO chip select -- more devices than the controller has selects --
 * wants the bit-bang delegate, which is what omitting the bus does.
 */
class RTEMSSPIDelegate : public SPIDelegate {
 public:
  RTEMSSPIDelegate(int fd, uint32_t data_rate, SPIBitOrder bit_order, SPIMode mode, GPIOPin *cs_pin)
      : SPIDelegate(data_rate, bit_order, mode, cs_pin), fd_(fd) {}

  void begin_transaction() override {}
  void end_transaction() override {}

  uint8_t transfer(uint8_t data) override {
    uint8_t rx = 0;
    this->exchange_(&data, &rx, 1);
    return rx;
  }

  void transfer(const uint8_t *txbuf, uint8_t *rxbuf, size_t length) override {
    this->exchange_(txbuf, rxbuf, length);
  }

  void write_array(const uint8_t *ptr, size_t length) override { this->exchange_(ptr, nullptr, length); }

  void read_array(uint8_t *ptr, size_t length) override { this->exchange_(nullptr, ptr, length); }

 protected:
  void exchange_(const uint8_t *tx, uint8_t *rx, size_t length) {
    struct spi_ioc_transfer msg {};

    msg.tx_buf = tx;
    msg.rx_buf = rx;
    msg.len = length;
    msg.speed_hz = this->data_rate_;
    msg.bits_per_word = 8;
    msg.mode = static_cast<uint8_t>(this->mode_);
    msg.cs_change = 1;

    if (ioctl(this->fd_, SPI_IOC_MESSAGE(1), &msg) != 0) {
      ESP_LOGE(TAG, "transfer of %u byte(s) failed", static_cast<unsigned>(length));
    }
  }

  int fd_;
};

class RTEMSSPIBus : public SPIBus {
 public:
  explicit RTEMSSPIBus(int fd) : SPIBus(), fd_(fd) {}

  SPIDelegate *get_delegate(uint32_t data_rate, SPIBitOrder bit_order, SPIMode mode, GPIOPin *cs_pin,
                            bool release_device, bool write_only) override {
    (void) release_device;
    (void) write_only;
    return new RTEMSSPIDelegate(this->fd_, data_rate, bit_order, mode, cs_pin);  // NOLINT
  }

  bool is_hw() override { return true; }

 protected:
  int fd_;
};

SPIBus *SPIComponent::get_bus(SPIInterface interface, GPIOPin *clk, GPIOPin *sdo, GPIOPin *sdi,
                              const std::vector<uint8_t> &data_pins) {
  // The pins are the BSP's business on this platform, as they are for I2C:
  // which pads the controller reaches is decided when the BSP is built.
  (void) clk;
  (void) sdo;
  (void) sdi;
  (void) data_pins;

#ifdef BSP_SPI_REGISTER
  // Already there, registered by an earlier bus or by the application.
  int fd = ::open(interface, O_RDWR);
  if (fd < 0) {
    rtems_status_code sc = BSP_SPI_REGISTER(interface);
    if (sc != RTEMS_SUCCESSFUL) {
      ESP_LOGE(TAG, "Cannot register %s: %s", interface, rtems_status_text(sc));
      return nullptr;
    }
    fd = ::open(interface, O_RDWR);
  }

  if (fd < 0) {
    ESP_LOGE(TAG, "%s registered but cannot be opened", interface);
    return nullptr;
  }

  return new RTEMSSPIBus(fd);  // NOLINT(cppcoreguidelines-owning-memory)
#else
  ESP_LOGE(TAG, "This BSP has no SPI controller driver, so %s cannot be used", interface);
  return nullptr;
#endif
}

}  // namespace esphome::spi

#endif  // USE_RTEMS
