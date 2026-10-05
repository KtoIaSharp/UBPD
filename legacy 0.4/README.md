# legacy 0.4 — прежняя Arduino-версия UBPD

Это Arduino-скетч UBPD v0.4.0: то же поведение, что и у текущей сборки на ESP-IDF,
но собранное Arduino IDE / `arduino-cli`. Оставлен как **эталон логики** и на случай,
если понадобится быстрый способ залить прошивку без IDF.

## Почему именно «legacy»

Основная сборка теперь ESP-IDF (см. `../README-IDF.md`): служебные части — NVS,
WEB UI, Wi-Fi, пищалка — переведены на чистый esp-idf. Смысл прибора там тот же,
поэтому Arduino-версия больше не развивается.

## Сборка

Arduino требует, чтобы имя папки скетча совпадало с именем главного `.ino`, а папка
называется `legacy 0.4` — поэтому собираем через копию в латинской папке (так же,
как раньше):

```cmd
mkdir C:\BLE
copy "C:\Users\user\Desktop\FakeWiFi\BLE\legacy 0.4\*" C:\BLE\

"C:\Users\user\Desktop\FakeWiFi\_tools\cli\arduino-cli.exe" compile ^
  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app C:\BLE

"C:\Users\user\Desktop\FakeWiFi\_tools\cli\arduino-cli.exe" upload ^
  -p COM3 --fqbn esp32:esp32:esp32:PartitionScheme=huge_app C:\BLE
```

Готовый скрипт: `..\_tools\build_legacy.cmd`.

## Важно

* Настройки в NVS (namespace `ubpd`) **общие** с IDF-сборкой: переключение между
  прошивками не сбрасывает избранное и настройки.
* Правки логики делай здесь, потом переноси в `../main/*.cpp` — иначе версии разъедутся.
* Файлы: `BLE.ino`, `btclassic.ino`, `buzzer.ino`, `display.ino`, `hardware.ino`,
  `radar.ino`, `scanner.ino`, `sleep.ino`, `storage.ino`, `ui.ino`, `web.ino`,
  `config.h`, `companies.h`, `web_page.h`.
