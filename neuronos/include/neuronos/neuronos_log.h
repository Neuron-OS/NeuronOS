/* ============================================================
 * NeuronOS — Structured Logging
 *
 * Minimal logging system with levels, timestamps, and
 * configurable output (stderr, file, or callback).
 *
 * Usage:
 *   LOG_INFO("Server started on port %d", port);
 *   LOG_ERROR("Failed to load model: %s", path);
 *
 * Copyright (c) 2025 NeuronOS Project
 * SPDX-License-Identifier: MIT
 * ============================================================ */
#ifndef NEURONOS_LOG_H
#define NEURONOS_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEURONOS_LOG_DEBUG = 0,
    NEURONOS_LOG_INFO  = 1,
    NEURONOS_LOG_WARN  = 2,
    NEURONOS_LOG_ERROR = 3,
    NEURONOS_LOG_NONE  = 4,  /* disable all logging */
} neuronos_log_level_t;

/* Callback for custom log routing (e.g., to web UI, file, etc.) */
typedef void (*neuronos_log_cb)(neuronos_log_level_t level, const char * file,
                                int line, const char * msg, void * user_data);

/* Set the minimum log level (default: NEURONOS_LOG_INFO) */
void neuronos_log_set_level(neuronos_log_level_t level);

/* Get current log level */
neuronos_log_level_t neuronos_log_get_level(void);

/* Set a log output file (NULL = stderr, which is the default) */
void neuronos_log_set_file(FILE * fp);

/* Set a custom log callback. When set, overrides file/stderr output. */
void neuronos_log_set_callback(neuronos_log_cb cb, void * user_data);

/* Core log function (use the macros below instead) */
void neuronos_log_write(neuronos_log_level_t level, const char * file,
                        int line, const char * fmt, ...);

/* ---- Convenience macros ---- */
#define LOG_DEBUG(...) neuronos_log_write(NEURONOS_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  neuronos_log_write(NEURONOS_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  neuronos_log_write(NEURONOS_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) neuronos_log_write(NEURONOS_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* NEURONOS_LOG_H */
