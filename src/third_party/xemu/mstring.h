// Portable, fully implemented string storage for the imported pure generator.
// This file is an xboxlle adapter, not a QEMU/GLib device shim.
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

typedef struct MString {
    char *data;
    size_t length, capacity;
    unsigned refs;
} MString;

static inline void mstring_reserve(MString *s, size_t size) {
    if (size <= s->capacity) return;
    size_t capacity = size < 256 ? 256 : size * 2;
    char *data = (char *)realloc(s->data, capacity);
    if (!data) abort();
    s->data = data;
    s->capacity = capacity;
}
static inline MString *mstring_new(void) {
    MString *s = (MString *)calloc(1, sizeof(*s));
    if (!s) abort();
    s->refs = 1;
    mstring_reserve(s, 1);
    s->data[0] = 0;
    return s;
}
static inline void mstring_ref(MString *s) { ++s->refs; }
static inline void mstring_unref(MString *s) {
    if (s && --s->refs == 0) { free(s->data); free(s); }
}
static inline void mstring_append(MString *s, const char *value) {
    size_t size = strlen(value);
    mstring_reserve(s, s->length + size + 1);
    memcpy(s->data + s->length, value, size + 1);
    s->length += size;
}
static inline void mstring_append_vfmt(MString *s, const char *fmt, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int size = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (size < 0) abort();
    mstring_reserve(s, s->length + (size_t)size + 1);
    int written = vsnprintf(s->data + s->length, (size_t)size + 1, fmt, args);
    if (written != size) abort();
    s->length += (size_t)size;
}
static inline MString *mstring_from_str(const char *value) {
    MString *s = mstring_new(); mstring_append(s, value); return s;
}
static inline MString *mstring_from_fmt(const char *fmt, ...) {
    MString *s = mstring_new();
    va_list args; va_start(args, fmt); mstring_append_vfmt(s, fmt, args); va_end(args);
    return s;
}
static inline void mstring_append_fmt(MString *s, const char *fmt, ...) {
    va_list args; va_start(args, fmt); mstring_append_vfmt(s, fmt, args); va_end(args);
}
static inline const char *mstring_get_str(const MString *s) { return s->data; }
static inline size_t mstring_get_length(const MString *s) { return s->length; }
