#include "catalog.h"

#include <stddef.h>
#include <stdint.h>
#include <strings.h>

#include "front_title.h"

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const char *const chatgpt_models[] = {
    "GPT-6 Astra", "GPT-5.6 Sol", "GPT-5.6 Terra", "GPT-5.6 Luna", "GPT-5.5",
};
static const char *const chatgpt_thinking[] = {"Light", "Medium", "High", "Extra High"};
static const char *const cursor_thinking[] = {
    "None", "Minimal", "Low", "Medium", "High", "Extra High", "Max",
};

/* Effort families from Cursor's AvailableModels parameterDefinitions. */
static const char *const think_lmh[] = {"Low", "Medium", "High"};
static const char *const think_min_lmh[] = {"Minimal", "Low", "Medium", "High"};
static const char *const think_lmhx[] = {"Low", "Medium", "High", "Extra High"};
static const char *const think_lmhm[] = {"Low", "Medium", "High", "Max"};
static const char *const think_lmhxm[] = {"Low", "Medium", "High", "Extra High", "Max"};
static const char *const think_nlmhx[] = {"None", "Low", "Medium", "High", "Extra High"};
static const char *const think_nlmhxm[] = {
    "None", "Low", "Medium", "High", "Extra High", "Max",
};
static const char *const think_lhm[] = {"Low", "High", "Max"};
static const char *const think_hm[] = {"High", "Max"};

typedef struct {
    const char *name;
    const char *const *thinking;
    uint8_t thinking_count;
} cursor_model_t;

#define THINK(arr) arr, (uint8_t)COUNT(arr)
#define THINK_NONE NULL, 0
/* Auto plus the seven Cursor Settings toggles that ship enabled. */
#define CURSOR_ENABLED_DEFAULT 0xFFull

/* Auto, then Cursor Settings toggle order (enabled group, then the rest). */
static const cursor_model_t cursor_models[] = {
    {"Auto", THINK_NONE},
    {"Cursor Grok 4.6", THINK(think_lmhx)},
    {"Composer 2.5", THINK_NONE},
    {"Claude Opus 5", THINK(think_lmhxm)},
    {"GPT-5.6 Sol", THINK(think_nlmhxm)},
    {"Claude Fable 5", THINK(think_lmhxm)},
    {"GPT-5.6 Terra", THINK(think_nlmhxm)},
    {"GPT-5.6 Luna", THINK(think_nlmhxm)},
    {"Claude Opus 4.8", THINK(think_lmhxm)},
    {"GPT-5.5", THINK(think_nlmhx)},
    {"Claude Fable 5.1", THINK(think_lmhxm)},
    {"Cursor Grok 4.5", THINK(think_lmh)},
    {"Gemini 3.8 Flash", THINK(think_lmh)},
    {"Gemini 3.7 Flash", THINK(think_lmh)},
    {"Claude Sonnet 5", THINK(think_lmhxm)},
    {"Claude Sonnet 4.6", THINK(think_lmhm)},
    {"Codex 5.3", THINK(think_lmhx)},
    {"Claude Opus 4.7", THINK(think_lmhxm)},
    {"GPT-5.4", THINK(think_nlmhx)},
    {"Claude Opus 4.6", THINK(think_lmhm)},
    {"Claude Opus 4.5", THINK_NONE},
    {"GPT-5.2", THINK(think_lmhx)},
    {"Gemini 3.6 Flash", THINK(think_min_lmh)},
    {"Gemini 3.1 Pro", THINK_NONE},
    {"GPT-5.4 Mini", THINK(think_nlmhx)},
    {"GPT-5.4 Nano", THINK(think_nlmhx)},
    {"Claude Haiku 4.5", THINK_NONE},
    {"Claude Sonnet 4.5", THINK_NONE},
    {"GPT-5.1", THINK(think_lmh)},
    {"Gemini 3 Flash", THINK_NONE},
    {"Gemini 3.5 Flash", THINK_NONE},
    {"Claude Sonnet 4", THINK_NONE},
    {"GPT-5 Mini", THINK_NONE},
    {"Gemini 2.5 Flash", THINK_NONE},
    {"Kimi K3", THINK(think_lhm)},
    {"Kimi K2.7 Code", THINK_NONE},
    {"GLM 5.2", THINK(think_hm)},
};

