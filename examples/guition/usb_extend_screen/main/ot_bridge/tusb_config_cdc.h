/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "sdkconfig.h"

#ifdef CONFIG_OT_BRIDGE_ENABLE

/* TinyUSB CDC ACM class — one instance for the OT bridge */
#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_RX_BUFSIZE    512
#define CFG_TUD_CDC_TX_BUFSIZE    512

#else

#define CFG_TUD_CDC               0
#define CFG_TUD_CDC_RX_BUFSIZE    0
#define CFG_TUD_CDC_TX_BUFSIZE    0

#endif /* CONFIG_OT_BRIDGE_ENABLE */

#ifdef __cplusplus
}
#endif
