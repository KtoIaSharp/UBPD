# UBPD на ESP-IDF

Тот же прибор UBPD, но собранный **ESP-IDF**, а не Arduino IDE. Логика та же:
экран, радар, BLE-скан, классический Bluetooth, WATCH, WEB UI.

## Зачем

* `esp_http_server` вместо Arduino-класса `WebServer`, `nvs` вместо `Preferences`,
  `esp_wifi` + `esp_netif` вместо Arduino-класса `WiFi`, LEDC вместо `tone()` —
  то есть «спорные» места перешли на чистый ESP-IDF, а не на обёртки Arduino.
* `arduino-esp32 2.0.17` подключён **как обычный компонент** (`components/arduino`),
  поэтому ядро (`Wire`, `Serial`, `millis()`, классы BLE) осталось прежним и
  поведение прибора не меняется.
* Сборка через `idf.py`, размер образа **1.47 МБ вместо 1.72 МБ** (флаг `-Os`),
  доступен `menuconfig`: можно выключить ненужные профили Bluetooth и урезать стек.

## Состав

Проект лежит прямо в этой папке; старый Arduino-скетч убран в `legacy 0.4/`.

```
BLE/                        <- корень проекта ESP-IDF (путь должен быть латиницей)
  CMakeLists.txt        проект IDF
  sdkconfig.defaults    Arduino-автозапуск, тик 1000 Гц, BTDM, huge_app, -Os
  partitions.csv        схема huge_app (3 МБ на приложение)
  legacy 0.4/           прежняя Arduino-версия (.ino + config.h/companies.h/web_page.h)
  components/
    arduino/            arduino-esp32 2.0.17 (cores, libraries, variants + CMakeLists)
    u8g2/               U8g2 2.37.1 (в библиотеке уже есть готовый CMakeLists)
  main/
    CMakeLists.txt      список .cpp
    app_main.cpp        setup()/loop() (бывший BLE.ino)
    config.h            пины, флаги, типы; подключает globals.h и ubpd.h
    globals.h/.cpp      общие переменные (extern + одно определение)
    ubpd.h              объявления функций между модулями
    wifi.cpp            ESP-IDF: esp_wifi + esp_netif (скан, точка доступа)
    web.cpp             ESP-IDF: esp_http_server (страница, API, CSV)
    storage.cpp         ESP-IDF: nvs (настройки, избранное)
    buzzer.cpp          ESP-IDF: driver/ledc + driver/gpio
    btclassic.cpp       классический Bluetooth (esp_bt_gap), как было
    display.cpp scanner.cpp radar.cpp sleep.cpp ui.cpp hardware.cpp
    companies.h web_page.h
```

## Что перенесено на чистый ESP-IDF

| Было (Arduino) | Стало (ESP-IDF) |
|---|---|
| `Preferences` (обёртка над NVS) | `nvs_open/nvs_set_*/nvs_get_*`, `nvs_flash_init`; пространство имён `ubpd` то же, настройки не теряются |
| `WebServer` (`webServer.on/send/hasArg`) | `esp_http_server`: `httpd_start`, `httpd_uri_t`, `httpd_query_key_value`, `httpd_resp_send` |
| `WiFi` (класс), `WiFi.scanNetworks`, `WiFi.softAP` | `esp_wifi` + `esp_netif`, события `WIFI_EVENT_SCAN_DONE`, режим `APSTA` |
| `tone()` / `noTone()` | `driver/ledc`: явный таймер и канал, `ledc_set_freq/ledc_set_duty` |
| `pinMode/digitalWrite` для пищалки | `driver/gpio`: `gpio_set_direction`, `gpio_set_level` |

## Что осознанно осталось на Arduino

* **BLE-скан** (`BLEDevice`, `BLEAdvertisedDevice`). Ядро BLE в Arduino — это тонкая
  обёртка над тем же `esp_ble_gap`, но переписывание разбора adv-отчёта ничего не
  улучшит, зато рискует сломать рабочее. Перенос — отдельный шаг.
