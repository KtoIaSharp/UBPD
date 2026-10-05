# UBPD - проект ESP-IDF.
#
# Смысл сборки: ESP-IDF 4.4 + arduino-esp32 2.0.17 как обычный компонент
# (components/arduino) + U8g2 (components/u8g2). Логика UBPD живёт в main/
# отдельными .cpp, а не склеивается в один скеч, как делает Arduino IDE.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ubpd)
