#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "front_title.h"

int catalog_model_count(void);
int catalog_model_count_in(bool cursor);
const char *catalog_model_at(int index);
const char *catalog_model_at_in(bool cursor, int index);
int catalog_model_index(const char *name);
int catalog_model_index_in(bool cursor, const char *name);
bool catalog_model_known(const char *name);
const char *catalog_default_model(void);
const char *catalog_default_model_in(bool cursor);
int catalog_model_count_for(desk_app_t app);
const char *catalog_model_at_for(desk_app_t app, int index);
int catalog_model_index_for(desk_app_t app, const char *name);
const char *catalog_default_model_for(desk_app_t app);
int catalog_thinking_count_for(desk_app_t app, const char *model);
const char *catalog_default_thinking_for(desk_app_t app, const char *model);

/* Cursor dial enable mask. Auto is always on. Default is the Settings "on" set. */
uint64_t catalog_cursor_enabled_mask(void);
void catalog_cursor_set_enabled_mask(uint64_t mask);
int catalog_cursor_slot_count(void);
const char *catalog_cursor_slot_name(int slot);
bool catalog_cursor_slot_on(int slot);
bool catalog_cursor_set_slot(int slot, bool on);

/* ChatGPT/OpenCode thinking enable mask. At least one effort is always on. */
uint64_t catalog_chatgpt_thinking_mask(void);
void catalog_chatgpt_set_thinking_mask(uint64_t mask);

int catalog_thinking_count(const char *model);
int catalog_thinking_count_in(bool cursor, const char *model);
/* Segment bar: this model's full thinking list (ChatGPT/OpenCode always 6, not the enable mask). */
int catalog_thinking_bar_count(const char *model);
int catalog_thinking_bar_level(const char *model, const char *name);
// UI levels are 1..count; zero means missing or unknown.
int catalog_thinking_level(const char *model, const char *name);
const char *catalog_thinking_name(const char *model, int level);
const char *catalog_default_thinking(const char *model);
const char *catalog_default_thinking_in(bool cursor, const char *model);
bool catalog_thinking_known(const char *name);
