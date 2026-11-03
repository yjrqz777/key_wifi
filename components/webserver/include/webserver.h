/**
 * @file webserver.h
 * @brief webserver 组件的公共接口：HTTP 服务器启动入口。
 * @details 网页、串口日志、颜色控制和 Wi-Fi 模式切换的 URI 处理器都在本组件内注册。
 */
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_http_server.h"

/**
 * @brief 启动 HTTP 服务器并注册全部 URI 处理器。
 * @return 启动成功时返回服务器句柄；httpd_start() 失败时返回 NULL。
 */
httpd_handle_t start_webserver(void);

#endif // WEBSERVER_H
