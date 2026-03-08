/**
 * json.c -- Minimal JSON parser for allow2-lock-overlay
 *
 * Handles the specific message formats in the IPC protocol:
 * objects with string/number/bool/null values and one level of
 * array-of-objects.
 */

#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- Internal parser state ---- */

typedef struct {
    const char *src;
    int pos;
    int len;
} Parser;

static void skip_ws(Parser *p) {
    while (p->pos < p->len && isspace((unsigned char)p->src[p->pos])) {
        p->pos++;
    }
}

static int peek(Parser *p) {
    skip_ws(p);
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->src[p->pos];
}

static int advance(Parser *p) {
    if (p->pos >= p->len) return -1;
    return (unsigned char)p->src[p->pos++];
}

/* Forward declaration */
static int parse_value(Parser *p, JsonValue *out);

/* Parse a JSON string (expects opening " already peeked). */
static int parse_string(Parser *p, char *buf, int buflen) {
    skip_ws(p);
    if (advance(p) != '"') return -1;

    int i = 0;
    while (p->pos < p->len) {
        char c = p->src[p->pos++];
        if (c == '"') {
            buf[i] = '\0';
            return 0;
        }
        if (c == '\\' && p->pos < p->len) {
            char esc = p->src[p->pos++];
            switch (esc) {
            case '"':  c = '"'; break;
            case '\\': c = '\\'; break;
            case '/':  c = '/'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            case 'u':
                /* Skip unicode escape (4 hex digits) — store as ? */
                if (p->pos + 4 <= p->len) p->pos += 4;
                c = '?';
                break;
            default:
                c = esc;
                break;
            }
        }
        if (i < buflen - 1) {
            buf[i++] = c;
        }
    }

    /* Unterminated string */
    buf[i] = '\0';
    return -1;
}

/* Parse a number (integer or float). */
static int parse_number(Parser *p, double *out) {
    skip_ws(p);
    char numbuf[64];
    int i = 0;

    while (p->pos < p->len && i < 63) {
        char c = p->src[p->pos];
        if (c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' ||
            (c >= '0' && c <= '9')) {
            numbuf[i++] = c;
            p->pos++;
        } else {
            break;
        }
    }
    numbuf[i] = '\0';

    if (i == 0) return -1;

    char *end = NULL;
    *out = strtod(numbuf, &end);
    if (end == numbuf) return -1;
    return 0;
}

/* Parse a JSON array. */
static int parse_array(Parser *p, JsonValue *out) {
    skip_ws(p);
    if (advance(p) != '[') return -1;

    out->type = JSON_ARRAY;
    out->array_len = 0;
    out->array_items = NULL;

    /* Check for empty array */
    if (peek(p) == ']') {
        advance(p);
        return 0;
    }

    /* Parse items (pre-allocate a reasonable amount) */
    int capacity = 16;
    out->array_items = (JsonValue *)calloc(capacity, sizeof(JsonValue));
    if (!out->array_items) return -1;

    while (1) {
        if (out->array_len >= capacity) {
            capacity *= 2;
            JsonValue *tmp = (JsonValue *)realloc(out->array_items,
                                                   capacity * sizeof(JsonValue));
            if (!tmp) return -1;
            out->array_items = tmp;
        }

        if (parse_value(p, &out->array_items[out->array_len]) != 0) {
            return -1;
        }
        out->array_len++;

        skip_ws(p);
        int c = peek(p);
        if (c == ',') {
            advance(p);
            continue;
        } else if (c == ']') {
            advance(p);
            return 0;
        } else {
            return -1;
        }
    }
}

