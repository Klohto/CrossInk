#pragma once
#include "FreeRTOS.h"

using SemaphoreHandle_t = void*;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t mutex, TickType_t wait);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t mutex);
