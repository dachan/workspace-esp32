#include "catalog.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "front_title.h"

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const char *const chatgpt_models[] = {
    "GPT-6 Astra", "GPT-6 Sol", "GPT-6 Luna", "GPT-5.6 Sol", "GPT-5.6 Terra",
    "GPT-5.6 Luna", "GPT-5.5",
};
/* OpenCode encoder order. Its native picker remains Luna-first. */
static const char *const opencode_models[] = {
    "GPT-6 Astra", "GPT-5.6 Terra", "GPT-5.6 Sol", "GPT-5.6 Luna",
};
/* Encoder slots are OpenRouter Latest aliases. Labels keep the slug provider and omit Latest. */
static const char *const rig_models[] = {
    "xAI Grok",
    "OpenAI Terra",
    "OpenAI Sol",
    "OpenAI Luna",
    "OpenAI Astra",
    "Moonshot Kimi",
    "Google Pro",
    "Google Gemini Flash",
    "DeepSeek Flash",
    "Anthropic Sonnet",
    "Anthropic Opus",
    "Anthropic Fable",
};
static const char *const chatgpt_thinking[] = {
    "Light", "Medium", "High", "Extra High", "Max", "Ultra",
};
/* Same labels as Rig composer: Auto plus OpenRouter none/low/medium/high/xhigh/max. */
static const char *const rig_thinking[] = {
    "Auto", "None", "Low", "Medium", "High", "X-High", "Max",
};
/* Same ladder as Rig: Auto through Max whenever the model has reasoning. */
static const uint8_t rig_default_masks[] = {
    0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
};
_Static_assert(COUNT(rig_default_masks) == COUNT(rig_models), "rig effort masks must match models");
static uint8_t s_rig_effort_mask[COUNT(rig_models)];
static uint8_t s_rig_host_mask;
static char s_rig_host_name[96];
static const char *s_rig_active[COUNT(rig_thinking)];
static int s_rig_active_count;

#define RIG_HOST_MAX 48
#define RIG_HOST_NAME 80
static char s_rig_live[RIG_HOST_MAX][RIG_HOST_NAME];
static uint8_t s_rig_live_effort[RIG_HOST_MAX];
static int s_rig_live_count;
static char s_rig_build[RIG_HOST_MAX][RIG_HOST_NAME];
static uint8_t s_rig_build_effort[RIG_HOST_MAX];
static int s_rig_build_count;
static bool s_rig_building;
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
/* Auto plus the seven Model Dial Settings entries that ship enabled. */
#define CURSOR_ENABLED_DEFAULT 0xFFull

/* Stable Cursor configuration-mask slots. */
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
    {"Grok 4.7", THINK(think_lmhx)},
};
/*
 * Keep configuration-mask bits stable while placing the latest Cursor model
 * beside Auto on the physical dial.
 */
static const uint8_t cursor_panel_order[] = {
    0, 37,
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
    35, 36,
};
_Static_assert(COUNT(cursor_panel_order) == COUNT(cursor_models),
               "cursor panel order must cover every model");

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

static bool starts_with_token(const char *text, const char *token, const char **rest)
{
    size_t n = token ? strlen(token) : 0;
    if (!text || n == 0 || strncasecmp(text, token, n) != 0 || text[n] != ' ' || !text[n + 1]) {
        return false;
    }
    *rest = text + n + 1;
    while (**rest == ' ' || **rest == '\t') {
        (*rest)++;
    }
    return **rest != '\0';
}

