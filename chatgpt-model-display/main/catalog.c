#include "catalog.h"

#include <stddef.h>
#include <strings.h>

const char *const catalog_models[] = {
    "GPT-6 Astra", "GPT-5.6 Sol", "GPT-5.6 Terra", "GPT-5.6 Luna", "GPT-5.5",
};
const int catalog_model_count = sizeof(catalog_models) / sizeof(catalog_models[0]);

static const char *const thinking_names[] = {"Light", "Medium", "High", "Extra High"};
static const struct {
    const char *name;
    int level;
} thinking_aliases[] = {
    {"minimal", 1}, {"instant", 1}, {"fast", 1}, {"low", 1},
    {"standard", 2}, {"auto", 2},
    {"advanced", 3}, {"thinking", 3},
    {"xhigh", 4}, {"max", 4}, {"ultra", 4}, {"heavy", 4},
};

int catalog_model_index(const char *name)
{
    if (name) {
        for (int i = 0; i < catalog_model_count; i++) {
            if (strcasecmp(name, catalog_models[i]) == 0) {
                return i;
            }
        }
    }
    return -1;
}

int catalog_thinking_level(const char *name)
{
    if (!name || !name[0]) {
        return 0;
    }
    for (int i = 0; i < THINKING_LEVEL_COUNT; i++) {
        if (strcasecmp(name, thinking_names[i]) == 0) {
            return i + 1;
        }
    }
    for (size_t i = 0; i < sizeof(thinking_aliases) / sizeof(thinking_aliases[0]); i++) {
        if (strcasecmp(name, thinking_aliases[i].name) == 0) {
            return thinking_aliases[i].level;
        }
    }
    return 0;
}

const char *catalog_thinking_name(int level)
{
    return level >= 1 && level <= THINKING_LEVEL_COUNT ? thinking_names[level - 1] : "Medium";
}
