/* SPDX-License-Identifier: MIT */
#include "lw_ui_asset_view.h"

/* The Windows host build compiles every file under lw_ui/src with the desktop
 * LVGL and no ESP-IDF, so this translation unit has to vanish there instead of
 * pulling in esp_heap_caps.h / esp_log.h. The pure-C half of the UI asset
 * pipeline (parsing, nine-slice geometry, repacking rules) lives in lw_engine
 * and IS host tested; only the LVGL widget placement below is device-only. */
#ifdef ESP_PLATFORM

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lw_path.h"
#include "lw_resource.h"

static const char *TAG = "lw_ui_asset";

/* One drawable piece of one asset, for one state. A panel owns nine of these, an
 * icon one, a button strip nine per state. */
typedef struct {
    lv_image_dsc_t image;
    uint8_t *pixels;
} piece_t;

typedef struct {
    bool used;
    char id[LW_UI_ASSET_ID_MAX];
    lw_ui_asset_t header;
    /* [state][piece]. A button needs its own nine pieces per state because the
     * three bands are stacked vertically in one image: a piece of the pressed
     * band is not the same pixels as the normal band, and RGB565A8 alpha planes
     * make a "just offset the pointer" trick impossible. */
    piece_t pieces[LW_UI_ASSET_MAX_STATES][LW_UI9_PIECES];
} slot_t;

static slot_t s_slots[LW_UI_ASSET_SLOTS];
static size_t s_count;
static size_t s_bytes;
static unsigned s_widgets;

static void free_piece(piece_t *piece)
{
    if (piece->pixels != NULL) {
        heap_caps_free(piece->pixels);
        piece->pixels = NULL;
    }
    memset(&piece->image, 0, sizeof(piece->image));
}

static void free_slot(slot_t *slot)
{
    for (unsigned s = 0; s < LW_UI_ASSET_MAX_STATES; s++) {
        for (unsigned i = 0; i < LW_UI9_PIECES; i++) {
            free_piece(&slot->pieces[s][i]);
        }
    }
    slot->used = false;
}

bool lw_ui_asset_reset(void)
{
    if (s_widgets != 0) {
        ESP_LOGW(TAG, "reset refused: %u widget(s) still reference the art", s_widgets);
        return false;
    }
    for (size_t i = 0; i < LW_UI_ASSET_SLOTS; i++) {
        if (s_slots[i].used) {
            free_slot(&s_slots[i]);
        }
    }
    memset(s_slots, 0, sizeof(s_slots));
    s_count = 0;
    s_bytes = 0;
    return true;
}

size_t lw_ui_asset_count(void) { return s_count; }
size_t lw_ui_asset_bytes(void) { return s_bytes; }

static slot_t *find_slot(const char *id)
{
    if (id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < LW_UI_ASSET_SLOTS; i++) {
        if (s_slots[i].used && strcmp(s_slots[i].id, id) == 0) {
            return &s_slots[i];
        }
    }
    return NULL;
}

const lw_ui_asset_t *lw_ui_asset_header(const char *id)
{
    slot_t *slot = find_slot(id);
    return slot != NULL ? &slot->header : NULL;
}

/* Copy one source rectangle out of the .lwu payload into its own descriptor.
 *
 * The copy is unavoidable for RGB565A8: the container stores one colour plane
 * followed by one alpha plane for the whole image, so a sub-rectangle is not
 * contiguous in either plane. The copy costs the same number of bytes the piece
 * occupies - the art is not duplicated, only rearranged into the layout an
 * lv_image_dsc_t requires. */
static bool repack_piece(piece_t *piece, const lw_ui_asset_t *asset, const lw_ui9_rect_t *src)
{
    unsigned w = src->w;
    unsigned h = src->h;
    if (w == 0 || h == 0) {
        return true; /* a legal degenerate piece: nothing to draw */
    }
    bool alpha = asset->format == LW_UI_ASSET_RGB565A8;
    size_t colour = (size_t)w * h * 2u;
    size_t total = colour + (alpha ? (size_t)w * h : 0u);

    uint8_t *pixels = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (pixels == NULL) {
        pixels = heap_caps_malloc(total, MALLOC_CAP_8BIT);
    }
    if (pixels == NULL) {
        ESP_LOGW(TAG, "out of memory for a %ux%u piece (%u B)", w, h, (unsigned)total);
        return false;
    }

    const uint8_t *payload = asset->payload;
    size_t image_w = asset->width;
    size_t image_h = (size_t)asset->height * asset->states;
    size_t colour_plane = image_w * image_h * 2u;
    for (unsigned row = 0; row < h; row++) {
        size_t src_row = (size_t)(src->y + (int)row);
        size_t src_col = (size_t)src->x;
        memcpy(pixels + (size_t)row * w * 2u, payload + (src_row * image_w + src_col) * 2u,
               (size_t)w * 2u);
        if (alpha) {
            memcpy(pixels + colour + (size_t)row * w,
                   payload + colour_plane + src_row * image_w + src_col, w);
        }
    }

    piece->pixels = pixels;
    piece->image.header.magic = LV_IMAGE_HEADER_MAGIC;
    piece->image.header.cf = alpha ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565;
    piece->image.header.w = (uint16_t)w;
    piece->image.header.h = (uint16_t)h;
    piece->image.header.stride = (uint16_t)(w * 2u);
    piece->image.data_size = (uint32_t)total;
    piece->image.data = pixels;
    return true;
}