/* Matching helper: drop a leading `Provider: ` or `Provider ` so host and panel names align. */
void catalog_rig_display_name(const char *name, char *out, size_t out_sz)
{
    static const char *const titles[] = {
        "Cognitive Computations", "Hugging Face", "01.AI", "OpenRouter",
        "Anthropic", "DeepSeek", "Moonshot", "MiniMax", "OpenAI", "Google",
        "Amazon", "NVIDIA", "Mistral", "Perplexity", "Microsoft", "Inception",
        "Arcee", "Cohere", "Groq", "Morph", "Meta", "Qwen", "AI21", "IBM",
        "xAI", "Z.ai",
    };
    if (!out || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    if (!name) {
        return;
    }
    while (*name == ' ' || *name == '\t') {
        name++;
    }
    const char *rest = name;
    char provider[40];
    provider[0] = '\0';
    const char *colon = strchr(name, ':');
    if (colon && colon > name && (colon - name) <= 40 && colon[1] == ' ') {
        size_t plen = (size_t)(colon - name);
        memcpy(provider, name, plen);
        provider[plen] = '\0';
        rest = colon + 2;
        while (*rest == ' ' || *rest == '\t') {
            rest++;
        }
    }
    const char *stripped = rest;
    if (provider[0]) {
        starts_with_token(rest, provider, &stripped);
    } else {
        for (int i = 0; i < (int)(sizeof(titles) / sizeof(titles[0])); i++) {
            if (starts_with_token(name, titles[i], &stripped)) {
                break;
            }
        }
    }
    snprintf(out, out_sz, "%s", stripped);
    size_t n = strlen(out);
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t')) {
        out[--n] = '\0';
    }
    const char latest[] = " Latest";
    size_t ln = sizeof(latest) - 1;
    if (n > ln && strcasecmp(out + n - ln, latest) == 0) {
        out[n - ln] = '\0';
    }
}

static bool rig_labels_match(const char *a, const char *b)
{
    if (!a || !b || !a[0] || !b[0]) {
        return false;
    }
    if (strcasecmp(a, b) == 0) {
        return true;
    }
    char sa[96];
    char sb[96];
    catalog_rig_display_name(a, sa, sizeof(sa));
    catalog_rig_display_name(b, sb, sizeof(sb));
    return (sa[0] && strcasecmp(sa, b) == 0)
        || (sb[0] && strcasecmp(a, sb) == 0)
        || (sa[0] && sb[0] && strcasecmp(sa, sb) == 0);
}

static int rig_full_index(const char *name)
{
    if (!name || !name[0]) {
        return -1;
    }
    for (int i = 0; i < COUNT(rig_models); i++) {
        if (rig_labels_match(rig_models[i], name)) {
            return i;
        }
    }
    return -1;
}

