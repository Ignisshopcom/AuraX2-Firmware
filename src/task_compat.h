#pragma once

#include <Arduino.h>

static constexpr UBaseType_t AURAX_STORAGE_TASK_PRIORITY = 1;

#if defined(ARDUINO_ARCH_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_FREERTOS_UNICORE)
static constexpr BaseType_t AURAX_LED_TASK_CORE = 0;
static constexpr UBaseType_t AURAX_WIFI_TASK_PRIORITY = 3;
static constexpr UBaseType_t AURAX_PIX_TASK_PRIORITY = 1;
static constexpr UBaseType_t AURAX_EFFECT_TASK_PRIORITY = 1;
#else
static constexpr BaseType_t AURAX_LED_TASK_CORE = 1;
static constexpr UBaseType_t AURAX_WIFI_TASK_PRIORITY = 2;
static constexpr UBaseType_t AURAX_PIX_TASK_PRIORITY = 5;
static constexpr UBaseType_t AURAX_EFFECT_TASK_PRIORITY = 4;
#endif
