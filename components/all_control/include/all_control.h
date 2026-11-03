/**
 * @file all_control.h
 * @brief all_control 组件的对外接口：设备启动入口和本组件内部的任务入口。
 * @details 本组件只做任务编排，功能实现分别位于 wifi、ws2812、myusb、easy_at 组件中。
 */
#ifndef ALL_CONTROL_H
#define ALL_CONTROL_H

/**
 * @brief 初始化各组件资源并启动设备全部功能任务。
 * @param[in] argc 未使用的兼容性参数数量。
 * @param[in] argv 未使用的兼容性参数数组。
 */
void all_control_main(int argc, int *argv);

/**
 * @brief 按键采集任务入口，由 all_control_main() 创建。
 * @param[in] pvParameters 未使用的 FreeRTOS 任务参数。
 */
void key_task(void *pvParameters);

#endif // ALL_CONTROL_H
