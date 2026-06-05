#pragma once

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_FREERTOS_UNICORE)
static constexpr BaseType_t AURAX_LED_TASK_CORE = 0;
#else
static constexpr BaseType_t AURAX_LED_TASK_CORE = 1;
#endif