static int rig_name_index_exact(const char (*slots)[RIG_HOST_NAME], int count, const char *name)
{
    if (!name || !name[0] || count <= 0) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (strcasecmp(slots[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static int rig_name_index(const char (*slots)[RIG_HOST_NAME], int count, const char *name)
{
    if (!name || !name[0] || count <= 0) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (rig_labels_match(slots[i], name)) {
            return i;
        }
    }
    return -1;
}

void catalog_rig_catalog_begin(void)
{
    s_rig_building = true;
    s_rig_build_count = 0;
}

void catalog_rig_catalog_add(const char *name, uint8_t effort_mask)
{
    if (!s_rig_building) {
        catalog_rig_catalog_begin();
    }
    if (!name || !name[0] || s_rig_build_count >= RIG_HOST_MAX) {
        return;
    }
    if (rig_name_index_exact(s_rig_build, s_rig_build_count, name) >= 0) {
        return;
    }
    snprintf(s_rig_build[s_rig_build_count], RIG_HOST_NAME, "%s", name);
    s_rig_build_effort[s_rig_build_count] = effort_mask;
    s_rig_build_count++;
}

static void sort_rig_build(void)
{
    for (int i = 0; i + 1 < s_rig_build_count; i++) {
        int best = i;
        for (int j = i + 1; j < s_rig_build_count; j++) {
            if (strcasecmp(s_rig_build[j], s_rig_build[best]) > 0) {
                best = j;
            }
        }
        if (best == i) {
            continue;
        }
        char name[RIG_HOST_NAME];
        memcpy(name, s_rig_build[i], RIG_HOST_NAME);
        memcpy(s_rig_build[i], s_rig_build[best], RIG_HOST_NAME);
        memcpy(s_rig_build[best], name, RIG_HOST_NAME);
        uint8_t mask = s_rig_build_effort[i];
        s_rig_build_effort[i] = s_rig_build_effort[best];
        s_rig_build_effort[best] = mask;
    }
}

void catalog_rig_catalog_commit(void)
{
    if (!s_rig_building) {
        return;
    }
    sort_rig_build();
    for (int i = 0; i < s_rig_build_count; i++) {
        snprintf(s_rig_live[i], RIG_HOST_NAME, "%s", s_rig_build[i]);
        s_rig_live_effort[i] = s_rig_build_effort[i];
    }
    s_rig_live_count = s_rig_build_count;
    s_rig_building = false;
    s_rig_build_count = 0;
}

static uint8_t rig_mask_for(const char *model)
{
    int live = rig_name_index_exact(s_rig_live, s_rig_live_count, model);
    if (live < 0) {
        live = rig_name_index(s_rig_live, s_rig_live_count, model);
    }
    if (live >= 0) {
        return s_rig_live_effort[live];
    }
    int slot = rig_full_index(model);
    if (slot >= 0) {
        if (s_rig_effort_mask[slot]) {
            return s_rig_effort_mask[slot];
        }
        return slot < COUNT(rig_default_masks) ? rig_default_masks[slot] : 0x7F;
    }
    if (s_rig_host_name[0] && model && strcasecmp(model, s_rig_host_name) == 0 && s_rig_host_mask) {
        return s_rig_host_mask;
    }
    return 0x7F;
}

static const char *const *rig_thinking_table(const char *model, int *count)
{
    uint8_t mask = rig_mask_for(model);
    s_rig_active_count = 0;
    if (mask == 0) {
        *count = 0;
        return rig_thinking;
    }
    for (int i = 0; i < COUNT(rig_thinking); i++) {
        if (mask & (1u << i)) {
            s_rig_active[s_rig_active_count++] = rig_thinking[i];
        }
    }
    if (s_rig_active_count == 0) {
        s_rig_active[0] = rig_thinking[0];
        s_rig_active_count = 1;
    }
    *count = s_rig_active_count;
    return s_rig_active;
}

void catalog_rig_set_effort_masks(const uint8_t *masks, int n)
{
    if (!masks || n <= 0) {
        return;
    }
    int limit = n < COUNT(rig_models) ? n : COUNT(rig_models);
    for (int i = 0; i < limit; i++) {
        s_rig_effort_mask[i] = masks[i];
    }
}

void catalog_rig_set_host_effort_mask(const char *model, uint8_t mask)
{
    int slot = rig_full_index(model);
    if (slot >= 0) {
        s_rig_effort_mask[slot] = mask;
        return;
    }
    if (!model || !model[0]) {
        s_rig_host_name[0] = '\0';
        s_rig_host_mask = 0;
        return;
    }
    snprintf(s_rig_host_name, sizeof(s_rig_host_name), "%s", model);
    s_rig_host_mask = mask;
}

static uint64_t s_enabled = CURSOR_ENABLED_DEFAULT;
#define RIG_ENABLED_DEFAULT ((1ull << COUNT(rig_models)) - 1ull)
static uint64_t s_rig_enabled = RIG_ENABLED_DEFAULT;
static uint64_t s_chatgpt_thinking_enabled = 0x0Full;
static const char *s_chatgpt_thinking_active[COUNT(chatgpt_thinking)];
static int s_chatgpt_thinking_active_count;
static bool s_chatgpt_thinking_active_ready;

static void refresh_chatgpt_thinking(void)
{
    s_chatgpt_thinking_active_count = 0;
    for (int i = 0; i < COUNT(chatgpt_thinking); i++) {
        if (s_chatgpt_thinking_enabled & (1ull << i)) {
            s_chatgpt_thinking_active[s_chatgpt_thinking_active_count++] = chatgpt_thinking[i];
        }
    }
    if (s_chatgpt_thinking_active_count == 0) {
        s_chatgpt_thinking_enabled = 1ull;
        s_chatgpt_thinking_active[0] = chatgpt_thinking[0];
        s_chatgpt_thinking_active_count = 1;
    }
    s_chatgpt_thinking_active_ready = true;
}

static const char *const *chatgpt_thinking_table(int *count)
{
    if (!s_chatgpt_thinking_active_ready) {
        refresh_chatgpt_thinking();
    }
    *count = s_chatgpt_thinking_active_count;
    return s_chatgpt_thinking_active;
}

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
    for (int i = 0; i < COUNT(cursor_panel_order); i++) {
        if (cursor_on(cursor_panel_order[i])) {
            n++;
        }
    }
    return n;
}

static int cursor_full_at_enabled(int enabled_index)
{
    int n = 0;
    for (int i = 0; i < COUNT(cursor_panel_order); i++) {
        int full_index = cursor_panel_order[i];
        if (!cursor_on(full_index)) {
            continue;
        }
        if (n == enabled_index) {
            return full_index;
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
    for (int i = 0; i < COUNT(cursor_panel_order); i++) {
        int panel_index = cursor_panel_order[i];
        if (panel_index == full_index) {
            return n;
        }
        if (cursor_on(panel_index)) n++;
    }
    return -1;
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
    return chatgpt_thinking_table(count);
}

static const char *const *active_thinking(const char *model, int *count)
{
    if (front_title_app() == DESK_OPENCODE) {
        *count = 0;
        return chatgpt_thinking;
    }
    if (front_title_app() == DESK_RIG) {
        return rig_thinking_table(model, count);
    }
    return thinking_table(front_title_is_cursor(), model, count);
}

static uint64_t rig_mask_limit(void)
{
    return COUNT(rig_models) >= 64 ? ~0ull : ((1ull << COUNT(rig_models)) - 1ull);
}

static bool rig_on(int full)
{
    if (full < 0 || full >= COUNT(rig_models) || full >= 64) {
        return false;
    }
    return (s_rig_enabled & (1ull << full)) != 0;
}

static int rig_enabled_count(void)
{
    int n = 0;
    for (int i = 0; i < COUNT(rig_models); i++) {
        if (rig_on(i)) n++;
    }
    return n > 0 ? n : COUNT(rig_models);
}

static int rig_full_at_enabled(int enabled_index)
{
    int n = 0;
    for (int i = 0; i < COUNT(rig_models); i++) {
        if (!rig_on(i)) continue;
        if (n == enabled_index) return i;
        n++;
    }
    return 0;
}

static int rig_enabled_index(int full)
{
    if (!rig_on(full)) return -1;
    int n = 0;
    for (int i = 0; i < full; i++) {
        if (rig_on(i)) n++;
    }
    return n;
}

int catalog_model_count(void)
{
    return catalog_model_count_for(front_title_app());
}

int catalog_model_count_for(desk_app_t app)
{
    if (app == DESK_OPENCODE) return COUNT(opencode_models);
    if (app == DESK_RIG) {
        if (s_rig_live_count > 0) return s_rig_live_count;
        return rig_enabled_count();
    }
    return catalog_model_count_in(app == DESK_CURSOR);
}

int catalog_model_count_in(bool cursor)
{
    int count;
    models_table(cursor, &count);
    return count;
}

const char *catalog_model_at(int index)
{
    return catalog_model_at_for(front_title_app(), index);
}

const char *catalog_model_at_for(desk_app_t app, int index)
{
    if (app == DESK_OPENCODE) {
        if (index < 0 || index >= COUNT(opencode_models)) return opencode_models[0];
        return opencode_models[index];
    }
    if (app == DESK_RIG) {
        if (s_rig_live_count > 0) {
            if (index < 0 || index >= s_rig_live_count) return s_rig_live[0];
            return s_rig_live[index];
        }
        if (index < 0 || index >= rig_enabled_count()) return rig_models[rig_full_at_enabled(0)];
        return rig_models[rig_full_at_enabled(index)];
    }
    return catalog_model_at_in(app == DESK_CURSOR, index);
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
    return catalog_model_index_for(front_title_app(), name);
}

int catalog_model_index_for(desk_app_t app, const char *name)
{
    if (app == DESK_OPENCODE) {
        return table_count(opencode_models, COUNT(opencode_models), name);
    }
    if (app == DESK_RIG) {
        if (s_rig_live_count > 0) {
            return rig_name_index(s_rig_live, s_rig_live_count, name);
        }
        return rig_enabled_index(rig_full_index(name));
    }
    return catalog_model_index_in(app == DESK_CURSOR, name);
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

uint64_t catalog_chatgpt_thinking_mask(void)
{
    return s_chatgpt_thinking_enabled;
}

void catalog_chatgpt_set_thinking_mask(uint64_t mask)
{
    const uint64_t limit = (1ull << COUNT(chatgpt_thinking)) - 1ull;
    s_chatgpt_thinking_enabled = mask & limit;
    refresh_chatgpt_thinking();
}

uint64_t catalog_rig_enabled_mask(void)
{
    uint64_t mask = s_rig_enabled & rig_mask_limit();
    return mask ? mask : 1ull;
}

void catalog_rig_set_enabled_mask(uint64_t mask)
{
    s_rig_enabled = mask & rig_mask_limit();
    if (s_rig_enabled == 0) {
        s_rig_enabled = 1ull;
    }
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
    return cursor_index(name) >= 0
        || table_count(opencode_models, COUNT(opencode_models), name) >= 0
        || rig_name_index(s_rig_live, s_rig_live_count, name) >= 0
        || rig_full_index(name) >= 0;
}

const char *catalog_default_model(void)
{
    return catalog_default_model_for(front_title_app());
}

const char *catalog_default_model_for(desk_app_t app)
{
    if (app == DESK_OPENCODE) return "GPT-5.6 Luna";
    if (app == DESK_RIG) {
        if (s_rig_live_count > 0) return s_rig_live[0];
        int idx = catalog_model_index_for(app, "xAI Grok");
        if (idx < 0) idx = catalog_model_index_for(app, "Grok");
        return idx >= 0 ? catalog_model_at_for(app, idx) : catalog_model_at_for(app, 0);
    }
    return catalog_default_model_in(app == DESK_CURSOR);
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
    return catalog_thinking_count_for(front_title_app(), model);
}

int catalog_thinking_count_for(desk_app_t app, const char *model)
{
    if (app == DESK_OPENCODE) {
        return 0;
    }
    if (app == DESK_RIG) {
        int count;
        rig_thinking_table(model, &count);
        return count;
    }
    return catalog_thinking_count_in(app == DESK_CURSOR, model);
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
    if (front_title_app() == DESK_RIG) {
        if (strcasecmp(name, "Light") == 0) {
            return catalog_thinking_level(model, "Low");
        }
        if (strcasecmp(name, "Extra High") == 0 || strcasecmp(name, "xhigh") == 0
            || strcasecmp(name, "x-high") == 0) {
            return catalog_thinking_level(model, "X-High");
        }
        if (strcasecmp(name, "Minimal") == 0) {
            return catalog_thinking_level(model, "None");
        }
        return 0;
    }
    if (!front_title_is_cursor()) {
        int full = table_count(chatgpt_thinking, COUNT(chatgpt_thinking), name);
        if (full >= 0) {
            for (int i = full; i >= 0; i--) {
                if ((s_chatgpt_thinking_enabled & (1ull << i)) == 0) {
                    continue;
                }
                return table_count(names, count, chatgpt_thinking[i]) + 1;
            }
            for (int i = full + 1; i < COUNT(chatgpt_thinking); i++) {
                if ((s_chatgpt_thinking_enabled & (1ull << i)) == 0) {
                    continue;
                }
                return table_count(names, count, chatgpt_thinking[i]) + 1;
            }
        }
    }
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
        if (front_title_app() == DESK_RIG) {
            return names[0];
        }
        return names[count > 1 ? 1 : 0];
    }
    return names[level - 1];
}

const char *catalog_default_thinking(const char *model)
{
    return catalog_default_thinking_for(front_title_app(), model);
}

const char *catalog_default_thinking_for(desk_app_t app, const char *model)
{
    if (app == DESK_OPENCODE) {
        return "Unsupported";
    }
    if (app == DESK_RIG) {
        return "Auto";
    }
    return catalog_default_thinking_in(app == DESK_CURSOR, model);
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
    return table_count(cursor_thinking, COUNT(cursor_thinking), name) >= 0
        || table_count(rig_thinking, COUNT(rig_thinking), name) >= 0;
}
