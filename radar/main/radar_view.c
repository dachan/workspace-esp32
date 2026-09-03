#include "radar_view.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display_sync.h"
#include "display_profile.h"

enum {
    DMA_ROWS = 32,
    MAX_DYNAMIC_RECTS = 32,
    RADAR_RANGE_MM = 6000,
    RADAR_VIEW_WIDTH = LCD_WIDTH,
    RADAR_VIEW_HEIGHT = LCD_HEIGHT,
    RADAR_SIDE_MARGIN = RADAR_VIEW_WIDTH / 12,
    RADAR_TAN_60_MILLI = 1732,
    RADAR_SIN_60_MILLI = 866,
    // Visual-left margin in pixels. Framebuffer +X is visual left, so x=8 is
    // the viewed RIGHT. Left-aligned origins use radar_text_left_x().
    DISTANCE_TEXT_X = 8,
    DISTANCE_TEXT_Y = 8,
    DISTANCE_GLYPH_WIDTH = 5,
    DISTANCE_GLYPH_HEIGHT = 7,
    DISTANCE_GLYPH_ADVANCE = 6,
    TEXT_SCALE_NUMERATOR = 8,
    TEXT_SCALE_DENOMINATOR = 5,
    TEXT_GLYPH_HEIGHT = (DISTANCE_GLYPH_HEIGHT * TEXT_SCALE_NUMERATOR
                         + TEXT_SCALE_DENOMINATOR / 2) / TEXT_SCALE_DENOMINATOR,
    TEXT_GLYPH_ADVANCE = (DISTANCE_GLYPH_ADVANCE * TEXT_SCALE_NUMERATOR
                          + TEXT_SCALE_DENOMINATOR / 2) / TEXT_SCALE_DENOMINATOR,
    TARGET_TEXT_LINE_HEIGHT = TEXT_GLYPH_HEIGHT + 3,
    TARGET_TEXT_MAX_CHARS = 23,
    RADAR_BOTTOM_MARGIN = RADAR_VIEW_HEIGHT / 12,
    RADAR_WIDTH_RADIUS = (RADAR_VIEW_WIDTH / 2 - RADAR_SIDE_MARGIN)
                       * 1000 / RADAR_SIN_60_MILLI,
    DISTANCE_TEXT_BOTTOM = DISTANCE_TEXT_Y
                         + TEXT_GLYPH_HEIGHT,
    ACCEL_TEXT_Y = DISTANCE_TEXT_BOTTOM + 4,
    ACCEL_TEXT_BOTTOM = ACCEL_TEXT_Y + TEXT_GLYPH_HEIGHT,
    TARGET_TEXT_TOP = RADAR_VIEW_HEIGHT - TARGET_TEXT_LINE_HEIGHT - 3,
    RADAR_SENSOR_Y = ACCEL_TEXT_BOTTOM
                   + (TARGET_TEXT_TOP - ACCEL_TEXT_BOTTOM
                      - RADAR_WIDTH_RADIUS) / 2,
};

#define RADAR_PING_SECONDS 0.25f
#define RADAR_PING_MAX_RADIUS 24
#define TARGET_SMOOTHING_PER_SECOND 12.0f
#define MOTION_VARIANCE_DISPLAY_GAIN 5

typedef struct {
    int x;
    int y;
    int width;
    int height;
} dirty_rect_t;

typedef struct {
    char distance_text[24];
    char acceleration_text[24];
    char target_text[RADAR_VIEW_MAX_PEOPLE][TARGET_TEXT_MAX_CHARS + 1];
    int distance_text_x;
    int acceleration_text_x;
    int target_text_x[RADAR_VIEW_MAX_PEOPLE];
    int target_text_y[RADAR_VIEW_MAX_PEOPLE];
} overlay_t;

static uint16_t *s_static_pixels;
static uint16_t *s_dma_pixels;
static bool s_static_ready;
static dirty_rect_t s_previous_rects[MAX_DYNAMIC_RECTS];
static int s_previous_rect_count;

static int64_t radar_now_us(void)
{
    return (int64_t)pdTICKS_TO_MS(xTaskGetTickCount()) * 1000;
}

static uint16_t rgb565_wire(uint8_t red, uint8_t green, uint8_t blue)
{
    uint16_t value = ((uint16_t)(red & 0xf8) << 8)
                   | ((uint16_t)(green & 0xfc) << 3)
                   | ((uint16_t)blue >> 3);
    return __builtin_bswap16(value);
}

