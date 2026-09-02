# Project component compiled by pioarduino's hybrid (custom_sdkconfig) library pass: ESP-IDF picks up the project's
# components/ directory on its own, so the flash chip driver list below is linked into the rebuilt IDF libraries where
# libspi_flash needs it. PlatformIO also compiles this directory into the app, which covers the final Arduino link.
# Everything is compiled out unless
# CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST is set (the display env only).
idf_component_register(SRCS "flash_chips.c" REQUIRES spi_flash)
