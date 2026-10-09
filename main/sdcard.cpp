#include "config.h"

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

// ==================== SD-КАРТА ====================
// Модуль microSD по SPI. Наши пины: CS=15, MOSI=23, MISO=19, SCK=18
// (питание модуля -> пин "3.3", GND -> GND, перемычка модуля на 3V3).
// VSPI (SPI2) свободен: OLED на I2C, кнопки/LED/бузер - обычные GPIO.
//
// Монтируем FAT32 при старте, создаём структуру /UBPD/... и печатаем листинг.
// ВАЖНО: карта должна быть честной FAT32 (не exFAT) и не поддельной - на "левых"
// 64-ГБ картах запись в часть областей молча не проходит (mkdir возвращает успех,
// а каталога нет). Проверяется через sdListDir после создания.

#define SD_PIN_CS 15
#define SD_PIN_MOSI 23
#define SD_PIN_MISO 19
#define SD_PIN_SCK 18
#define SD_MOUNT "/sdcard"

static sdmmc_card_t *s_card = nullptr;
static bool s_mounted = false;
static char s_info[32] = "SD нет";

bool sdMounted() { return s_mounted; }
const char *sdCardInfo() { return s_info; }

// Печатает содержимое каталога (до 24 записей).
void sdListDir(const char *path) {
  DIR *d = opendir(path);
  if (!d) {
    Serial.printf("[i] SD: нет каталога %s\n", path);
    return;
  }
  Serial.printf("[i] SD: %s\n", path);
  struct dirent *e;
  int n = 0;
  while ((e = readdir(d)) != nullptr && n < 24) {
    if (e->d_name[0] == '.') continue;
    Serial.printf("     %s%s\n", e->d_name, (e->d_type == DT_DIR) ? "/" : "");
    n++;
  }
  closedir(d);
}

// Создаёт /UBPD/{sounds,logs,scripts,ble_names} и показывает листинг.
void sdEnsureLayout() {
  if (!s_mounted) return;
  mkdir(SD_MOUNT "/UBPD", 0775);
  mkdir(SD_MOUNT "/UBPD/sounds", 0775);
  mkdir(SD_MOUNT "/UBPD/logs", 0775);
  mkdir(SD_MOUNT "/UBPD/scripts", 0775);
  mkdir(SD_MOUNT "/UBPD/ble_names", 0775);
  sdListDir(SD_MOUNT "/UBPD");
}

void sdInit() {
  esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
  mount_cfg.format_if_mount_failed = false;   // карту НЕ форматируем
  mount_cfg.max_files = 5;
  mount_cfg.allocation_unit_size = 16 * 1024;

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  // Скорость подбираем: для стриминга звука (176 КБ/с) нужно ≥ 4 МГц, но на
  // макетке/длинных проводах надёжнее ниже. Пробуем от быстрого к медленному.
  static const int freqLadder[4] = {20000, 10000, 4000, 1000};

  spi_bus_config_t bus = {};
  bus.mosi_io_num = SD_PIN_MOSI;
  bus.miso_io_num = SD_PIN_MISO;
  bus.sclk_io_num = SD_PIN_SCK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 4000;

  esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus, SDSPI_DEFAULT_DMA);
  if (ret != ESP_OK) {
    snprintf(s_info, sizeof(s_info), "SPI busy");
    Serial.printf("[!] SD: spi_bus_initialize failed (%d)\n", (int)ret);
    return;
  }
  gpio_set_pull_mode((gpio_num_t)SD_PIN_MISO, GPIO_PULLUP_ONLY);

  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.gpio_cs = (gpio_num_t)SD_PIN_CS;
  slot.host_id = (spi_host_device_t)host.slot;

  ret = ESP_FAIL;   // сброс: иначе здесь остался бы результат spi_bus_initialize (OK)
  for (size_t fi = 0; fi < sizeof(freqLadder) / sizeof(freqLadder[0]) && ret != ESP_OK; fi++) {
    host.max_freq_khz = freqLadder[fi];
    for (int attempt = 1; attempt <= 2; attempt++) {
      ret = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &mount_cfg, &s_card);
      if (ret == ESP_OK) break;
      s_card = nullptr;
      vTaskDelay(pdMS_TO_TICKS(120));
    }
    if (ret == ESP_OK) {
      Serial.printf("[i] SD: смонтирована на %d кГц\n", freqLadder[fi]);
    } else {
      Serial.printf("[i] SD: %d кГц не вышло (%d)\n", freqLadder[fi], (int)ret);
    }
  }

  if (ret != ESP_OK) {
    snprintf(s_info, sizeof(s_info), "SD нет");
    spi_bus_free((spi_host_device_t)host.slot);
    s_card = nullptr;
    gpio_set_direction((gpio_num_t)SD_PIN_MISO, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)SD_PIN_MISO, GPIO_FLOATING);
    Serial.printf("[i] SD-диаг: CS=%d MOSI=%d MISO=%d SCK=%d, уровень MISO=%d\n",
                  SD_PIN_CS, SD_PIN_MOSI, SD_PIN_MISO, SD_PIN_SCK,
                  gpio_get_level((gpio_num_t)SD_PIN_MISO));
    return;
  }

  s_mounted = true;

  uint64_t mb = ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) / (1024ULL * 1024ULL);
  snprintf(s_info, sizeof(s_info), "SD %lluMB", (unsigned long long)mb);

  Serial.printf("[i] SD: OK, имя=\"%s\", %llu МБ, сектор=%u Б\n",
                s_card->cid.name, (unsigned long long)mb, (unsigned)s_card->csd.sector_size);

  sdEnsureLayout();
}