/* Parse a JSON object. */
static int parse_object(Parser *p, JsonValue *out) {
    skip_ws(p);
    if (advance(p) != '{') return -1;

    out->type = JSON_OBJECT;
    out->key_count = 0;
    out->values = NULL;

    /* Check for empty object */
    if (peek(p) == '}') {
        advance(p);
        return 0;
    }

    out->values = (JsonValue *)calloc(JSON_MAX_KEYS, sizeof(JsonValue));
    if (!out->values) return -1;

    while (1) {
        if (out->key_count >= JSON_MAX_KEYS) {
            /* Too many keys — skip remaining */
            break;
        }

        /* Parse key */
        if (peek(p) != '"') return -1;
        if (parse_string(p, out->keys[out->key_count], 64) != 0) return -1;

        /* Expect colon */
        skip_ws(p);
        if (advance(p) != ':') return -1;

        /* Parse value */
        if (parse_value(p, &out->values[out->key_count]) != 0) return -1;
        out->key_count++;

        skip_ws(p);
        int c = peek(p);
        if (c == ',') {
            advance(p);
            continue;
        } else if (c == '}') {
            advance(p);
            return 0;
        } else {
            return -1;
        }
    }

    return 0;
}

/* Parse any JSON value. */
static int parse_value(Parser *p, JsonValue *out) {
    memset(out, 0, sizeof(*out));
    skip_ws(p);

    int c = peek(p);
    if (c < 0) return -1;

    if (c == '"') {
        out->type = JSON_STRING;
        return parse_string(p, out->str_val, JSON_MAX_STRING);
    }
    if (c == '{') {
        return parse_object(p, out);
    }
    if (c == '[') {
        return parse_array(p, out);
    }
    if (c == 't') {
        /* true */
        if (p->pos + 4 <= p->len &&
            strncmp(p->src + p->pos, "true", 4) == 0) {
            p->pos += 4;
            out->type = JSON_BOOL;
            out->num_val = 1;
            return 0;
        }
        return -1;
    }
    if (c == 'f') {
        /* false */
        if (p->pos + 5 <= p->len &&
            strncmp(p->src + p->pos, "false", 5) == 0) {
            p->pos += 5;
            out->type = JSON_BOOL;
            out->num_val = 0;
            return 0;
        }
        return -1;
    }
    if (c == 'n') {
        /* null */
        if (p->pos + 4 <= p->len &&
            strncmp(p->src + p->pos, "null", 4) == 0) {
            p->pos += 4;
            out->type = JSON_NULL;
            return 0;
        }
        return -1;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        out->type = JSON_NUMBER;
        return parse_number(p, &out->num_val);
    }

    return -1;
}

/* ---- Public API ---- */

int json_parse(const char *json, JsonValue *out) {
    if (!json || !out) return -1;

    Parser p;
    p.src = json;
    p.pos = 0;
    p.len = strlen(json);

    return parse_value(&p, out);
}

void json_free(JsonValue *val) {
    if (!val) return;

    int i;

    if (val->type == JSON_OBJECT && val->values) {
        for (i = 0; i < val->key_count; i++) {
            json_free(&val->values[i]);
        }
        free(val->values);
        val->values = NULL;
    }

    if (val->type == JSON_ARRAY && val->array_items) {
        for (i = 0; i < val->array_len; i++) {
            json_free(&val->array_items[i]);
        }
        free(val->array_items);
        val->array_items = NULL;
    }
}

const char *json_get_string(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;

    int i;
    for (i = 0; i < obj->key_count; i++) {
        if (strcmp(obj->keys[i], key) == 0) {
            if (obj->values[i].type == JSON_STRING) {
                return obj->values[i].str_val;
            }
            return NULL;
        }
    }
    return NULL;
}

int json_get_int(const JsonValue *obj, const char *key, int def) {
    if (!obj || obj->type != JSON_OBJECT || !key) return def;

    int i;
    for (i = 0; i < obj->key_count; i++) {
        if (strcmp(obj->keys[i], key) == 0) {
            if (obj->values[i].type == JSON_NUMBER) {
                return (int)obj->values[i].num_val;
            }
            if (obj->values[i].type == JSON_BOOL) {
                return (int)obj->values[i].num_val;
            }
            return def;
        }
    }
    return def;
}

const JsonValue *json_get_array(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;

    int i;
    for (i = 0; i < obj->key_count; i++) {
        if (strcmp(obj->keys[i], key) == 0) {
            if (obj->values[i].type == JSON_ARRAY) {
                return &obj->values[i];
            }
            return NULL;
        }
    }
    return NULL;
}
