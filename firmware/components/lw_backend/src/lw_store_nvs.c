/* NVS transport implementation. SPDX-License-Identifier: MIT */
#include "lw_store_nvs.h"

#ifdef ESP_PLATFORM
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "lw_nvs";

static bool s_probed;
static lw_nvs_state_t s_state = LW_NVS_UNAVAILABLE_ERROR;
static const char *s_reason = "not initialised";
static esp_err_t s_last_error;

lw_nvs_state_t lw_store_nvs_begin(void)
{
    if (s_probed) {
        return s_state;
    }
    s_probed = true;
    esp_err_t err = nvs_flash_init();
    switch (err) {
    case ESP_OK:
        s_state = LW_NVS_READY;
        s_reason = NULL;
        break;
    case ESP_ERR_NVS_NO_FREE_PAGES:
        /* The partition may hold the only copy of the pet and anniversary
         * saves. Refuse to erase; run with defaults for this boot instead. */
        s_state = LW_NVS_UNAVAILABLE_NO_FREE_PAGES;
        s_reason = "nvs partition has no free pages; erase not attempted";
        break;
    case ESP_ERR_NVS_NEW_VERSION_FOUND:
        s_state = LW_NVS_UNAVAILABLE_NEW_VERSION;
        s_reason = "nvs written by a different IDF version; erase not attempted";
        break;
    default:
        s_state = LW_NVS_UNAVAILABLE_ERROR;
        s_reason = esp_err_to_name(err);
        break;
    }
    s_last_error = err;
    if (s_state == LW_NVS_READY) {
        ESP_LOGI(TAG, "NVS ready");
    } else {
        ESP_LOGW(TAG, "NVS unavailable: %s (data preserved)", s_reason ? s_reason : "unknown");
    }
    return s_state;
}

lw_nvs_state_t lw_store_nvs_state(void)
{
    return s_state;
}

const char *lw_store_nvs_reason(void)
{
    return s_reason;
}

esp_err_t lw_store_nvs_last_error(void)
{
    return s_last_error;
}

static bool nvs_read(void *context, uint8_t *out, size_t capacity, size_t *length)
{
    const lw_store_nvs_key_t *key = (const lw_store_nvs_key_t *)context;
    if (key == NULL || out == NULL || length == NULL || s_state != LW_NVS_READY) {
        return false;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(key->namespace_name, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }
    size_t size = capacity;
    err = nvs_get_blob(handle, key->key, out, &size);
    nvs_close(handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            s_last_error = err;
        }
        return false;
    }
    *length = size;
    return true;
}

static bool nvs_write(void *context, const uint8_t *data, size_t length)
{
    const lw_store_nvs_key_t *key = (const lw_store_nvs_key_t *)context;
    if (key == NULL || data == NULL || s_state != LW_NVS_READY) {
        return false;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(key->namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        s_last_error = err;
        return false;
    }
    err = nvs_set_blob(handle, key->key, data, length);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        s_last_error = err;
        return false;
    }
    return true;
}

static lw_store_io_t s_io;

const lw_store_io_t *lw_store_nvs_io(const lw_store_nvs_key_t *key)
{
    s_io.context = (void *)key;
    s_io.read = nvs_read;
    s_io.write = nvs_write;
    return &s_io;
}

#endif /* ESP_PLATFORM */
