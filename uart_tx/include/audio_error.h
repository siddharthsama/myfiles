#pragma once
#ifndef _AUDIO_ERROR_H_
#define _AUDIO_ERROR_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


#ifndef __FILENAME__
#define __FILENAME__ __FILE__
#endif

#define ESP_ERR_ADF_BASE                        0x80000   /*!< Starting number of ESP-ADF error codes */

/*
 * ESP-ADF error code field start from 0x80000, end of the -1.
 * The whole area is divided into series independent modules.
 * The range of each module is 0x1000.
 * //////////////////////////////////////////////////////////
 * ESP-Audio module starting on 0x81000;
 *
 */


#define ESP_ERR_ADF_NO_ERROR                    ESP_OK
#define ESP_ERR_ADF_NO_FAIL                     ESP_FAIL

#define ESP_ERR_ADF_UNKNOWN                     ESP_ERR_ADF_BASE + 0
#define ESP_ERR_ADF_ALREADY_EXISTS              ESP_ERR_ADF_BASE + 1
#define ESP_ERR_ADF_MEMORY_LACK                 ESP_ERR_ADF_BASE + 2
#define ESP_ERR_ADF_INVALID_URI                 ESP_ERR_ADF_BASE + 3
#define ESP_ERR_ADF_INVALID_PATH                ESP_ERR_ADF_BASE + 4
#define ESP_ERR_ADF_INVALID_PARAMETER           ESP_ERR_ADF_BASE + 5
#define ESP_ERR_ADF_NOT_READY                   ESP_ERR_ADF_BASE + 6
#define ESP_ERR_ADF_NOT_SUPPORT                 ESP_ERR_ADF_BASE + 7
#define ESP_ERR_ADF_NOT_FOUND                   ESP_ERR_ADF_BASE + 8
#define ESP_ERR_ADF_TIMEOUT                     ESP_ERR_ADF_BASE + 9
#define ESP_ERR_ADF_INITIALIZED                 ESP_ERR_ADF_BASE + 10
#define ESP_ERR_ADF_UNINITIALIZED               ESP_ERR_ADF_BASE + 11



#define AUDIO_CHECK(TAG, a, action, msg) if (!(a)) {                                     \
        ESP_LOGE(TAG,"%s:%d (%s): %s", __FILENAME__, __LINE__, __FUNCTION__, msg);       \
        action;                                                                          \
        }

#define AUDIO_RET_ON_FALSE(TAG, a, action, msg) if (unlikely(a == ESP_FAIL)) {           \
        ESP_LOGE(TAG,"%s:%d (%s): %s", __FILENAME__, __LINE__, __FUNCTION__, msg);       \
        action;                                                                          \
}

#define AUDIO_MEM_CHECK(TAG, a, action)  AUDIO_CHECK(TAG, a, action, "Memory exhausted")

#define AUDIO_NULL_CHECK(TAG, a, action) AUDIO_CHECK(TAG, a, action, "Got NULL Pointer")

#define AUDIO_ERROR(TAG, str) ESP_LOGE(TAG, "%s:%d (%s): %s", __FILENAME__, __LINE__, __FUNCTION__, str)

#define ESP_EXISTS   (ESP_OK + 1)

#ifdef __cplusplus
}
#endif

#endif
