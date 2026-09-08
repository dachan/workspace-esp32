#include "catalog.h"

#include <stddef.h>
#include <strings.h>

#include "front_title.h"

static const char *const chatgpt_models[] = {
    "GPT-6 Astra", "GPT-5.6 Sol", "GPT-5.6 Terra", "GPT-5.6 Luna", "GPT-5.5",
};
static const char *const cursor_models[] = {
    "Auto",
    "Cursor Grok 4.6",
    "Composer 2.5",
    "Claude Opus 5",
    "GPT-5.6 Sol",
    "Claude Fable 5",
    "GPT-5.6 Terra",
    "GPT-5.6 Luna",
};
static const char *const chatgpt_thinking[] = {"Light", "Medium", "High", "Extra High"};
static const char *const cursor_thinking[] = {"None", "Low", "Medium", "High", "Extra High", "Max"};

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

static const char *const *models_table(bool cursor, int *count)
{
    if (cursor) {
        *count = (int)(sizeof(cursor_models) / sizeof(cursor_models[0]));
        return cursor_models;
    }
    *count = (int)(sizeof(chatgpt_models) / sizeof(chatgpt_models[0]));
    return chatgpt_models;
}

static const char *const *thinking_table(bool cursor, const char *model, int *count)
{
    if (cursor) {
        int n = (int)(sizeof(cursor_models) / sizeof(cursor_models[0]));
        int index = table_count(cursor_models, n, model);
        if (index == 0 || index == 2 || index < 0) {
            *count = 0;
            return cursor_thinking;
        }
        if (index == 1 || index == 3 || index == 5) {
            *count = index == 1 ? 4 : 5;
            return cursor_thinking + 1;
        }
        *count = 6;
        return cursor_thinking;
    }
    *count = (int)(sizeof(chatgpt_thinking) / sizeof(chatgpt_thinking[0]));
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
        return names[0];
    }
    return names[index];
}

int catalog_model_index(const char *name)
{
    return catalog_model_index_in(front_title_is_cursor(), name);
}

int catalog_model_index_in(bool cursor, const char *name)
{
    int count;
    const char *const *names = models_table(cursor, &count);
    return table_count(names, count, name);
}

bool catalog_model_known(const char *name)
{
    int n = (int)(sizeof(chatgpt_models) / sizeof(chatgpt_models[0]));
    if (table_count(chatgpt_models, n, name) >= 0) {
        return true;
    }
    n = (int)(sizeof(cursor_models) / sizeof(cursor_models[0]));
    return table_count(cursor_models, n, name) >= 0;
}

const char *catalog_default_model(void)
{
    return catalog_default_model_in(front_title_is_cursor());
}

const char *catalog_default_model_in(bool cursor)
{
    int count;
    const char *const *names = models_table(cursor, &count);
    const char *want = cursor ? "Cursor Grok 4.6" : "GPT-5.6 Luna";
    return table_count(names, count, want) >= 0 ? want : names[0];
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
        || strcasecmp(name, "Light") == 0) {
        int low = table_count(names, count, front_title_is_cursor() ? "Low" : "Light");
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
    int n = (int)(sizeof(chatgpt_thinking) / sizeof(chatgpt_thinking[0]));
    if (table_count(chatgpt_thinking, n, name) >= 0) {
        return true;
    }
    n = (int)(sizeof(cursor_thinking) / sizeof(cursor_thinking[0]));
    return table_count(cursor_thinking, n, name) >= 0;
}
