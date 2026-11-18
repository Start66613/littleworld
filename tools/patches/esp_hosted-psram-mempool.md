# Local patch: let the esp_hosted receive pool spill into PSRAM

`firmware/managed_components/` is git-ignored, so this file is the record of a
patch that lives only in the working tree. Re-apply it after
`tools/setup_wifi_components.ps1` or any dependency update.

## File

`firmware/managed_components/espressif__esp_hosted/host/port/include/os_wrapper.h`

## Why

The SDIO receive pool allocates fixed blocks with

```c
#define MEM_ALLOC(x)  heap_caps_aligned_alloc(64, (x), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)
```

The pool grows on demand and never returns blocks, so once the LVGL UI has taken
its share of internal RAM (about 26 KB free / 12 KB largest block after the UI is
built) a busy scan hits a NULL and the host asserts in `sdio_read_task`:

```
assert failed: sdio_read_task sdio_drv.c:878 (rxbuff)
```

On this part `SOC_PSRAM_DMA_CAPABLE` and `SOC_SDMMC_PSRAM_DMA_CAPABLE` are both
set, so the SDMMC controller can read descriptors and buffers straight out of
PSRAM. Falling back to PSRAM keeps the pool working without taking internal RAM
from the drivers that cannot use PSRAM (I2S DMA, for instance).

## Replacement

```c
static inline void *hosted_mem_alloc(size_t size)
{
    void *p = heap_caps_aligned_alloc(64, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!p) {
        p = heap_caps_aligned_alloc(64, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    }
    return p;
}
#define MEM_ALLOC(x)                     hosted_mem_alloc(x)
```

## Verified

With the patch, the board boots once, opens the Wi-Fi page, and reports
`scan done: 6 access point(s), generation 2` with the page listing the real
access points. Without it the same sequence asserts and reboots.
