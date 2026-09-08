#pragma once

#include <stdbool.h>
#include <stddef.h>

bool clock_apply_line(const char *line);
bool clock_format(char *buf, size_t n);
bool clock_format_date(char *buf, size_t n);
bool clock_needs_paint(void);
