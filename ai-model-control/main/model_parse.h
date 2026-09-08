#pragma once

#include <stddef.h>

#define MODEL_PARSE_MAX 96

typedef struct {
    char model[MODEL_PARSE_MAX];
    char thinking[MODEL_PARSE_MAX];
    int has_model;
    int has_thinking;
} model_fields_t;

// Split an Accessibility-style model string into model + thinking.
// Examples:
//   "GPT-5.6 Luna Light"      -> model="GPT-5.6 Luna", thinking="Light"
//   "GPT-5.6 Luna Extra High" -> model="GPT-5.6 Luna", thinking="Extra High"
//   "Thinking"                -> model="Thinking", thinking="Thinking"
//   "GPT-5 Instant"           -> model="GPT-5", thinking="Instant"
void model_parse_name(const char *raw, model_fields_t *out);
