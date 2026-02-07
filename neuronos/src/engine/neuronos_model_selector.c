/* ============================================================
 * NeuronOS — Hardware Detection & Model Auto-Selection
 *
 * Phase 2D: Detect hardware → scan models → score → select best.
 *
 * Inspired by:
 *   - Claude Code: "do the simple thing first"
 *   - OpenClaw: minimal architecture (triggering + state + glue)
 *   - ArXiv 2601.01743: budgeted, tool-augmented systems
 *
 * Algorithm:
 *   score = fits_in_ram * 1000
 *         + quality_tier(params) * 100
 *         + speed_estimate * 10
 *         + format_bonus * 5
 * ============================================================ */
#include "neuronos/neuronos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>

#ifdef __linux__
    #include <sys/sysinfo.h>
    #include <unistd.h>
#elif defined(__APPLE__)
    #include <sys/sysctl.h>
    #include <unistd.h>
#elif defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
#endif

/* ============================================================
 * HARDWARE DETECTION
 * ============================================================ */

/* Read a line from a file, return 0 on success */
static int read_proc_line(const char * path, const char * key, char * buf, size_t buflen) {
    FILE * fp = fopen(path, "r");
    if (!fp)
        return -1;

    char line[512];
    while (fgets(line, (int)sizeof(line), fp)) {
        if (strstr(line, key)) {
            /* Extract value after ':' */
            const char * val = strchr(line, ':');
            if (val) {
                val++;
                while (*val == ' ' || *val == '\t')
                    val++;
                size_t len = strlen(val);
                while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
                    len--;
                if (len >= buflen)
                    len = buflen - 1;
                memcpy(buf, val, len);
                buf[len] = '\0';
                fclose(fp);
                return 0;
            }
        }
    }
    fclose(fp);
    return -1;
}

static int64_t read_meminfo_kb(const char * key) {
    char val[64] = {0};
    if (read_proc_line("/proc/meminfo", key, val, sizeof(val)) == 0) {
        /* Value is in kB, e.g. "16384000 kB" */
        return atoll(val);
    }
    return 0;
}

