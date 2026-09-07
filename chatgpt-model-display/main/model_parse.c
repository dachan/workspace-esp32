#include "model_parse.h"

#include <ctype.h>
#include <string.h>

static void trim_copy(char *dst, size_t dst_sz, const char *src)
{
    while (*src && isspace((unsigned char)*src)) {
        src++;
    }
    size_t n = strlen(src);
    while (n > 0 && isspace((unsigned char)src[n - 1])) {
        n--;
    }
    if (n >= dst_sz) {
        n = dst_sz - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int streq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int is_thinking_token(const char *tok)
{
    static const char *k_tokens[] = {
        "Light",
        "Heavy",
        "Instant",
        "Fast",
        "Thinking",
        "High",
        "Medium",
        "Low",
        "Standard",
        "Advanced",
        "Max",
        "Auto",
        NULL,
    };
    for (int i = 0; k_tokens[i]; i++) {
        if (streq_ci(tok, k_tokens[i])) {
            return 1;
        }
    }
    return 0;
}

static int is_extra_high_pair(const char *a, const char *b)
{
    return streq_ci(a, "Extra") && streq_ci(b, "High");
}

void model_parse_name(const char *raw, model_fields_t *out)
{
    memset(out, 0, sizeof(*out));
    if (raw == NULL) {
        return;
    }

    char buf[MODEL_PARSE_MAX];
    trim_copy(buf, sizeof(buf), raw);
    if (buf[0] == '\0') {
        return;
    }

    // Tokenize on spaces into a small stack array.
    char tokens[16][48];
    int ntok = 0;
    const char *p = buf;
    while (*p && ntok < 16) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len >= sizeof(tokens[0])) {
            len = sizeof(tokens[0]) - 1;
        }
        memcpy(tokens[ntok], start, len);
        tokens[ntok][len] = '\0';
        ntok++;
    }

    if (ntok == 0) {
        return;
    }

    int think_from = -1;
    if (ntok >= 2 && is_extra_high_pair(tokens[ntok - 2], tokens[ntok - 1])) {
        think_from = ntok - 2;
    } else if (is_thinking_token(tokens[ntok - 1])) {
        think_from = ntok - 1;
    }

    if (think_from > 0) {
        // model = tokens[0..think_from)
        size_t pos = 0;
        for (int i = 0; i < think_from; i++) {
            size_t len = strlen(tokens[i]);
            if (pos && pos + 1 < sizeof(out->model)) {
                out->model[pos++] = ' ';
            }
            if (pos + len >= sizeof(out->model)) {
                len = sizeof(out->model) - pos - 1;
            }
            memcpy(out->model + pos, tokens[i], len);
            pos += len;
            out->model[pos] = '\0';
        }
        size_t tpos = 0;
        for (int i = think_from; i < ntok; i++) {
            size_t len = strlen(tokens[i]);
            if (tpos && tpos + 1 < sizeof(out->thinking)) {
                out->thinking[tpos++] = ' ';
            }
            if (tpos + len >= sizeof(out->thinking)) {
                len = sizeof(out->thinking) - tpos - 1;
            }
            memcpy(out->thinking + tpos, tokens[i], len);
            tpos += len;
            out->thinking[tpos] = '\0';
        }
        out->has_model = out->model[0] != '\0';
        out->has_thinking = out->thinking[0] != '\0';
        return;
    }

    // Single token that is itself a thinking style (e.g. "Thinking").
    if (ntok == 1 && is_thinking_token(tokens[0])) {
        strncpy(out->model, tokens[0], sizeof(out->model) - 1);
        strncpy(out->thinking, tokens[0], sizeof(out->thinking) - 1);
        out->has_model = 1;
        out->has_thinking = 1;
        return;
    }

    strncpy(out->model, buf, sizeof(out->model) - 1);
    out->has_model = 1;
    out->thinking[0] = '\0';
    out->has_thinking = 0;
}
