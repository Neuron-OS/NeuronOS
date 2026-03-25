/* ============================================================
 * NeuronOS — Structured Logging (implementation)
 *
 * Copyright (c) 2025 NeuronOS Project
 * SPDX-License-Identifier: MIT
 * ============================================================ */
#include "neuronos/neuronos_log.h"

#include <stdio.h>
#include <string.h>

/* ---- Global state ---- */
static neuronos_log_level_t g_log_level = NEURONOS_LOG_INFO;
static FILE * g_log_file = NULL;        /* NULL = stderr */
static neuronos_log_cb g_log_cb = NULL;
static void * g_log_cb_data = NULL;

static const char * level_names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
static const char * level_colors[] = {"\033[36m", "\033[32m", "\033[33m", "\033[31m"};
static const char * color_reset = "\033[0m";

void neuronos_log_set_level(neuronos_log_level_t level) {
    g_log_level = level;
}

neuronos_log_level_t neuronos_log_get_level(void) {
    return g_log_level;
}

void neuronos_log_set_file(FILE * fp) {
    g_log_file = fp;
}

void neuronos_log_set_callback(neuronos_log_cb cb, void * user_data) {
    g_log_cb = cb;
    g_log_cb_data = user_data;
}

void neuronos_log_write(neuronos_log_level_t level, const char * file,
                        int line, const char * fmt, ...) {
    if (level < g_log_level)
        return;

    /* Format the message */
    char msg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    /* If a callback is set, use it exclusively */
    if (g_log_cb) {
        g_log_cb(level, file, line, msg, g_log_cb_data);
        return;
    }

    /* Get timestamp */
    time_t now = time(NULL);
    struct tm * tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

    /* Extract just the filename from the path */
    const char * basename = strrchr(file, '/');
    if (!basename) basename = strrchr(file, '\\');
    basename = basename ? basename + 1 : file;

    /* Write to file or stderr */
    FILE * out = g_log_file ? g_log_file : stderr;
    int is_tty = (out == stderr); /* simplified: color only on stderr */

    if (is_tty) {
        fprintf(out, "%s%s %s[%s:%d]%s %s\n",
                level_colors[level], level_names[level],
                "\033[90m", basename, line, color_reset,
                msg);
    } else {
        fprintf(out, "%s %s [%s:%d] %s\n",
                timestamp, level_names[level], basename, line, msg);
    }
}
