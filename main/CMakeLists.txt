# main - сама логика UBPD. Один .cpp на модуль (раньше были .ino,
# которые Arduino склеивал в один файл и сам расставлял прототипы).
idf_component_register(
    SRCS
        "app_main.cpp"
        "btclassic.cpp"
        "buzzer.cpp"
        "display.cpp"
        "globals.cpp"
        "hardware.cpp"
        "radar.cpp"
        "scanner.cpp"
        "sleep.cpp"
        "storage.cpp"
        "ui.cpp"
        "web.cpp"
        "wifi.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        arduino
        u8g2
        bt
        nvs_flash
        esp_http_server
        esp_wifi
        driver
)

# ESP-IDF включает -Werror=all, и компилятор ругается на "snprintf может обрезать
# строку" там, где обрезка и есть задумка: подписи для OLED (128x64, шрифт 5x7)
# живут в 26-байтных буферах, а кириллица занимает 2 байта на символ.
# В Arduino-сборке этого флага не было, поэтому поведение не менялось.
# Оставляем предупреждение видимым, но не превращаем его в ошибку.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format-truncation)
