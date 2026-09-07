#pragma once

#include <stdbool.h>

int catalog_model_count(void);
const char *catalog_model_at(int index);
int catalog_model_index(const char *name);
bool catalog_model_known(const char *name);

int catalog_thinking_count(const char *model);
// UI levels are 1..count; zero means missing or unknown.
int catalog_thinking_level(const char *model, const char *name);
const char *catalog_thinking_name(const char *model, int level);
bool catalog_thinking_known(const char *name);
