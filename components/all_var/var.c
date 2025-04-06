#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"


#include "var.h"


QueueHandle_t xQueueLed;
QueueHandle_t xQueueKey;



EventGroupHandle_t xGlobalEventGroup = NULL;
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
void event_group_init(void)
{
    // 创建事件组（最多支持24个事件位）
    xGlobalEventGroup = xEventGroupCreate();
    
    if(xGlobalEventGroup == NULL) {
        ESP_LOGE("EVENT_GROUP", "Failed to create event group!");
        return;
    }
    
    ESP_LOGI("EVENT_GROUP", "Event group created successfully");
}

/***************************************************************************************************
 * 功能描述: 获取句柄接口
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
EventGroupHandle_t get_event_group(void)
{
    return xGlobalEventGroup;
}
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
void vat_task(void)
{
    event_group_init();


    while (true) 
    {
        // ESP_LOGI(TAG, "vat_task run");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

