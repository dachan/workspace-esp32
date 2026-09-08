#pragma once

#include <stdint.h>

#include "canvas.h"

// 5x7 glyph cells drawn at `scale` (1 = 5x7, 2 = 10x14, ...).
void font_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
void font_draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
int font_text_width(const char *text, int scale);
