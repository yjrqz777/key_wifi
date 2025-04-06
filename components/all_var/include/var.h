#ifndef VAR_H
#define VAR_H
#include "freertos/semphr.h"




// 定义事件位（最多可用24位）
#define SAT_EN  (1 << 0)
#define AP_EN   (1 << 1)



typedef struct tRgbKeyDef
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t k;
} tRgbKeyDef;


extern QueueHandle_t xQueueLed;
extern QueueHandle_t xQueueKey;










extern void vat_task(void);

#endif
