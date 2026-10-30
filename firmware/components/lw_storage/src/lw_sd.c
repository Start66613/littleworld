#ifdef ESP_PLATFORM
#include "lw_sd.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#ifdef CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL
#error "Little World refuses a build that can auto-format an SD card"
#endif

static const char *TAG = "lw_sd";
static bool owned;

/*
 * A persistent DMA-capable staging buffer for the SDMMC driver.
 *
 * Without it the driver allocates one on every unaligned multi-block transfer
 * (components/sdmmc/sdmmc_cmd.c allocate_dma_buf()) and halves the request
 * until MALLOC_CAP_DMA succeeds, failing the whole read with ESP_ERR_NO_MEM
 * once even one sector does not fit. On this board that showed up on the card
 * as "E sdmmc_cmd: allocate_dma_buf: not enough mem, err=0x101" plus
 * "E diskio_sdmmc: sdmmc_read_blocks failed (0x101)" after pages had been
 * opened a few times: internal RAM still reported ~19 MB free but the largest
 * contiguous internal block had dropped to ~30 KB.
 *
 * Reserving the chunk once at mount (before the UI fragments the heap) removes
 * the runtime allocation entirely. 64 KiB covers the driver's 16-block chunk
 * even for a 4 KiB logical sector, and the driver clamps it further through
 * unaligned_multi_block_rw_max_chunk_size.
 */
#define LW_SD_DMA_CHUNK_MAX (64u * 1024u)
#define LW_SD_DMA_CHUNK_MIN (8u * 1024u)
static void *s_dma_staging;
static size_t s_dma_staging_bytes;

esp_err_t lw_sd_mount_existing(void)
{
    if (owned || bsp_sdcard) return ESP_ERR_INVALID_STATE;
    esp_err_t result = bsp_sdcard_mount();
    if (result != ESP_OK) return result;

    /* Take the largest chunk that internal DMA-capable RAM can still spare.
     * The driver only needs one sector, so shrinking costs extra transfers
     * rather than correctness. */
    for (size_t want = LW_SD_DMA_CHUNK_MAX; want >= LW_SD_DMA_CHUNK_MIN; want /= 2u) {
        s_dma_staging = heap_caps_malloc(want, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (s_dma_staging) { s_dma_staging_bytes = want; break; }
    }
    /* Internal RAM shrank to 16 KiB by the time the card mounts, and every
     * transfer through a small bounce buffer is a separate SDMMC transaction:
     * the measured cost of a 4 KiB read was 6 ms and of 32 KiB 14 ms, so the
     * bounce size is a throughput knob, not just a safety net. PSRAM is
     * DMA-capable on this chip through the cache, so take the larger block
     * there rather than accept the small internal one. */
    if (s_dma_staging_bytes < LW_SD_DMA_CHUNK_MAX) {
        void *psram = heap_caps_malloc(LW_SD_DMA_CHUNK_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (psram) {
            heap_caps_free(s_dma_staging);
            s_dma_staging = psram;
            s_dma_staging_bytes = LW_SD_DMA_CHUNK_MAX;
        }
    }
    if (s_dma_staging) {
        bsp_sdcard->host.dma_aligned_buffer = s_dma_staging;
        ESP_LOGI(TAG, "sdmmc staging buffer: %u B", (unsigned)s_dma_staging_bytes);
    } else {
        /* Reads still work, just with the driver's per-transfer allocation. */
        ESP_LOGW(TAG, "sdmmc staging buffer unavailable");
    }
    owned = true;
    return result;
}

esp_err_t lw_sd_unmount_existing(void)
{
    if (!owned) return ESP_ERR_INVALID_STATE;
    if (bsp_sdcard && bsp_sdcard->host.dma_aligned_buffer == s_dma_staging) {
        bsp_sdcard->host.dma_aligned_buffer = NULL;
    }
    esp_err_t result = bsp_sdcard_unmount();
    if (result == ESP_OK) {
        owned = false;
        heap_caps_free(s_dma_staging);
        s_dma_staging = NULL;
        s_dma_staging_bytes = 0;
    }
    return result;
}

const char *lw_sd_asset_root(void) { return BSP_SD_MOUNT_POINT "/littleworld"; }
#endif
