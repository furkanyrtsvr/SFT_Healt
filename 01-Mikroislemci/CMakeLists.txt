cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(OLED_Merhaba)

target_sources(app PRIVATE
    src/main.c
    src/display_ui.c
    src/input_encoder.c
    src/max30102.c
    src/heart_rate.c
    src/profile_store.c
    src/pressure_sensor.c
    src/mep_test.c
    src/ble_health.c
    src/environment_sensor.c
    src/rtc_clock.c
    src/measurement_log.c
    src/storage_reset.c
)