static uint8_t circular_glow(int dx, int dy, int radius)
{
    int distance_squared = dx * dx + dy * dy;
    int radius_squared = radius * radius;

    return distance_squared >= radius_squared
        ? 0
        : (radius_squared - distance_squared) * 255 / radius_squared;
}

#if !defined(RADAR_LINK_ROLE_RECEIVER)
static bool cursor_shadow_at(int dx, int dy)
{
    return (abs(dx) <= 2 && abs(dy) <= 9) || (abs(dy) <= 2 && abs(dx) <= 9);
}

static bool cursor_foreground_at(int dx, int dy)
{
    return (dx == 0 && abs(dy) <= 8) || (dy == 0 && abs(dx) <= 8);
}
#endif

static int radar_mirror_x(int x)
{
    return RADAR_VIEW_WIDTH - 1 - x;
}

static int radar_radius(void)
{
    int height_radius = RADAR_VIEW_HEIGHT - RADAR_SENSOR_Y - RADAR_BOTTOM_MARGIN;
    int half_width = RADAR_VIEW_WIDTH / 2 - RADAR_SIDE_MARGIN;
    int width_radius = half_width * 1000 / RADAR_SIN_60_MILLI;
    return height_radius < width_radius ? height_radius : width_radius;
}

static bool radar_point_in_fov(int dx, int dy, int radius)
{
    if (dy < 0 || dx * dx + dy * dy > radius * radius) {
        return false;
    }
    return abs(dx) * 1000 <= dy * RADAR_TAN_60_MILLI;
}

static bool radar_ring_at(int dx, int dy, int radius)
{
    int distance_squared = dx * dx + dy * dy;
    int radius_squared = radius * radius;
    return abs(distance_squared - radius_squared) <= radius * 2 + 1;
}

static uint8_t radar_grid_brightness_at(int x, int y)
{
    static const int ray_tangent_milli[] = {
        0, 176, 364, 577, 839, 1192, 1732,
    };
    int dx = x - RADAR_VIEW_WIDTH / 2;
    int dy = y - RADAR_SENSOR_Y;
    int radius = radar_radius();
    if (!radar_point_in_fov(dx, dy, radius)) {
        return 0;
    }

    int boundary_error = abs(abs(dx) * 1000 - dy * RADAR_TAN_60_MILLI);
    if (boundary_error <= 1800) {
        return 150;
    }

    uint8_t brightness = 0;
    for (size_t ray = 0;
         ray < sizeof(ray_tangent_milli) / sizeof(ray_tangent_milli[0]);
         ++ray) {
        int error = abs(abs(dx) * 1000 - dy * ray_tangent_milli[ray]);
        if (error <= 1050) {
            brightness = ray == 0 || ray == 3 ? 105 : 62;
            break;
        }
    }

    for (int half_metre = 1; half_metre <= 12; ++half_metre) {
        if (radar_ring_at(dx, dy, half_metre * radius / 12)) {
            uint8_t ring_brightness = half_metre % 2 == 0 ? 92 : 48;
            if (ring_brightness > brightness) {
                brightness = ring_brightness;
            }
        }
    }
    return brightness;
}

static bool person_screen_position(const radar_person_t *person,
                                   int *screen_x, int *screen_y)
{
    int x_mm = person->x_mm;
    int y_mm = person->y_mm;
    int64_t distance_squared = (int64_t)x_mm * x_mm + (int64_t)y_mm * y_mm;
    if (y_mm < 0 || distance_squared > (int64_t)RADAR_RANGE_MM * RADAR_RANGE_MM
        || (int64_t)abs(x_mm) * 1000 > (int64_t)y_mm * RADAR_TAN_60_MILLI) {
        return false;
    }

    int radius = radar_radius();
    *screen_x = radar_mirror_x(RADAR_VIEW_WIDTH / 2 + x_mm * radius / RADAR_RANGE_MM);
    *screen_y = RADAR_SENSOR_Y + y_mm * radius / RADAR_RANGE_MM;
    return true;
}

static bool person_dot_at(int x, int y, int centre_x, int centre_y)
{
    int dx = x - centre_x;
    int dy = y - centre_y;
    int radius = 4;
    return dx * dx + dy * dy <= radius * radius;
}

static bool person_at(const radar_view_t *view, int x, int y)
{
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        const radar_person_t *person = &view->people[index];
        if (!person->active) {
            continue;
        }
        int screen_x;
        int screen_y;
        if (!person_screen_position(person, &screen_x, &screen_y)) {
            continue;
        }
        if (person_dot_at(x, y, screen_x, screen_y)) {
            return true;
        }
    }
    return false;
}

static uint32_t integer_square_root(uint64_t value)
{
    uint64_t result = 0;
    uint64_t bit = 1ULL << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

static bool nearest_distance_tenths(const radar_view_t *view,
                                    unsigned int *distance_tenths)
{
    uint64_t nearest_squared = UINT64_MAX;
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        const radar_person_t *person = &view->people[index];
        if (!person->active) {
            continue;
        }
        int64_t x_mm = person->x_mm;
        int64_t y_mm = person->y_mm;
        uint64_t distance_squared = (uint64_t)(x_mm * x_mm + y_mm * y_mm);
        if (distance_squared < nearest_squared) {
            nearest_squared = distance_squared;
        }
    }
    if (nearest_squared == UINT64_MAX) {
        return false;
    }

    unsigned int tenths = (integer_square_root(nearest_squared) + 50) / 100;
    *distance_tenths = tenths > 999 ? 999 : tenths;
    return true;
}

static uint8_t radar_glyph_row(char character, int row)
{
    static const uint8_t digits[10][DISTANCE_GLYPH_HEIGHT] = {
        { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
        { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
        { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
        { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
        { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
        { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
        { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e },
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
        { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
        { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e },
    };
    static const uint8_t decimal[DISTANCE_GLYPH_HEIGHT] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06,
    };
    static const uint8_t dash[DISTANCE_GLYPH_HEIGHT] = {
        0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00,
    };
    static const uint8_t metres[DISTANCE_GLYPH_HEIGHT] = {
        0x00, 0x00, 0x1a, 0x15, 0x15, 0x15, 0x15,
    };
    static const uint8_t plus[DISTANCE_GLYPH_HEIGHT] = {
        0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00,
    };
    static const uint8_t x_axis[DISTANCE_GLYPH_HEIGHT] = {
        0x11, 0x0a, 0x04, 0x04, 0x04, 0x0a, 0x11,
    };
    static const uint8_t y_axis[DISTANCE_GLYPH_HEIGHT] = {
        0x11, 0x0a, 0x04, 0x04, 0x04, 0x04, 0x04,
    };
    static const uint8_t colon[DISTANCE_GLYPH_HEIGHT] = {
        0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00,
    };
    static const uint8_t distance_d[DISTANCE_GLYPH_HEIGHT] = {
        0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e,
    };
    static const uint8_t distance_i[DISTANCE_GLYPH_HEIGHT] = {
        0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e,
    };
    static const uint8_t distance_s[DISTANCE_GLYPH_HEIGHT] = {
        0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e,
    };
    static const uint8_t distance_t[DISTANCE_GLYPH_HEIGHT] = {
        0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    };
    static const uint8_t distance_a[DISTANCE_GLYPH_HEIGHT] = {
        0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11,
    };
    static const uint8_t distance_n[DISTANCE_GLYPH_HEIGHT] = {
        0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11,
    };
    static const uint8_t distance_c[DISTANCE_GLYPH_HEIGHT] = {
        0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e,
    };
    static const uint8_t distance_e[DISTANCE_GLYPH_HEIGHT] = {
        0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f,
    };
    static const uint8_t distance_l[DISTANCE_GLYPH_HEIGHT] = {
        0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f,
    };
    static const uint8_t slash[DISTANCE_GLYPH_HEIGHT] = {
        0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10,
    };

    if (character >= '0' && character <= '9') {
        return digits[character - '0'][row];
    }
    if (character == '.') {
        return decimal[row];
    }
    if (character == '-') {
        return dash[row];
    }
    if (character == 'm') {
        return metres[row];
    }
    if (character == '+') {
        return plus[row];
    }
    if (character == 'X') {
        return x_axis[row];
    }
    if (character == 'Y') {
        return y_axis[row];
    }
    if (character == ':') {
        return colon[row];
    }
    if (character == 'D') {
        return distance_d[row];
    }
    if (character == 'I') {
        return distance_i[row];
    }
    if (character == 'S') {
        return distance_s[row];
    }
    if (character == 'T') {
        return distance_t[row];
    }
    if (character == 'A') {
        return distance_a[row];
    }
    if (character == 'N') {
        return distance_n[row];
    }
    if (character == 'C') {
        return distance_c[row];
    }
    if (character == 'E') {
        return distance_e[row];
    }
    if (character == 'L') {
        return distance_l[row];
    }
    if (character == '/') {
        return slash[row];
    }
    return 0;
}

static int radar_text_pixel_width(const char *text, int max_characters)
{
    int length = 0;
    while (length < max_characters && text[length] != '\0') {
        ++length;
    }
    return length * TEXT_GLYPH_ADVANCE;
}

// After MADCTL, framebuffer +X is visual left. Left-align against that edge.
static int radar_text_left_x(const char *text, int max_characters)
{
    return RADAR_VIEW_WIDTH - DISTANCE_TEXT_X
        - radar_text_pixel_width(text, max_characters);
}

static bool radar_text_box_at(const char *text, int origin_x, int origin_y,
                              int max_characters, int padding,
                              int x, int y)
{
    int length = 0;
    while (length < max_characters && text[length] != '\0') {
        ++length;
    }
    int local_x = x - origin_x;
    int local_y = y - origin_y;
    return local_x >= -padding
        && local_x < length * TEXT_GLYPH_ADVANCE + padding
        && local_y >= -padding
        && local_y < TEXT_GLYPH_HEIGHT + padding;
}

static bool radar_text_at(const char *text, int origin_x, int origin_y,
                          int max_characters, int x, int y)
{
    int length = 0;
    while (length < max_characters && text[length] != '\0') {
        ++length;
    }
    // Glyphs are authored left-to-right. After the landscape MADCTL rotation
    // framebuffer +X is visual left, so sample the string mirrored in X.
    int local_x = length * TEXT_GLYPH_ADVANCE - 1 - (x - origin_x);
    int local_y = y - origin_y;
    if (local_x < 0 || local_y < 0
        || local_y >= TEXT_GLYPH_HEIGHT) {
        return false;
    }

    int character_index = local_x / TEXT_GLYPH_ADVANCE;
    if (character_index >= max_characters || text[character_index] == '\0') {
        return false;
    }
    int glyph_x = ((local_x % TEXT_GLYPH_ADVANCE) * TEXT_SCALE_DENOMINATOR)
                / TEXT_SCALE_NUMERATOR;
    if (glyph_x >= DISTANCE_GLYPH_WIDTH) {
        return false;
    }
    int glyph_y = local_y * TEXT_SCALE_DENOMINATOR / TEXT_SCALE_NUMERATOR;
    uint8_t row = radar_glyph_row(text[character_index], glyph_y);
    return (row & (1U << (DISTANCE_GLYPH_WIDTH - 1 - glyph_x))) != 0;
}

static void target_coordinate_text(const radar_person_t *person, int target_number,
                                   char text[TARGET_TEXT_MAX_CHARS + 1])
{
    int x_mm = person->x_mm;
    int y_mm = person->y_mm;
    if (x_mm < -RADAR_RANGE_MM) {
        x_mm = -RADAR_RANGE_MM;
    } else if (x_mm > RADAR_RANGE_MM) {
        x_mm = RADAR_RANGE_MM;
    }
    if (y_mm < 0) {
        y_mm = 0;
    } else if (y_mm > RADAR_RANGE_MM) {
        y_mm = RADAR_RANGE_MM;
    }
    unsigned int x_tenths = (abs(x_mm) + 50) / 100;
    unsigned int y_tenths = (y_mm + 50) / 100;
    snprintf(text, TARGET_TEXT_MAX_CHARS + 1, "T%d X:%c%u.%um Y:+%u.%um",
             target_number, x_mm < 0 ? '-' : '+',
             x_tenths / 10, x_tenths % 10,
             y_tenths / 10, y_tenths % 10);
}

static uint8_t ping_brightness(const radar_ping_t *ping, int x, int y)
{
    if (ping->age_seconds >= RADAR_PING_SECONDS) {
        return 0;
    }
    int dx = x - ping->x;
    int dy = y - ping->y;
    int radius = 4 + (int)(ping->age_seconds
                         * (RADAR_PING_MAX_RADIUS - 4) / RADAR_PING_SECONDS);
    uint8_t opacity = (uint8_t)((RADAR_PING_SECONDS - ping->age_seconds) * 255.0f
                              / RADAR_PING_SECONDS);
    return (uint16_t)circular_glow(dx, dy, radius) * opacity / 255;
}

static int next_ping_slot(const radar_view_t *view)
{
    int oldest = 0;
    for (int i = 0; i < RADAR_VIEW_MAX_PINGS; ++i) {
        if (view->pings[i].age_seconds >= RADAR_PING_SECONDS) {
            return i;
        }
        if (view->pings[i].age_seconds > view->pings[oldest].age_seconds) {
            oldest = i;
        }
    }
    return oldest;
}

static uint8_t static_brightness_uncached(int x, int y)
{
    int radar_x = radar_mirror_x(x);
    int radar_dx = radar_x - RADAR_VIEW_WIDTH / 2;
    int radar_dy = y - RADAR_SENSOR_Y;
    uint8_t green = radar_point_in_fov(radar_dx, radar_dy, radar_radius()) ? 7 : 0;
    uint8_t grid = radar_grid_brightness_at(radar_x, y);
    return grid > green ? grid : green;
}

static uint8_t static_brightness_at(int display_x, int display_y)
{
    if (!s_static_ready) {
        return static_brightness_uncached(display_x, display_y);
    }
    uint16_t rgb565 = __builtin_bswap16(
        s_static_pixels[(size_t)display_y * LCD_WIDTH + display_x]);
    return (uint8_t)((rgb565 & 0x07e0) >> 3);
}

static bool renderer_init(void)
{
    if (!s_dma_pixels) {
        s_dma_pixels = heap_caps_malloc(LCD_WIDTH * DMA_ROWS * sizeof(*s_dma_pixels),
                                        MALLOC_CAP_DMA);
    }
    if (!s_dma_pixels) {
        ESP_LOGE("radar_view", "DMA render buffer allocation failed");
        return false;
    }

    if (!s_static_pixels) {
        s_static_pixels = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(*s_static_pixels),
                                            MALLOC_CAP_SPIRAM);
    }
    if (!s_static_pixels) {
        ESP_LOGW("radar_view", "static radar cache unavailable; using direct composition");
        return true;
    }

    for (int y = 0; y < LCD_HEIGHT; ++y) {
        for (int x = 0; x < LCD_WIDTH; ++x) {
            s_static_pixels[(size_t)y * LCD_WIDTH + x] =
                rgb565_wire(0, static_brightness_uncached(x, y), 0);
        }
    }
    s_static_ready = true;
    return true;
}

static bool clip_rect(dirty_rect_t *rect)
{
    int x_end = rect->x + rect->width;
    int y_end = rect->y + rect->height;
    if (rect->x < 0) {
        rect->x = 0;
    }
    if (rect->y < 0) {
        rect->y = 0;
    }
    if (x_end > LCD_WIDTH) {
        x_end = LCD_WIDTH;
    }
    if (y_end > LCD_HEIGHT) {
        y_end = LCD_HEIGHT;
    }
    rect->width = x_end - rect->x;
    rect->height = y_end - rect->y;
    return rect->width > 0 && rect->height > 0;
}

static bool rects_touch(const dirty_rect_t *first, const dirty_rect_t *second)
{
    return first->x <= second->x + second->width
        && second->x <= first->x + first->width
        && first->y <= second->y + second->height
        && second->y <= first->y + first->height;
}

static void merge_rect(dirty_rect_t *destination, const dirty_rect_t *source)
{
    int x_end = destination->x + destination->width;
    int y_end = destination->y + destination->height;
    int source_x_end = source->x + source->width;
    int source_y_end = source->y + source->height;
    if (source->x < destination->x) {
        destination->x = source->x;
    }
    if (source->y < destination->y) {
        destination->y = source->y;
    }
    if (source_x_end > x_end) {
        x_end = source_x_end;
    }
    if (source_y_end > y_end) {
        y_end = source_y_end;
    }
    destination->width = x_end - destination->x;
    destination->height = y_end - destination->y;
}

static void add_dirty_rect(dirty_rect_t rects[MAX_DYNAMIC_RECTS], int *count,
                           dirty_rect_t rect)
{
    if (!clip_rect(&rect)) {
        return;
    }
    for (int index = 0; index < *count; ++index) {
        if (rects_touch(&rects[index], &rect)) {
            merge_rect(&rects[index], &rect);
            for (int other = 0; other < *count; ++other) {
                if (other != index && rects_touch(&rects[index], &rects[other])) {
                    merge_rect(&rects[index], &rects[other]);
                    rects[other] = rects[--*count];
                    other = -1;
                }
            }
            return;
        }
    }
    if (*count < MAX_DYNAMIC_RECTS) {
        rects[(*count)++] = rect;
    } else {
        merge_rect(&rects[0], &rect);
    }
}

static dirty_rect_t radar_rect_to_display(int radar_x, int radar_y, int width, int height)
{
    return (dirty_rect_t) {
        .x = radar_x,
        .y = radar_y,
        .width = width,
        .height = height,
    };
}

static void prepare_overlay(const radar_view_t *view, overlay_t *overlay)
{
    *overlay = (overlay_t) {0};
    unsigned int distance_tenths;
    if (nearest_distance_tenths(view, &distance_tenths)) {
        snprintf(overlay->distance_text, sizeof(overlay->distance_text), "DISTANCE: %u.%um",
                 distance_tenths / 10, distance_tenths % 10);
    } else {
        snprintf(overlay->distance_text, sizeof(overlay->distance_text), "DISTANCE: --.-m");
    }
    overlay->distance_text_x = radar_text_left_x(overlay->distance_text, 16);

    int32_t acceleration_magnitude = view->radial_acceleration_mm_per_second_squared < 0
                                   ? -(int32_t)view->radial_acceleration_mm_per_second_squared
                                   : view->radial_acceleration_mm_per_second_squared;
    unsigned int acceleration_tenths = (acceleration_magnitude
        * MOTION_VARIANCE_DISPLAY_GAIN + 50) / 100;
    if (acceleration_tenths > 999) {
        acceleration_tenths = 999;
    }
    snprintf(overlay->acceleration_text, sizeof(overlay->acceleration_text),
             "ACCEL: %u.%u", acceleration_tenths / 10, acceleration_tenths % 10);
    overlay->acceleration_text_x = radar_text_left_x(overlay->acceleration_text, 12);

    int active_text_count = 0;
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        if (view->people[index].active) {
            target_coordinate_text(&view->people[index], index + 1,
                                   overlay->target_text[index]);
            overlay->target_text_x[index] = radar_text_left_x(
                overlay->target_text[index], TARGET_TEXT_MAX_CHARS);
            ++active_text_count;
        }
    }
    int active_text_line = 0;
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        if (overlay->target_text[index][0] != '\0') {
            overlay->target_text_y[index] = RADAR_VIEW_HEIGHT
                - (active_text_count - active_text_line) * TARGET_TEXT_LINE_HEIGHT - 3;
            ++active_text_line;
        }
    }
}

static uint8_t compose_brightness(const radar_view_t *view, const overlay_t *overlay,
                                  int x, int y)
{
    uint8_t green = static_brightness_at(x, y);
    for (int index = 0; index < RADAR_VIEW_MAX_PINGS; ++index) {
        uint8_t strength = ping_brightness(&view->pings[index], x, y);
        if (strength > green) {
            green = strength;
        }
    }

#if !defined(RADAR_LINK_ROLE_RECEIVER)
    int cursor_dx = x - view->cursor_x;
    int cursor_dy = y - view->cursor_y;
    if (cursor_shadow_at(cursor_dx, cursor_dy)) {
        green = 72;
    }
    if (cursor_foreground_at(cursor_dx, cursor_dy)) {
        green = 255;
    }
#endif
    if (person_at(view, x, y)) {
        green = 255;
    }

    bool text_background = radar_text_box_at(
        overlay->distance_text, overlay->distance_text_x, DISTANCE_TEXT_Y,
        16, 2, x, y)
        || radar_text_box_at(overlay->acceleration_text, overlay->acceleration_text_x,
                             ACCEL_TEXT_Y, 12, 2, x, y);
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        if (overlay->target_text[index][0] != '\0'
            && radar_text_box_at(overlay->target_text[index],
                                 overlay->target_text_x[index],
                                 overlay->target_text_y[index],
                                 TARGET_TEXT_MAX_CHARS, 2, x, y)) {
            text_background = true;
        }
    }
    if (text_background) {
        green = 0;
    }
    if (radar_text_at(overlay->distance_text, overlay->distance_text_x,
                      DISTANCE_TEXT_Y, 16, x, y)
        || radar_text_at(overlay->acceleration_text, overlay->acceleration_text_x,
                         ACCEL_TEXT_Y, 20, x, y)) {
        green = 255;
    }
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        if (overlay->target_text[index][0] != '\0'
            && radar_text_at(overlay->target_text[index],
                             overlay->target_text_x[index],
                             overlay->target_text_y[index],
                             TARGET_TEXT_MAX_CHARS, x, y)) {
            green = 225;
        }
    }
    return green;
}

static void render_static_full(esp_lcd_panel_handle_t panel)
{
    for (int y = 0; y < LCD_HEIGHT; y += DMA_ROWS) {
        int rows = LCD_HEIGHT - y < DMA_ROWS ? LCD_HEIGHT - y : DMA_ROWS;
        for (int row = 0; row < rows; ++row) {
            uint16_t *destination = s_dma_pixels + row * LCD_WIDTH;
            if (s_static_ready) {
                memcpy(destination, s_static_pixels + (size_t)(y + row) * LCD_WIDTH,
                       LCD_WIDTH * sizeof(*destination));
            } else {
                for (int x = 0; x < LCD_WIDTH; ++x) {
                    destination[x] = rgb565_wire(0, static_brightness_uncached(x, y + row), 0);
                }
            }
        }
        display_sync_draw(panel, 0, y, LCD_WIDTH, y + rows, s_dma_pixels);
    }
}

static void render_dirty_rect(esp_lcd_panel_handle_t panel, const radar_view_t *view,
                              const overlay_t *overlay, dirty_rect_t rect)
{
    if (!clip_rect(&rect)) {
        return;
    }
    for (int y = rect.y; y < rect.y + rect.height; y += DMA_ROWS) {
        int rows = rect.y + rect.height - y < DMA_ROWS ? rect.y + rect.height - y : DMA_ROWS;
        for (int row = 0; row < rows; ++row) {
            for (int x = 0; x < rect.width; ++x) {
                int display_x = rect.x + x;
                s_dma_pixels[row * rect.width + x] = rgb565_wire(
                    0, compose_brightness(view, overlay, display_x, y + row), 0);
            }
        }
        display_sync_draw(panel, rect.x, y, rect.x + rect.width, y + rows, s_dma_pixels);
    }
}

static void add_radar_text_rect(dirty_rect_t rects[MAX_DYNAMIC_RECTS], int *count,
                                const char *text, int radar_x, int radar_y,
                                int max_characters)
{
    int length = 0;
    while (length < max_characters && text[length] != '\0') {
        ++length;
    }
    add_dirty_rect(rects, count, radar_rect_to_display(
        radar_x - 2, radar_y - 2,
        length * TEXT_GLYPH_ADVANCE + 4,
        TEXT_GLYPH_HEIGHT + 4));
}

static int ping_radius(const radar_ping_t *ping)
{
    return ping->age_seconds >= RADAR_PING_SECONDS ? 0
        : 4 + (int)(ping->age_seconds
                  * (RADAR_PING_MAX_RADIUS - 4) / RADAR_PING_SECONDS);
}

static void collect_dynamic_rects(const radar_view_t *view, const overlay_t *overlay,
                                  dirty_rect_t rects[MAX_DYNAMIC_RECTS], int *count)
{
    *count = 0;
    add_radar_text_rect(rects, count, overlay->distance_text,
                        overlay->distance_text_x, DISTANCE_TEXT_Y,
                        16);
    add_radar_text_rect(rects, count, overlay->acceleration_text,
                        overlay->acceleration_text_x, ACCEL_TEXT_Y,
                        12);
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        if (overlay->target_text[index][0] != '\0') {
            add_radar_text_rect(rects, count, overlay->target_text[index],
                                overlay->target_text_x[index],
                                overlay->target_text_y[index], TARGET_TEXT_MAX_CHARS);
        }
        int radar_x;
        int radar_y;
        if (view->people[index].active
            && person_screen_position(&view->people[index], &radar_x, &radar_y)) {
            add_dirty_rect(rects, count, (dirty_rect_t) {
                .x = radar_x - 5, .y = radar_y - 5, .width = 11, .height = 11,
            });
        }
    }
    for (int index = 0; index < RADAR_VIEW_MAX_PINGS; ++index) {
        const radar_ping_t *ping = &view->pings[index];
        int radius = ping_radius(ping);
        if (radius > 0) {
            add_dirty_rect(rects, count, (dirty_rect_t) {
                .x = ping->x - radius - 6,
                .y = ping->y - radius - 6,
                .width = radius * 2 + 13,
                .height = radius * 2 + 13,
            });
        }
    }
#if !defined(RADAR_LINK_ROLE_RECEIVER)
    add_dirty_rect(rects, count, (dirty_rect_t) {
        .x = view->cursor_x - 10,
        .y = view->cursor_y - 10,
        .width = 21,
        .height = 21,
    });
#endif
}

static void update_people(radar_view_t *view, float dt)
{
    float blend = dt * TARGET_SMOOTHING_PER_SECOND;
    if (blend > 1.0f) {
        blend = 1.0f;
    }
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        if (!view->people_target_active[index]) {
            view->people[index].active = false;
            view->people_initialized[index] = false;
            continue;
        }
        if (!view->people_initialized[index]) {
            view->people_target_x[index] = view->people[index].x_mm;
            view->people_target_y[index] = view->people[index].y_mm;
            view->people_initialized[index] = true;
        }
        float current_x = view->people[index].x_mm;
        float current_y = view->people[index].y_mm;
        current_x += (view->people_target_x[index] - current_x) * blend;
        current_y += (view->people_target_y[index] - current_y) * blend;
        view->people[index].x_mm = (int16_t)(current_x + (current_x >= 0 ? 0.5f : -0.5f));
        view->people[index].y_mm = (int16_t)(current_y + (current_y >= 0 ? 0.5f : -0.5f));
        view->people[index].active = true;
    }
}

void radar_view_start(esp_lcd_panel_handle_t panel, radar_view_t *view)
{
    *view = (radar_view_t) {
        .cursor_x = LCD_WIDTH / 2,
        .cursor_y = LCD_HEIGHT / 2,
        .last_step_us = radar_now_us(),
    };
    for (int i = 0; i < RADAR_VIEW_MAX_PINGS; ++i) {
        view->pings[i].age_seconds = RADAR_PING_SECONDS;
    }
    s_previous_rect_count = 0;
    s_static_ready = false;
    if (renderer_init()) {
        render_static_full(panel);
    }
}

void radar_view_restore(esp_lcd_panel_handle_t panel)
{
    s_previous_rect_count = 0;
    if (s_dma_pixels) {
        render_static_full(panel);
    }
}

void radar_view_set_cursor(radar_view_t *view, int x, int y)
{
    if (x < 0) {
        x = 0;
    } else if (x >= LCD_WIDTH) {
        x = LCD_WIDTH - 1;
    }
    if (y < 0) {
        y = 0;
    } else if (y >= LCD_HEIGHT) {
        y = LCD_HEIGHT - 1;
    }
    view->cursor_x = x;
    view->cursor_y = y;
}

void radar_view_set_people(radar_view_t *view,
                           const radar_person_t people[RADAR_VIEW_MAX_PEOPLE],
                           int16_t radial_acceleration_mm_per_second_squared)
{
    view->radial_acceleration_mm_per_second_squared =
        radial_acceleration_mm_per_second_squared;
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        view->people_target_active[index] = people[index].active;
        view->people_target_x[index] = people[index].x_mm;
        view->people_target_y[index] = people[index].y_mm;
        if (people[index].active && !view->people_initialized[index]) {
            view->people[index] = people[index];
            view->people_initialized[index] = true;
        }
    }
}

void radar_view_trigger_ping(radar_view_t *view)
{
    for (int index = 0; index < RADAR_VIEW_MAX_PEOPLE; ++index) {
        const radar_person_t *person = &view->people[index];
        int radar_x;
        int radar_y;
        if (!person->active
            || !person_screen_position(person, &radar_x, &radar_y)) {
            continue;
        }

        int slot = next_ping_slot(view);
        view->pings[slot] = (radar_ping_t) {
            .x = radar_x,
            .y = radar_y,
            .age_seconds = 0.0f,
        };
    }
}

void radar_view_step(esp_lcd_panel_handle_t panel, radar_view_t *view)
{
    int64_t now = radar_now_us();
    float dt = (float)(now - view->last_step_us) / 1000000.0f;
    view->last_step_us = now;
    if (dt <= 0.0f || dt > 0.1f) {
        dt = 1.0f / 30.0f;
    }
    update_people(view, dt);
    for (int i = 0; i < RADAR_VIEW_MAX_PINGS; ++i) {
        radar_ping_t *ping = &view->pings[i];
        if (ping->age_seconds < RADAR_PING_SECONDS) {
            ping->age_seconds += dt;
        }
    }

    if (!s_dma_pixels) {
        return;
    }
    overlay_t overlay;
    prepare_overlay(view, &overlay);
    dirty_rect_t current_rects[MAX_DYNAMIC_RECTS];
    int current_count;
    collect_dynamic_rects(view, &overlay, current_rects, &current_count);

    dirty_rect_t redraw_rects[MAX_DYNAMIC_RECTS];
    int redraw_count = 0;
    for (int index = 0; index < s_previous_rect_count; ++index) {
        add_dirty_rect(redraw_rects, &redraw_count, s_previous_rects[index]);
    }
    for (int index = 0; index < current_count; ++index) {
        add_dirty_rect(redraw_rects, &redraw_count, current_rects[index]);
    }
    for (int index = 0; index < redraw_count; ++index) {
        render_dirty_rect(panel, view, &overlay, redraw_rects[index]);
    }
    memcpy(s_previous_rects, current_rects, current_count * sizeof(current_rects[0]));
    s_previous_rect_count = current_count;
}