neuronos_hw_info_t neuronos_detect_hardware(void) {
    neuronos_hw_info_t hw = {0};

    /* ---- CPU name ---- */
#ifdef __linux__
    if (read_proc_line("/proc/cpuinfo", "model name", hw.cpu_name, sizeof(hw.cpu_name)) != 0) {
        /* ARM or other */
        if (read_proc_line("/proc/cpuinfo", "Hardware", hw.cpu_name, sizeof(hw.cpu_name)) != 0) {
            snprintf(hw.cpu_name, sizeof(hw.cpu_name), "Unknown CPU");
        }
    }
#elif defined(__APPLE__)
    size_t len = sizeof(hw.cpu_name);
    sysctlbyname("machdep.cpu.brand_string", hw.cpu_name, &len, NULL, 0);
#else
    snprintf(hw.cpu_name, sizeof(hw.cpu_name), "Unknown CPU");
#endif

    /* ---- Architecture ---- */
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    snprintf(hw.arch, sizeof(hw.arch), "x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
    snprintf(hw.arch, sizeof(hw.arch), "aarch64");
#elif defined(__riscv) && (__riscv_xlen == 64)
    snprintf(hw.arch, sizeof(hw.arch), "riscv64");
#elif defined(__EMSCRIPTEN__)
    snprintf(hw.arch, sizeof(hw.arch), "wasm");
#elif defined(__arm__)
    snprintf(hw.arch, sizeof(hw.arch), "arm32");
#else
    snprintf(hw.arch, sizeof(hw.arch), "unknown");
#endif

    /* ---- Cores ---- */
#ifdef _SC_NPROCESSORS_ONLN
    hw.n_cores_logical = (int)sysconf(_SC_NPROCESSORS_ONLN);
#else
    hw.n_cores_logical = 4;
#endif
    /* Heuristic: assume ~60% are physical on hybrid CPUs */
    hw.n_cores_physical = hw.n_cores_logical > 8 ? (int)(hw.n_cores_logical * 0.6) : hw.n_cores_logical;

    /* ---- RAM ---- */
#ifdef __linux__
    hw.ram_total_mb = read_meminfo_kb("MemTotal") / 1024;
    hw.ram_available_mb = read_meminfo_kb("MemAvailable") / 1024;
    if (hw.ram_available_mb <= 0) {
        /* Fallback: free + buffers + cached */
        hw.ram_available_mb =
            (read_meminfo_kb("MemFree") + read_meminfo_kb("Buffers") + read_meminfo_kb("Cached")) / 1024;
    }
#elif defined(__APPLE__)
    {
        int64_t memsize = 0;
        size_t len = sizeof(memsize);
        sysctlbyname("hw.memsize", &memsize, &len, NULL, 0);
        hw.ram_total_mb = memsize / (1024 * 1024);
        /* macOS: estimate available as 60% of total */
        hw.ram_available_mb = hw.ram_total_mb * 60 / 100;
    }
#elif defined(_WIN32)
    {
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        hw.ram_total_mb = (int64_t)(ms.ullTotalPhys / (1024 * 1024));
        hw.ram_available_mb = (int64_t)(ms.ullAvailPhys / (1024 * 1024));
    }
#else
    /* POSIX fallback */
    {
        long pages = sysconf(_SC_PHYS_PAGES);
        long page_size = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && page_size > 0) {
            hw.ram_total_mb = (int64_t)(pages * page_size / (1024 * 1024));
        } else {
            hw.ram_total_mb = 2048; /* assume 2GB */
        }
        hw.ram_available_mb = hw.ram_total_mb * 50 / 100;
    }
#endif

    /* ---- Model budget: available RAM - 500MB safety margin ---- */
    hw.model_budget_mb = hw.ram_available_mb - 500;
    if (hw.model_budget_mb < 256) {
        hw.model_budget_mb = 256; /* minimum */
    }

    /* ---- CPU features from HAL ---- */
    /* We'll detect inline instead of depending on HAL init */
#if defined(__x86_64__) || defined(_M_X64)
    hw.features = 0;
    /* Use cpuid if available */
    #ifdef __GNUC__
    {
        unsigned int eax, ebx, ecx, edx;
        __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        if (ecx & (1 << 0))
            hw.features |= (1 << 0); /* SSE3 */
        if (ecx & (1 << 9))
            hw.features |= (1 << 1); /* SSSE3 */
        if (ecx & (1 << 28))
            hw.features |= (1 << 2); /* AVX */

        __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
        if (ebx & (1 << 5))
            hw.features |= (1 << 3); /* AVX2 */
        if (ebx & (1 << 16))
            hw.features |= (1 << 5); /* AVX512F */
    }
    #endif
#elif defined(__aarch64__)
    hw.features = (1 << 8); /* NEON is always available on aarch64 */
#endif

    /* ---- GPU (stub for now) ---- */
    hw.gpu_vram_mb = 0;
    hw.gpu_name[0] = '\0';

    return hw;
}

void neuronos_hw_print_info(const neuronos_hw_info_t * hw) {
    if (!hw)
        return;
    fprintf(stderr, "╔══════════════════════════════════════════╗\n");
    fprintf(stderr, "║  NeuronOS Hardware Detection v%s     ║\n", NEURONOS_VERSION_STRING);
    fprintf(stderr, "╠══════════════════════════════════════════╣\n");
    fprintf(stderr, "║  CPU:    %-32s║\n", hw->cpu_name);
    fprintf(stderr, "║  Arch:   %-32s║\n", hw->arch);
    fprintf(stderr, "║  Cores:  %d physical / %d logical        ║\n", hw->n_cores_physical, hw->n_cores_logical);
    fprintf(stderr, "║  RAM:    %lld MB total / %lld MB available ║\n", (long long)hw->ram_total_mb,
            (long long)hw->ram_available_mb);
    fprintf(stderr, "║  Budget: %lld MB for models               ║\n", (long long)hw->model_budget_mb);
    if (hw->gpu_vram_mb > 0) {
        fprintf(stderr, "║  GPU:    %s (%lld MB) ║\n", hw->gpu_name, (long long)hw->gpu_vram_mb);
    } else {
        fprintf(stderr, "║  GPU:    None detected (CPU-only)        ║\n");
    }
    fprintf(stderr, "║  Features: 0x%08X                     ║\n", hw->features);
    fprintf(stderr, "╚══════════════════════════════════════════╝\n");
}