/* Slice one state band of @p asset and repack all nine pieces. */
static bool build_state(slot_t *slot, unsigned state)
{
    const lw_ui_asset_t *asset = &slot->header;
    unsigned band_h = asset->height;
    if (asset->kind == LW_UI_ASSET_ICON) {
        lw_ui9_rect_t whole = {0, 0, asset->width, (uint16_t)band_h};
        return repack_piece(&slot->pieces[0][0], asset, &whole);
    }
    lw_ui9_rect_t src[LW_UI9_PIECES];
    lw_ui9_rect_t dst[LW_UI9_PIECES];
    if (lw_ui9_slice(asset->width, (uint16_t)band_h, &asset->insets, 0,
                     (int16_t)(state * band_h), asset->width, (uint16_t)band_h, src,
                     dst) != LW_UI9_OK) {
        return false;
    }
    for (unsigned i = 0; i < LW_UI9_PIECES; i++) {
        if (!repack_piece(&slot->pieces[state][i], asset, &src[i])) {
            return false;
        }
    }
    return true;
}

bool lw_ui_asset_add(const char *id, const uint8_t *bytes, size_t length)
{
    if (id == NULL || id[0] == '\0' || bytes == NULL) {
        return false;
    }
    size_t id_len = strlen(id);
    if (id_len >= LW_UI_ASSET_ID_MAX) {
        ESP_LOGW(TAG, "id too long: %s", id);
        return false;
    }
    if (find_slot(id) != NULL) {
        ESP_LOGW(TAG, "duplicate id: %s", id);
        return false;
    }
    slot_t *slot = NULL;
    for (size_t i = 0; i < LW_UI_ASSET_SLOTS; i++) {
        if (!s_slots[i].used) {
            slot = &s_slots[i];
            break;
        }
    }
    if (slot == NULL) {
        ESP_LOGW(TAG, "no free slot for %s (%u in use)", id, (unsigned)s_count);
        return false;
    }

    lw_ui_asset_t header;
    lw_ui_asset_status_t status = lw_ui_asset_parse(bytes, length, &header);
    if (status != LW_UI_ASSET_OK) {
        ESP_LOGW(TAG, "%s: %s", id, lw_ui_asset_status_text(status));
        return false;
    }

    memset(slot, 0, sizeof(*slot));
    memcpy(slot->id, id, id_len + 1u);
    slot->header = header;

    unsigned states = header.kind == LW_UI_ASSET_BUTTON ? header.states : 1u;
    for (unsigned s = 0; s < states; s++) {
        if (!build_state(slot, s)) {
            ESP_LOGW(TAG, "%s: cannot slice state %u", id, s);
            free_slot(slot);
            return false;
        }
    }

    size_t bytes_used = 0;
    for (unsigned s = 0; s < states; s++) {
        for (unsigned i = 0; i < LW_UI9_PIECES; i++) {
            bytes_used += slot->pieces[s][i].image.data_size;
        }
    }
    slot->used = true;
    s_count++;
    s_bytes += bytes_used;
    ESP_LOGI(TAG, "%s: %ux%u x%u %s, %u piece(s), %u B", id, header.width, header.height,
             header.states, header.format == LW_UI_ASSET_RGB565A8 ? "RGB565A8" : "RGB565",
             states * LW_UI9_PIECES, (unsigned)bytes_used);
    return true;
}

bool lw_ui_asset_add_file(const char *id, const char *relative)
{
    char path[512];
    if (!lw_resource_path(path, sizeof(path), relative)) {
        ESP_LOGW(TAG, "%s: cannot build a path for %s", id, relative ? relative : "(null)");
        return false;
    }
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) {
        ESP_LOGW(TAG, "%s: %s is not on the card", id, path);
        return false;
    }
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return false;
    }
    long size = ftell(stream);
    if (size <= 0 || (unsigned long)size > LW_UI_ASSET_FILE_MAX) {
        ESP_LOGW(TAG, "%s: implausible size %ld", id, size);
        fclose(stream);
        return false;
    }
    rewind(stream);
    uint8_t *buffer = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM);
    if (buffer == NULL) {
        buffer = heap_caps_malloc((size_t)size, MALLOC_CAP_8BIT);
    }
    if (buffer == NULL) {
        ESP_LOGW(TAG, "%s: out of memory for %ld B", id, size);
        fclose(stream);
        return false;
    }
    size_t got = fread(buffer, 1, (size_t)size, stream);
    fclose(stream);
    bool ok = got == (size_t)size && lw_ui_asset_add(id, buffer, (size_t)size);
    heap_caps_free(buffer); /* lw_ui_asset_add copied what it needs */
    return ok;
}

