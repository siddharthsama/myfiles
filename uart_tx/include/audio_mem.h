#pragma once

#ifndef _AUDIO_MEM_H_
#define _AUDIO_MEM_H_

#include <esp_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Malloc memory in ADF
 *
 * @param[in]  size   memory size
 *
 * @return
 *     - valid pointer on success
 *     - NULL when any errors
 */
void *audio_malloc(size_t size);

/**
 * @brief  Malloc an aligned chunk of memory in ADF
 *
 * @param[in]  alignment  How the pointer received needs to be aligned
 *                        must be a power of two
 * @param[in]  size       memory size
 *
 * @return
 *     - valid pointer on success
 *     - NULL when any errors
 */
void *audio_malloc_align(size_t alignment, size_t size);

/**
 * @brief  Free memory in ADF
 *
 * @param[in]  ptr  memory pointer
 */
void audio_free(void *ptr);

/**
 * @brief  Malloc memory in ADF, if spi ram is enabled, it will malloc memory in the spi ram
 *
 * @param[in]  nmemb   number of block
 * @param[in]  size    block memory size
 *
 * @return
 *     - valid pointer on success
 *     - NULL when any errors
 */
void *audio_calloc(size_t nmemb, size_t size);

/**
 * @brief   Malloc memory in ADF, it will malloc to internal memory
 *
 * @param[in] nmemb   number of block
 * @param[in]  size   block memory size
 *
 * @return
 *     - valid pointer on success
 *     - NULL when any errors
 */
void *audio_calloc_inner(size_t nmemb, size_t size);

/**
 * @brief  Print heap memory status
 *
 * @param[in]  tag   tag of log
 * @param[in]  line  line of log
 * @param[in]  func  function name of log
 */
void audio_mem_print(const char *tag, int line, const char *func);

/**
 * @brief  Reallocate memory in ADF, if spi ram is enabled, it will allocate memory in the spi ram
 *
 * @param[in]  ptr   memory pointer
 * @param[in]  size  block memory size
 *
 * @return
 *     - valid pointer on success
 *     - NULL when any errors
 */
void *audio_realloc(void *ptr, size_t size);

/**
 * @brief   Duplicate given string.
 *
 *          Allocate new memory, copy contents of given string into it and return the pointer
 *
 * @param[in]  str   String to be duplicated
 *
 * @return
 *     - Pointer to new malloc'ed string
 *     - NULL otherwise
 */
char *audio_strdup(const char *str);

/**
 * @brief   SPI ram is enabled or not
 *
 * @return
 *     - true, spi ram is enabled
 *     - false, spi ram is not enabled
 */
bool audio_mem_spiram_is_enabled(void);

/**
 * @brief   Stack on external SPI ram is enabled or not
 *
 * @return
 *     - true, stack on spi ram is enabled
 *     - false, stack on spi ram is not enabled
 */
bool audio_mem_spiram_stack_is_enabled(void);

#define AUDIO_MEM_SHOW(x)  audio_mem_print(x, __LINE__, __func__)

#define AUDIO_SAFE_FREE(ptr, free_fn) do {   \
    if (ptr && free_fn != NULL ) {           \
        free_fn(ptr);                        \
        ptr = NULL;                          \
    }                                        \
} while (0)

#ifdef __cplusplus
}
#endif

#endif /*_AUDIO_MEM_H_*/