/* ============================================================
 * MODEL SCANNER
 * ============================================================ */

/* Maximum models we'll scan */
#define MAX_SCAN_MODELS 128

/* Get file size in MB */
static int64_t file_size_mb(const char * path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return (int64_t)(st.st_size / (1024 * 1024));
    }
    return 0;
}

/* Extract model name from file path (just the filename without .gguf) */
static void extract_model_name(const char * path, char * name, size_t name_len) {
    const char * base = strrchr(path, '/');
    if (!base)
        base = strrchr(path, '\\');
    base = base ? base + 1 : path;

    size_t len = strlen(base);
    /* Remove .gguf extension */
    if (len > 5 && strcmp(base + len - 5, ".gguf") == 0) {
        len -= 5;
    }
    if (len >= name_len)
        len = name_len - 1;
    memcpy(name, base, len);
    name[len] = '\0';
}

/* Estimate RAM needed: file size + ~30% overhead for context/KV cache */
static int64_t estimate_ram_needed(int64_t file_size_mb_val) {
    return file_size_mb_val + (file_size_mb_val * 30 / 100) + 100; /* +100MB for context */
}

/* Estimate parameters from file size (heuristic for I2_S ternary models) */
static int64_t estimate_params(int64_t file_size_mb_val) {
    /* I2_S: ~2 bits per weight = ~0.25 bytes per param (with metadata overhead ~0.35) */
    return (int64_t)(file_size_mb_val * 1024LL * 1024LL / 35 * 100);
}

/* Score a model based on hardware fit */
static float score_model(const neuronos_model_entry_t * entry, const neuronos_hw_info_t * hw) {
    float score = 0.0f;

    /* Hard constraint: must fit in RAM */
    if (entry->est_ram_mb > hw->model_budget_mb) {
        return -1.0f; /* doesn't fit */
    }

    /* Fits in RAM: huge bonus */
    score += 1000.0f;

    /* Quality tier: prefer larger models (more params = smarter) */
    /* Scale: 0-500M=10, 500M-2B=30, 2B-4B=60, 4B-8B=80, 8B+=100 */
    int64_t params_b = entry->n_params_est / 1000000000LL;
    if (params_b >= 8)
        score += 100.0f;
    else if (params_b >= 4)
        score += 80.0f;
    else if (params_b >= 2)
        score += 60.0f;
    else if (params_b >= 1)
        score += 30.0f;
    else
        score += 10.0f;

    /* Speed estimate: smaller models are faster */
    /* Inverse relationship: budget_headroom → more speed */
    float headroom = (float)(hw->model_budget_mb - entry->est_ram_mb) / (float)hw->model_budget_mb;
    score += headroom * 50.0f;

    /* Format bonus: I2_S ternary models get bonus */
    if (strstr(entry->name, "i2_s") || strstr(entry->name, "I2_S") || strstr(entry->name, "1.58") ||
        strstr(entry->name, "bitnet") || strstr(entry->name, "BitNet")) {
        score += 25.0f;
    }

    /* Instruct model bonus (better for agents) */
    if (strstr(entry->name, "nstruct") || strstr(entry->name, "chat") || strstr(entry->name, "Chat")) {
        score += 15.0f;
    }

    return score;
}

