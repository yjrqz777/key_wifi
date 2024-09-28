#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"


#include "var.h"


QueueHandle_t xQueueLed;
QueueHandle_t xQueueKey;

// tRgbKeyDef


