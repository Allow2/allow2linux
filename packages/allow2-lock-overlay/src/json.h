/**
 * json.h -- Minimal JSON parser for allow2-lock-overlay
 *
 * Parses the specific JSON message formats used by the IPC protocol.
 * Not a general-purpose parser — handles objects, strings, numbers,
 * booleans, arrays of objects, and null. No nesting beyond one level
 * of array-of-objects.
 *
 * Usage:
 *   JsonValue val;
 *   if (json_parse(json_string, &val) == 0) {
 *       const char *screen = json_get_string(&val, "screen");
 *       int num = json_get_int(&val, "remaining", 0);
 *       const JsonValue *arr = json_get_array(&val, "children");
 *       json_free(&val);
 *   }
 */

#ifndef JSON_H
#define JSON_H

#define JSON_MAX_KEYS 32
#define JSON_MAX_STRING 512

typedef enum {
    JSON_NULL = 0,
    JSON_STRING,
    JSON_NUMBER,
    JSON_BOOL,
    JSON_OBJECT,
    JSON_ARRAY
} JsonType;

typedef struct JsonValue JsonValue;

struct JsonValue {
    JsonType type;

    /* For JSON_STRING */
    char str_val[JSON_MAX_STRING];

    /* For JSON_NUMBER / JSON_BOOL */
    double num_val;

    /* For JSON_OBJECT: key-value pairs */
    int key_count;
    char keys[JSON_MAX_KEYS][64];
    JsonValue *values;  /* Array of key_count values (heap-allocated) */

    /* For JSON_ARRAY: array of JsonValue items */
    int array_len;
    JsonValue *array_items;  /* Heap-allocated array */
};

/* Parse a JSON string into a JsonValue. Returns 0 on success, -1 on error.
 * Caller must call json_free() when done. */
int json_parse(const char *json, JsonValue *out);

/* Free heap-allocated memory inside a JsonValue. */
void json_free(JsonValue *val);

/* Get a string value for a key from an object. Returns NULL if not found. */
const char *json_get_string(const JsonValue *obj, const char *key);

/* Get an integer value for a key from an object. Returns def if not found. */
int json_get_int(const JsonValue *obj, const char *key, int def);

/* Get an array value for a key from an object. Returns NULL if not found. */
const JsonValue *json_get_array(const JsonValue *obj, const char *key);

#endif /* JSON_H */
