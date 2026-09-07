#pragma once

#define THINKING_LEVEL_COUNT 4

extern const char *const catalog_models[];
extern const int catalog_model_count;

int catalog_model_index(const char *name);
// UI levels are 1..4; zero means missing or unknown.
int catalog_thinking_level(const char *name);
const char *catalog_thinking_name(int level);