bool lw_ui_asset_minimum_size(const char *id, int *min_w, int *min_h)
{
    const lw_ui_asset_t *asset = lw_ui_asset_header(id);
    if (asset == NULL) {
        return false;
    }
    uint16_t w = 0;
    uint16_t h = 0;
    lw_ui_asset_minimum(asset, &w, &h);
    if (min_w != NULL) {
        *min_w = w;
    }
    if (min_h != NULL) {
        *min_h = h;
    }
    return true;
}

/* LVGL tells us when a widget dies. The count is what makes
 * lw_ui_asset_reset() safe: it refuses while any widget still points at the
 * pixel buffers, which is the same ordering rule the sprite leases follow. */
static void widget_deleted(lv_event_t *event)
{
    (void)event;
    if (s_widgets > 0) {
        s_widgets--;
    }
}

static void adopt_widget(lv_obj_t *object)
{
    lv_obj_add_event_cb(object, widget_deleted, LV_EVENT_DELETE, NULL);
    s_widgets++;
}

/* Apply the "pixel art" flag: nearest neighbour, so a stretched centre keeps its
 * hard edges instead of turning into mush. */
static void set_art_flags(lv_obj_t *object, const lw_ui_asset_t *asset)
{
    if (object != NULL && asset != NULL) {
        lv_image_set_antialias(object, !asset->pixel_art);
    }
}

lv_obj_t *lw_ui_asset_panel(lv_obj_t *parent, const char *id, int x, int y, int w, int h,
                            lw_ui9_button_state_t state)
{
    slot_t *slot = find_slot(id);
    if (parent == NULL || slot == NULL || w <= 0 || h <= 0) {
        return NULL;
    }
    if (slot->header.kind == LW_UI_ASSET_ICON) {
        return lw_ui_asset_icon(parent, id, x, y, w, h);
    }
    unsigned frame = 0;
    if (slot->header.kind == LW_UI_ASSET_BUTTON) {
        frame = (state >= 0 && state < LW_UI9_STATE_COUNT) ? (unsigned)state : 0u;
        if (frame >= slot->header.states) {
            frame = slot->header.states - 1u;
        }
    }
    lw_ui9_rect_t src[LW_UI9_PIECES];
    lw_ui9_rect_t dst[LW_UI9_PIECES];
    if (lw_ui9_slice(slot->header.width, slot->header.height, &slot->header.insets, (int16_t)x,
                     (int16_t)y, (uint16_t)w, (uint16_t)h, src, dst) != LW_UI9_OK) {
        int min_w = 0;
        int min_h = 0;
        (void)lw_ui_asset_minimum_size(id, &min_w, &min_h);
        ESP_LOGW(TAG, "%s: cannot slice into %dx%d (minimum %dx%d)", id, w, h, min_w, min_h);
        return NULL;
    }

    lv_obj_t *container = lv_obj_create(parent);
    if (container == NULL) {
        return NULL;
    }
    lv_obj_remove_style_all(container);
    lv_obj_set_pos(container, x, y);
    lv_obj_set_size(container, w, h);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_CLICKABLE);

    for (unsigned i = 0; i < LW_UI9_PIECES; i++) {
        const piece_t *piece = &slot->pieces[frame][i];
        if (src[i].w == 0 || src[i].h == 0 || piece->image.data == NULL) {
            continue;
        }
        lv_obj_t *view = lv_image_create(container);
        if (view == NULL) {
            continue;
        }
        /* The piece is a full descriptor of its own (see repack_piece), so the
         * source is simply that descriptor: no offsets, no sub-rectangle maths
         * at draw time. */
        lv_image_set_src(view, &piece->image);
        lv_obj_set_pos(view, dst[i].x - x, dst[i].y - y);
        if (dst[i].w != src[i].w) {
            lv_image_set_scale_x(view, (uint32_t)dst[i].w * 256u / src[i].w);
        }
        if (dst[i].h != src[i].h) {
            lv_image_set_scale_y(view, (uint32_t)dst[i].h * 256u / src[i].h);
        }
        set_art_flags(view, &slot->header);
    }
    adopt_widget(container);
    return container;
}

lv_obj_t *lw_ui_asset_icon(lv_obj_t *parent, const char *id, int x, int y, int w, int h)
{
    slot_t *slot = find_slot(id);
    if (parent == NULL || slot == NULL || w <= 0 || h <= 0 || slot->pieces[0][0].image.data == NULL) {
        return NULL;
    }
    lv_obj_t *image = lv_image_create(parent);
    if (image == NULL) {
        return NULL;
    }
    lv_image_set_src(image, &slot->pieces[0][0].image);
    lv_obj_set_pos(image, x, y);
    if (slot->header.width != 0 && slot->header.height != 0) {
        lv_image_set_scale_x(image, (uint32_t)w * 256u / slot->header.width);
        lv_image_set_scale_y(image, (uint32_t)h * 256u / (slot->header.height * slot->header.states));
    }
    set_art_flags(image, &slot->header);
    adopt_widget(image);
    return image;
}

#endif /* ESP_PLATFORM */