* `Wire` (I2C для OLED), `Serial`, `millis()`, `delay()`, `analogReadMilliVolts`.
  Это примитивы ядра, дублировать их на IDF смысла нет.
* **U8g2** как есть: в библиотеке уже есть IDF-CMakeLists, менять код отрисовки не нужно.
* Классический Bluetooth уже был на `esp_bt_gap` — просто переехал в свой `.cpp`.

## Сборка

Нужны: ESP-IDF v4.4.7 (`C:/esp/esp-idf`), Python 3.11 (`C:/py311`), cmake, ninja,
xtensa-esp32-elf (ставится `install.bat esp32`).

**Путь обязан быть латиницей.** ESP-IDF ломается на кириллице: `kconfiglib` получает
путь в неверной кодировке и падает с `kconfigs.in not found`. Поэтому рабочая папка —
`C:\Users\user\Desktop\FakeWiFi\BLE` (кириллическое имя родительской папки пришлось
сменить). Сборка из пути с русскими буквами даёт:

```
FileNotFoundError: 'C:/Users/user/Desktop/╨д╨╡╨╣╨║ ╨▓╨╕╤Д╨╕/BLE/build/kconfigs.in'
```

```cmd
C:\esp\esp-idf\export.bat
cd /d "C:\Users\user\Desktop\FakeWiFi\BLE"
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor      (выход: Ctrl+])
```

Сборка «с нуля» ~10 минут, дальше инкрементально. `ccache` (уже в тулчейне) ускоряет
повторные сборки. `build/` и `sdkconfig` в git не попадают.

## Прототипы функций

Arduino сам генерировал прототипы (ctags) и вставлял их в склеенный скеч. Здесь так
нельзя: каждый `.cpp` компилируется отдельно. Объявления лежат в `main/ubpd.h`.
**Если добавил функцию и вызываешь её из другого файла — допиши её в `ubpd.h`.**

## Проверено на железе (COM3, ESP32-D0WD-V3)

```
I (713) cpu_start: cpu freq: 240000000
=== UBPD v0.4.0 ===
[i] Display: 128x64 rows=4
[i] NVS: имя idx=2, радар=BLE, избранных=1        <- настройки от Arduino-сборки на месте
I (751) BTDM_INIT: BT controller compile version [0f0c5a2]
[i] CLASSIC: поиск (inquiry) запущен
[i] CLASSIC устройств: 1
[i] CLASSIC: спрашиваю имя у 1 устройств
I wifi:mode : sta (58:2a:bd:d8:41:b4) + softAP (58:2a:bd:d8:41:b5)
[i] WEB UI: SSID "UBPD-41B4", http://192.168.4.1/ (открытая)
```

То есть проверены именно те части, которые переехали на IDF: NVS (настройки читаются),
классический Bluetooth (поиск идёт, устройство найдено), Wi-Fi + точка доступа и
`esp_http_server` (сервер отдаёт страницу на 192.168.4.1).

Не проверено вживую: ответы `/api/*` — для этого нужен клиент в сети `UBPD-41B4`
(подключить к точке доступа и открыть `http://192.168.4.1/`).

## Грабли, найденные при переносе

* **`-Werror=all`.** IDF строже Arduino: `snprintf` в 26-байтный буфер экрана с
  кириллицей даёт `-Wformat-truncation`. Для `main` этот флаг ослаблен
  (`-Wno-error=format-truncation`) — обрезка подписей для OLED так и задумана.
* **`HTTP_ANY` в IDF 4.4 нет.** Каждый обработчик регистрируется дважды: на GET и POST.
* **`wifi_start()`** — это функция Arduino, в IDF нужен `esp_wifi_start()`.
* **`esp_wifi_init` вызывается один раз**: инициализация Wi-Fi собрана в `wifiEnsure()`,
  поэтому сканер и точка доступа не дерутся за радио.
* Заголовки `esp_bt.h`/`esp_gap_bt_api.h` подключены в `config.h`, иначе прототип
  `classicGapCb` не видит типы `esp_bt_gap_*` (порядок include-ов в IDF-сборке другой,
  чем в склейке Arduino).