/* Recursive directory walker for .gguf files */
static int scan_dir_recursive(const char * dir_path, const neuronos_hw_info_t * hw, neuronos_model_entry_t * entries,
                              int max_entries, int current_count) {
    DIR * dir = opendir(dir_path);
    if (!dir)
        return current_count;

    struct dirent * ent;
    while ((ent = readdir(dir)) != NULL && current_count < max_entries) {
        /* Skip . and .. */
        if (ent->d_name[0] == '.')
            continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            /* Recurse into subdirectory */
            current_count = scan_dir_recursive(full_path, hw, entries, max_entries, current_count);
        } else if (S_ISREG(st.st_mode)) {
            /* Check for .gguf extension */
            size_t name_len = strlen(ent->d_name);
            if (name_len > 5 && strcmp(ent->d_name + name_len - 5, ".gguf") == 0) {
                neuronos_model_entry_t * e = &entries[current_count];

                snprintf(e->path, sizeof(e->path), "%s", full_path);
                extract_model_name(full_path, e->name, sizeof(e->name));
                e->file_size_mb = file_size_mb(full_path);
                e->est_ram_mb = estimate_ram_needed(e->file_size_mb);
                e->n_params_est = estimate_params(e->file_size_mb);
                e->fits_in_ram = (e->est_ram_mb <= hw->model_budget_mb);
                e->score = score_model(e, hw);

                current_count++;
            }
        }
    }

    closedir(dir);
    return current_count;
}

/* Comparison function for qsort: descending by score */
static int compare_models_desc(const void * a, const void * b) {
    const neuronos_model_entry_t * ma = (const neuronos_model_entry_t *)a;
    const neuronos_model_entry_t * mb = (const neuronos_model_entry_t *)b;
    if (mb->score > ma->score)
        return 1;
    if (mb->score < ma->score)
        return -1;
    return 0;
}

neuronos_model_entry_t * neuronos_model_scan(const char * dir_path, const neuronos_hw_info_t * hw, int * out_count) {
    if (!dir_path || !hw || !out_count)
        return NULL;

    /* Allocate temporary buffer */
    neuronos_model_entry_t * entries = calloc(MAX_SCAN_MODELS, sizeof(neuronos_model_entry_t));
    if (!entries)
        return NULL;

    int count = scan_dir_recursive(dir_path, hw, entries, MAX_SCAN_MODELS, 0);

    if (count == 0) {
        free(entries);
        *out_count = 0;
        return NULL;
    }

    /* Sort by score descending (best first) */
    qsort(entries, (size_t)count, sizeof(neuronos_model_entry_t), compare_models_desc);

    *out_count = count;
    return entries;
}

void neuronos_model_scan_free(neuronos_model_entry_t * entries, int count) {
    (void)count;
    free(entries);
}

const neuronos_model_entry_t * neuronos_model_select_best(const neuronos_model_entry_t * entries, int count) {
    if (!entries || count == 0)
        return NULL;

    /* Already sorted by score desc, return first that fits */
    for (int i = 0; i < count; i++) {
        if (entries[i].fits_in_ram && entries[i].score > 0.0f) {
            return &entries[i];
        }
    }

    return NULL;
}

/* ============================================================
 * CONTEXT TRACKING (for compaction)
 *
 * Simple token counting based on conversation history length.
 * Real implementation would track actual llama.cpp token counts.
 * ============================================================ */

int neuronos_context_token_count(const neuronos_agent_t * agent) {
    /* Agent struct is opaque; we'll estimate from the model context */
    (void)agent;
    /* TODO: track actual token count in agent struct */
    return 0;
}

int neuronos_context_capacity(const neuronos_agent_t * agent) {
    (void)agent;
    /* TODO: return model->context_size */
    return 2048;
}

float neuronos_context_usage_ratio(const neuronos_agent_t * agent) {
    int used = neuronos_context_token_count(agent);
    int cap = neuronos_context_capacity(agent);
    if (cap <= 0)
        return 0.0f;
    return (float)used / (float)cap;
}