static int table_count(const char *const *names, int n, const char *name)
{
    if (!name) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (strcasecmp(name, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static uint64_t s_enabled = CURSOR_ENABLED_DEFAULT;

static int cursor_index(const char *name)
{
    if (!name) {
        return -1;
    }
    for (int i = 0; i < COUNT(cursor_models); i++) {
        if (strcasecmp(name, cursor_models[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

static uint64_t cursor_mask_limit(void)
{
    return COUNT(cursor_models) >= 64 ? ~0ull : ((1ull << COUNT(cursor_models)) - 1ull);
}

static bool cursor_on(int full_index)
{
    if (full_index <= 0) {
        return true;
    }
    if (full_index >= 64) {
        return false;
    }
    return (s_enabled & (1ull << full_index)) != 0;
}

static int cursor_enabled_count(void)
{
    int n = 0;
    for (int i = 0; i < COUNT(cursor_models); i++) {
        if (cursor_on(i)) {
            n++;
        }
    }
    return n;
}

static int cursor_full_at_enabled(int enabled_index)
{
    int n = 0;
    for (int i = 0; i < COUNT(cursor_models); i++) {
        if (!cursor_on(i)) {
            continue;
        }
        if (n == enabled_index) {
            return i;
        }
        n++;
    }
    return 0;
}

static int cursor_enabled_index(int full_index)
{
    if (!cursor_on(full_index)) {
        return -1;
    }
    int n = 0;
    for (int i = 0; i < full_index; i++) {
        if (cursor_on(i)) {
            n++;
        }
    }
    return n;
}

static const char *const *models_table(bool cursor, int *count)
{
    if (cursor) {
        *count = cursor_enabled_count();
        return NULL;
    }
    *count = COUNT(chatgpt_models);
    return chatgpt_models;
}

static const char *const *thinking_table(bool cursor, const char *model, int *count)
{
    if (cursor) {
        int index = cursor_index(model);
        if (index < 0) {
            *count = 0;
            return cursor_thinking;
        }
        *count = cursor_models[index].thinking_count;
        return *count ? cursor_models[index].thinking : cursor_thinking;
    }
    *count = COUNT(chatgpt_thinking);
    return chatgpt_thinking;
}

static const char *const *active_thinking(const char *model, int *count)
{
    return thinking_table(front_title_is_cursor(), model, count);
}

int catalog_model_count(void)
{
    return catalog_model_count_in(front_title_is_cursor());
}

int catalog_model_count_in(bool cursor)
{
    int count;
    models_table(cursor, &count);
    return count;
}

const char *catalog_model_at(int index)
{
    return catalog_model_at_in(front_title_is_cursor(), index);
}

const char *catalog_model_at_in(bool cursor, int index)
{
    int count;
    const char *const *names = models_table(cursor, &count);
    if (index < 0 || index >= count) {
        return cursor ? cursor_models[0].name : names[0];
    }
    return cursor ? cursor_models[cursor_full_at_enabled(index)].name : names[index];
}

int catalog_model_index(const char *name)
{
    return catalog_model_index_in(front_title_is_cursor(), name);
}

int catalog_model_index_in(bool cursor, const char *name)
{
    if (cursor) {
        int full = cursor_index(name);
        return full < 0 ? -1 : cursor_enabled_index(full);
    }
    int count;
    const char *const *names = models_table(false, &count);
    return table_count(names, count, name);
}

uint64_t catalog_cursor_enabled_mask(void)
{
    return s_enabled | 1ull;
}

void catalog_cursor_set_enabled_mask(uint64_t mask)
{
    s_enabled = (mask | 1ull) & cursor_mask_limit();
}

int catalog_cursor_slot_count(void)
{
    return COUNT(cursor_models) - 1;
}

const char *catalog_cursor_slot_name(int slot)
{
    int full = slot + 1;
    if (full <= 0 || full >= COUNT(cursor_models)) {
        return cursor_models[1].name;
    }
    return cursor_models[full].name;
}

bool catalog_cursor_slot_on(int slot)
{
    return cursor_on(slot + 1);
}

bool catalog_cursor_set_slot(int slot, bool on)
{
    int full = slot + 1;
    if (full <= 0 || full >= COUNT(cursor_models) || full >= 64) {
        return false;
    }
    uint64_t bit = 1ull << full;
    uint64_t next = on ? (s_enabled | bit) : (s_enabled & ~bit);
    next = (next | 1ull) & cursor_mask_limit();
    if (next == s_enabled) {
        return false;
    }
    s_enabled = next;
    return true;
}

bool catalog_model_known(const char *name)
{
    if (table_count(chatgpt_models, COUNT(chatgpt_models), name) >= 0) {
        return true;
    }
    return cursor_index(name) >= 0;
}

const char *catalog_default_model(void)
{
    return catalog_default_model_in(front_title_is_cursor());
}

const char *catalog_default_model_in(bool cursor)
{
    const char *want = cursor ? "Cursor Grok 4.6" : "GPT-5.6 Luna";
    return catalog_model_index_in(cursor, want) >= 0
        ? want
        : catalog_model_at_in(cursor, 0);
}

int catalog_thinking_count(const char *model)
{
    return catalog_thinking_count_in(front_title_is_cursor(), model);
}

int catalog_thinking_count_in(bool cursor, const char *model)
{
    int count;
    thinking_table(cursor, model, &count);
    return count;
}

int catalog_thinking_level(const char *model, const char *name)
{
    if (!name || !name[0]) {
        return 0;
    }
    int count;
    const char *const *names = active_thinking(model, &count);
    int index = table_count(names, count, name);
    if (index >= 0) {
        return index + 1;
    }
    if (count == 0) return 0;
    // Preserve names across app/model changes; clamp unsupported endpoints.
    if (strcasecmp(name, "None") == 0 || strcasecmp(name, "Low") == 0
        || strcasecmp(name, "Light") == 0 || strcasecmp(name, "Minimal") == 0) {
        int low = table_count(names, count, front_title_is_cursor() ? "Low" : "Light");
        if (low < 0) {
            return 1;
        }
        return low + 1;
    }
    if (strcasecmp(name, "Max") == 0) return count;
    static const struct { const char *alias; const char *name; } aliases[] = {
        {"minimal", "Low"}, {"instant", "Low"}, {"fast", "Low"},
        {"standard", "Medium"}, {"auto", "Medium"},
        {"advanced", "High"}, {"thinking", "High"},
        {"xhigh", "Extra High"}, {"ultra", "Extra High"}, {"heavy", "Extra High"},
    };
    for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
        if (strcasecmp(name, aliases[i].alias) == 0) {
            return catalog_thinking_level(model, aliases[i].name);
        }
    }
    return 0;
}

const char *catalog_thinking_name(const char *model, int level)
{
    int count;
    const char *const *names = active_thinking(model, &count);
    if (count == 0) return "Unsupported";
    if (level < 1 || level > count) {
        return names[count > 1 ? 1 : 0];
    }
    return names[level - 1];
}

const char *catalog_default_thinking(const char *model)
{
    return catalog_default_thinking_in(front_title_is_cursor(), model);
}

const char *catalog_default_thinking_in(bool cursor, const char *model)
{
    int count;
    const char *const *names = thinking_table(cursor, model, &count);
    if (count == 0) return "Unsupported";
    int index = table_count(names, count, "Extra High");
    if (index >= 0) {
        return names[index];
    }
    return names[count - 1];
}

bool catalog_thinking_known(const char *name)
{
    if (table_count(chatgpt_thinking, COUNT(chatgpt_thinking), name) >= 0) {
        return true;
    }
    return table_count(cursor_thinking, COUNT(cursor_thinking), name) >= 0;
}
