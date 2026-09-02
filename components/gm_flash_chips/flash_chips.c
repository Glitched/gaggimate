// Flash chip driver list for the display build: the stock ESP-IDF list, with a driver in front of it that declares
// the Winbond W25Q128 (JEDEC id 0xEF4018, the chip on the LilyGo T-RGB) capable of erase/program suspend.
//
// Why: with CONFIG_SPI_FLASH_AUTO_SUSPEND the flash driver keeps the cache enabled during erases and page writes and
// lets the hardware suspend the flash operation whenever the cache or the LCD DMA needs the bus. Without it every
// shot-log write and every OTA write disables the cache for milliseconds, PSRAM becomes unreachable, the RGB panel's
// bounce buffers are not refilled and the picture repeats rows (see CLAUDE.md, "The panel needs bounce buffers").
// IDF 5.5 only whitelists the W25Q64 (0xEF4017) in its Winbond driver; the W25Q128 has the same suspend/resume
// commands (0x75/0x7A, SUS bit in status register 2) and tSUS <= 20 us, under CONFIG_SPI_FLASH_SUSPEND_TSUS_VAL_US.
//
// Safety: esp_flash_init_default_chip() asserts at boot when the option is on and the chip has no suspend capability,
// so an unlisted chip never runs this image. The Arduino 2 bootloader users have and ours both enable app rollback, so
// that boot failure rolls the device back to its previous firmware.
//
// CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST=y removes IDF's own list, so this file must name every stock driver too.
// pioarduino compiles this directory twice: in its IDF library pass (where libspi_flash needs the symbol) and, like
// PlatformIO does with any project components/ directory, into the app for the final link.
#if defined(ESP_PLATFORM) // never the simulator
#include "sdkconfig.h"
#endif
#if defined(CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST) && CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST

#include <stddef.h>
#include <stdint.h>

#include "spi_flash_chip_boya.h"
#include "spi_flash_chip_driver.h"
#include "spi_flash_chip_gd.h"
#include "spi_flash_chip_generic.h"
#include "spi_flash_chip_issi.h"
#include "spi_flash_chip_mxic.h"
#include "spi_flash_chip_th.h"
#include "spi_flash_chip_winbond.h"

// Its header is not shipped in the Arduino libs package's include tree; the driver itself is compiled and linked.
extern const spi_flash_chip_t esp_flash_chip_mxic_opi;

#define W25Q128_JEDEC_ID 0xEF4018u

static esp_err_t gm_probe_w25q128(esp_flash_t *chip, uint32_t flash_id) {
    (void)chip;
    return flash_id == W25Q128_JEDEC_ID ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static spi_flash_caps_t gm_caps_w25q128(esp_flash_t *chip) {
    (void)chip;
    return (spi_flash_caps_t)(SPI_FLASH_CHIP_CAP_SUSPEND | SPI_FLASH_CHIP_CAP_UNIQUE_ID); // enum | enum is int in C++
}

// The Winbond driver's own entry points only add 4-byte addressing for parts above 16 MB, so for this 16 MB chip the
// generic ones are equivalent. Field order follows spi_flash_chip_t; the suspend command set is the generic one, which
// is the same as IDF's Winbond one (RDSR2, 0x75, 0x7A, mask 0x80).
static const spi_flash_chip_t gm_w25q128_with_suspend = {
    .name = "winbond-w25q128-suspend",
    .timeout = &spi_flash_chip_generic_timeout,
    .probe = gm_probe_w25q128,
    .reset = spi_flash_chip_generic_reset,
    .detect_size = spi_flash_chip_generic_detect_size,
    .erase_chip = spi_flash_chip_generic_erase_chip,
    .erase_sector = spi_flash_chip_generic_erase_sector,
    .erase_block = spi_flash_chip_generic_erase_block,
    .sector_size = 4 * 1024,
    .block_erase_size = 64 * 1024,
    .get_chip_write_protect = spi_flash_chip_generic_get_write_protect,
    .set_chip_write_protect = spi_flash_chip_generic_set_write_protect,
    .num_protectable_regions = 0,
    .protectable_regions = NULL,
    .get_protected_regions = NULL,
    .set_protected_regions = NULL,
    .read = spi_flash_chip_generic_read,
    .write = spi_flash_chip_generic_write,
    .program_page = spi_flash_chip_generic_page_program,
    .page_size = 256,
    .write_encrypted = spi_flash_chip_generic_write_encrypted,
    .wait_idle = spi_flash_chip_generic_wait_idle,
    .set_io_mode = spi_flash_chip_generic_set_io_mode,
    .get_io_mode = spi_flash_chip_generic_get_io_mode,
    .read_id = NULL,
    .read_reg = spi_flash_chip_generic_read_reg,
    .yield = spi_flash_chip_generic_yield,
    .sus_setup = spi_flash_chip_generic_suspend_cmd_conf,
    .read_unique_id = spi_flash_chip_generic_read_unique_id,
    .get_chip_caps = gm_caps_w25q128,
    .config_host_io_mode = spi_flash_chip_generic_config_host_io_mode,
};

// Same order as IDF's spi_flash_chip_drivers.c, with ours first so it wins the probe for 0xEF4018.
const spi_flash_chip_t *default_registered_chips[] = {
    &gm_w25q128_with_suspend, &esp_flash_chip_issi, &esp_flash_chip_gd,       &esp_flash_chip_mxic,    &esp_flash_chip_winbond,
    &esp_flash_chip_boya,     &esp_flash_chip_th,   &esp_flash_chip_mxic_opi, &esp_flash_chip_generic, NULL,
};

#endif // CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST
