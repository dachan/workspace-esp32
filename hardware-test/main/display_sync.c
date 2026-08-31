#include "display_sync.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_color_transfer_done;

static bool on_color_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_io_event_data_t *event_data,
                                   void *user_context)
{
    (void)panel_io;
    (void)event_data;
    (void)user_context;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_color_transfer_done, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

esp_err_t display_sync_init(esp_lcd_panel_io_handle_t panel_io)
{
    s_color_transfer_done = xSemaphoreCreateBinary();
    if (!s_color_transfer_done) {
        return ESP_ERR_NO_MEM;
    }
    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = on_color_transfer_done,
    };
    return esp_lcd_panel_io_register_event_callbacks(panel_io, &callbacks, NULL);
}

esp_err_t display_sync_draw(esp_lcd_panel_handle_t panel, int x_start, int y_start,
                            int x_end, int y_end, const void *color_data)
{
    xSemaphoreTake(s_color_transfer_done, 0);
    esp_err_t result = esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end,
                                                  color_data);
    if (result != ESP_OK) {
        return result;
    }
    return xSemaphoreTake(s_color_transfer_done, portMAX_DELAY) == pdTRUE
        ? ESP_OK : ESP_FAIL;
}
