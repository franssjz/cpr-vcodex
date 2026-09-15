#include "version.h"

#include "version.generated.inc"

#ifndef SIMULATOR
#include <esp_app_desc.h>
#include <esp_idf_version.h>
#include <sdkconfig.h>

#define CPR_STRINGIFY_IMPL(value) #value
#define CPR_STRINGIFY(value) CPR_STRINGIFY_IMPL(value)

// ESP-IDF provides a weak descriptor in its cached core archive. Own the
// descriptor here so an incremental build cannot inherit an older app version.
// Keep the boot compatibility fields from the selected SDK configuration.
extern "C" const esp_app_desc_t esp_app_desc __attribute__((section(".rodata_desc"), used)) = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
#ifdef CONFIG_BOOTLOADER_APP_SECURE_VERSION
    .secure_version = CONFIG_BOOTLOADER_APP_SECURE_VERSION,
#else
    .secure_version = 0,
#endif
    .reserv1 = {},
    .version = CPR_BUILD_VERSION_STRING,
    .project_name = "cpr-vcodex",
#ifdef CONFIG_APP_COMPILE_TIME_DATE
    .time = __TIME__,
    .date = __DATE__,
#else
    .time = "",
    .date = "",
#endif
    .idf_ver = CPR_STRINGIFY(ESP_IDF_VERSION_MAJOR) "." CPR_STRINGIFY(ESP_IDF_VERSION_MINOR) "." CPR_STRINGIFY(
        ESP_IDF_VERSION_PATCH),
    .app_elf_sha256 = {},  // Filled by esptool during ELF-to-BIN conversion.
    .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
    .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
    .mmu_page_size = 31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE),
    .reserv3 = {},
    .reserv2 = {},
};
static_assert(sizeof(CPR_BUILD_VERSION_STRING) <= sizeof(esp_app_desc.version), "Firmware version exceeds descriptor");
#endif
