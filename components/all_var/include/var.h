#ifndef VAR_H
#define VAR_H
#include "freertos/semphr.h"

typedef struct tRgbKeyDef
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t k;
} tRgbKeyDef;


extern QueueHandle_t xQueueLed;
extern QueueHandle_t xQueueKey;



#endif
