// The flash chip driver list that makes CONFIG_SPI_FLASH_AUTO_SUSPEND usable on the T-RGB's W25Q128 lives in
// components/gm_flash_chips/flash_chips.c, where pioarduino's library pass compiles it into the rebuilt IDF
// libraries. PlatformIO also compiles that directory for the app, but as an archive that precedes the IDF libraries
// on the link line, so the linker never pulls the definition libspi_flash needs. Compiling the same source here makes
// it a plain object, which is linked unconditionally; the archive member is then never selected, so there is no
// duplicate. Compiled out in the sim and in every env whose sdkconfig does not set
// CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST.
#if !defined(GAGGIMATE_SIM)
extern "C" {
#include "../../../components/gm_flash_chips/flash_chips.c"
}
#endif
