/*
 * coreHTTP v2.0.1
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file core_http_config_defaults.h
 * @brief HTTP 客户端库配置宏的默认值。
 *
 * @note 此文件不应被修改。如果任何配置宏需要自定义值，
 * 应向 HTTP 客户端库提供一个 core_http_config.h 文件以覆盖此文件中定义的默认值。
 * 要使用自定义配置文件，不应设置 HTTP_DO_NOT_USE_CUSTOM_CONFIG 预处理器宏。
 */

#ifndef CORE_HTTP_CONFIG_DEFAULTS_
#define CORE_HTTP_CONFIG_DEFAULTS_

/**
 * @brief 允许从服务器接收的 HTTP 头的最大大小（以字节为单位）。
 *
 * 如果从服务器接收到的 HTTP 头的总大小（以字节为单位）超过此配置，
 * 则 #HTTPClient_Send 函数将返回状态码 #HTTPSecurityAlertResponseHeadersSizeLimitExceeded。
 *
 * <b>可能的值：</b>任何正 32 位整数。<br>
 * <b>默认值：</b> `2048`
 */
#ifndef HTTP_MAX_RESPONSE_HEADERS_SIZE_BYTES
    #define HTTP_MAX_RESPONSE_HEADERS_SIZE_BYTES    2048U
#endif

/**
 * @brief HTTP 头 "User-Agent" 的值。
 *
 * 以下头行会自动写入 #HTTPRequestHeaders_t.pBuffer：
 * "User-Agent: my-platform-name\r\n"
 *
 * <b>可能的值：</b>任何字符串。<br>
 * <b>默认值：</b> `my-platform-name`
 */
#ifndef HTTP_USER_AGENT_VALUE
    #define HTTP_USER_AGENT_VALUE    "my-platform-name"
#endif

/**
 * @brief 通过 #HTTPClient_Send API 函数接收 HTTP 响应时，两次非空网络读取之间的最大持续时间。
 *
 * 传输层接收函数可能会被多次调用，直到解析器检测到响应结束。
 * 此超时表示在传入响应的网络数据接收过程中允许的最大无数据间隔。
 *
 * 如果超时到期，#HTTPClient_Send 函数将返回 #HTTPNetworkError。
 *
 * 如果 #HTTPResponse_t.getTime 设置为 NULL，则此 HTTP_RECV_RETRY_TIMEOUT_MS 将不被使用。
 * 当此超时未使用时，#HTTPClient_Send 将不会重试返回零字节读取的传输层接收调用。
 *
 * <b>可能的值：</b>任何正 32 位整数。建议使用较小的超时值。<br>
 * <b>默认值：</b> `10`
 */
#ifndef HTTP_RECV_RETRY_TIMEOUT_MS
    #define HTTP_RECV_RETRY_TIMEOUT_MS    ( 10U )
#endif

/**
 * @brief 通过 #HTTPClient_Send API 函数发送 HTTP 请求时，两次非空网络传输之间的最大持续时间。
 *
 * 发送 HTTP 请求时，传输层发送函数可能会被多次调用，直到发送完所有需要的字节数。
 * 此超时表示在通过传输层发送函数进行的网络数据传输过程中允许的最大无数据间隔。
 *
 * 如果超时到期，#HTTPClient_Send 函数将返回 #HTTPNetworkError。
 *
 * 如果 #HTTPResponse_t.getTime 设置为 NULL，则此 HTTP_SEND_RETRY_TIMEOUT_MS（原文笔误为 HTTP_RECV_RETRY_TIMEOUT_MS）将不被使用。
 * 当此超时未使用时，#HTTPClient_Send 将不会重试返回零字节发送的传输层发送调用。
 *
 * <b>可能的值：</b>任何正 32 位整数。建议使用较小的超时值。<br>
 * <b>默认值：</b> `10`
 */
#ifndef HTTP_SEND_RETRY_TIMEOUT_MS
    #define HTTP_SEND_RETRY_TIMEOUT_MS    ( 10U )
#endif

/**
 * @brief 在 HTTP 客户端库中调用，用于记录"错误"级别消息的宏。
 *
 * 要在 HTTP 客户端库中启用错误级别日志记录，此宏应映射到支持错误日志记录的特定于应用程序的日志记录实现。
 *
 * @note 此日志记录宏在 HTTP 客户端库中调用时，参数用双括号括起来，以符合 ISO C89/C90 标准。
 * 有关日志记录宏的 POSIX 实现参考，请参阅 core_http_config.h 文件以及
 * [AWS IoT Embedded C SDK 仓库](https://github.com/aws/aws-iot-device-sdk-embedded-C) 中 demos 文件夹下的 logging-stack。
 *
 * <b>默认值</b>：错误日志记录已关闭，编译时不会为 HTTP 客户端库中对此宏的调用生成代码。
 */
#ifndef LogError
    #define LogError( message )
#endif

/**
 * @brief 在 HTTP 客户端库中调用，用于记录"警告"级别消息的宏。
 *
 * 要在 HTTP 客户端库中启用警告级别日志记录，此宏应映射到支持警告日志记录的特定于应用程序的日志记录实现。
 *
 * @note 此日志记录宏在 HTTP 客户端库中调用时，参数用双括号括起来，以符合 ISO C89/C90 标准。
 * 有关日志记录宏的 POSIX 实现参考，请参阅 core_http_config.h 文件以及
 * [AWS IoT Embedded C SDK 仓库](https://github.com/aws/aws-iot-device-sdk-embedded-C) 中 demos 文件夹下的 logging-stack。
 *
 * <b>默认值</b>：警告日志已关闭，编译时不会为 HTTP 客户端库中对此宏的调用生成代码。
 */
#ifndef LogWarn
    #define LogWarn( message )
#endif

/**
 * @brief 在 HTTP 客户端库中调用，用于记录"信息"级别消息的宏。
 *
 * 要在 HTTP 客户端库中启用信息级别日志记录，此宏应映射到支持信息日志记录的特定于应用程序的日志记录实现。
 *
 * @note 此日志记录宏在 HTTP 客户端库中调用时，参数用双括号括起来，以符合 ISO C89/C90 标准。
 * 有关日志记录宏的 POSIX 实现参考，请参阅 core_http_config.h 文件以及
 * [AWS IoT Embedded C SDK 仓库](https://github.com/aws/aws-iot-device-sdk-embedded-C) 中 demos 文件夹下的 logging-stack。
 *
 * <b>默认值</b>：信息日志记录已关闭，编译时不会为 HTTP 客户端库中对此宏的调用生成代码。
 */
#ifndef LogInfo
    #define LogInfo( message )
#endif

/**
 * @brief 在 HTTP 客户端库中调用，用于记录"调试"级别消息的宏。
 *
 * 要从 HTTP 客户端库启用调试级别日志记录，此宏应映射到支持调试日志记录的特定于应用程序的日志记录实现。
 *
 * @note 此日志记录宏在 HTTP 客户端库中调用时，参数用双括号括起来，以符合 ISO C89/C90 标准。
 * 有关日志记录宏的 POSIX 实现参考，请参阅 core_http_config.h 文件以及
 * [AWS IoT Embedded C SDK 仓库](https://github.com/aws/aws-iot-device-sdk-embedded-C) 中 demos 文件夹下的 logging-stack。
 *
 * <b>默认值</b>：调试日志记录已关闭，编译时不会为 HTTP 客户端库中对此宏的调用生成代码。
 */
#ifndef LogDebug
    #define LogDebug( message )
#endif

#endif /* ifndef CORE_HTTP_CONFIG_DEFAULTS_ */
