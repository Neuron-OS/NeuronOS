/* ============================================================
 * NeuronOS CLI v0.4.0 — Smart Agent Interface
 *
 * New in v0.4:
 *   - hwinfo: Show detected hardware capabilities
 *   - scan:   Scan directory for models and rank them
 *   - auto:   Auto-select best model for hardware
 *
 * Usage:
 *   neuronos-cli <model.gguf> generate "prompt text"
 *   neuronos-cli <model.gguf> agent "do something for me"
 *   neuronos-cli <model.gguf> info
 *   neuronos-cli hwinfo
 *   neuronos-cli scan [models-dir]
 *   neuronos-cli auto agent "do something"
 *   neuronos-cli auto generate "prompt"
 * ============================================================ */
#include "neuronos/neuronos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Default models directory (relative to binary) ---- */
#define DEFAULT_MODELS_DIR "../../models"

/* ---- Streaming callback: print tokens as they arrive ---- */
static bool stream_token(const char * text, void * user_data) {
    (void)user_data;
    printf("%s", text);
    fflush(stdout);
    return true;
}

/* ---- Agent step callback: show each step ---- */
static void agent_step(int step, const char * thought, const char * action, const char * observation,
                       void * user_data) {
    (void)user_data;
    fprintf(stderr, "\n── Step %d ──\n", step + 1);
    if (thought)
        fprintf(stderr, "  Thought: %s\n", thought);
    if (action)
        fprintf(stderr, "  Action:  %s\n", action);
    if (observation)
        fprintf(stderr, "  Observe: %.200s%s\n", observation, strlen(observation) > 200 ? "..." : "");
}

/* ---- Usage ---- */
static void print_usage(const char * prog) {
    fprintf(stderr,
            "NeuronOS CLI v%s — The fastest AI agent engine\n\n"
            "Usage:\n"
            "  %s <model.gguf> generate \"prompt\"    Generate text\n"
            "  %s <model.gguf> agent \"task\"          Run agent with tools\n"
            "  %s <model.gguf> info                  Show model info\n"
            "  %s hwinfo                             Detect hardware\n"
            "  %s scan [dir]                         Scan models directory\n"
            "  %s auto generate \"prompt\"             Auto-select model + generate\n"
            "  %s auto agent \"task\"                  Auto-select model + agent\n"
            "\nOptions:\n"
            "  -t <threads>     Number of threads (default: auto)\n"
            "  -n <tokens>      Max tokens to generate (default: 256)\n"
            "  -s <steps>       Max agent steps (default: 10)\n"
            "  --temp <float>   Temperature (default: 0.7)\n"
            "  --grammar <file> GBNF grammar file\n"
            "  --models <dir>   Models search directory\n"
            "  --verbose        Show debug info\n",
            NEURONOS_VERSION_STRING, prog, prog, prog, prog, prog, prog, prog);
}

/* ---- Run generate command ---- */
static int cmd_generate(neuronos_model_t * model, const char * prompt, int max_tokens, float temperature,
                        const char * grammar_file, bool verbose) {
    if (!prompt) {
        fprintf(stderr, "Error: No prompt provided\n");
        return 1;
    }

    /* Load grammar file if specified */
    char * grammar = NULL;
    if (grammar_file) {
        FILE * fp = fopen(grammar_file, "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            grammar = malloc((size_t)size + 1);
            if (grammar) {
                fread(grammar, 1, (size_t)size, fp);
                grammar[size] = '\0';
            }
            fclose(fp);
        }
    }

    neuronos_gen_params_t gparams = {
        .prompt = prompt,
        .max_tokens = max_tokens,
        .temperature = temperature,
        .top_p = 0.95f,
        .top_k = 40,
        .grammar = grammar,
        .on_token = stream_token,
        .user_data = NULL,
        .seed = 0,
    };

    neuronos_gen_result_t result = neuronos_generate(model, gparams);
    printf("\n");

    if (verbose) {
        fprintf(stderr, "\n[%d tokens, %.1f ms, %.2f t/s]\n", result.n_tokens, result.elapsed_ms, result.tokens_per_s);
    }

    free(grammar);
    neuronos_gen_result_free(&result);
    return (result.status == NEURONOS_OK) ? 0 : 1;
}

/* ---- Run agent command ---- */
static int cmd_agent(neuronos_model_t * model, const char * prompt, int max_tokens, int max_steps, float temperature,
                     bool verbose) {
    if (!prompt) {
        fprintf(stderr, "Error: No task provided\n");
        return 1;
    }

    /* Create tool registry with safe defaults */
    neuronos_tool_registry_t * tools = neuronos_tool_registry_create();
    neuronos_tool_register_defaults(tools, NEURONOS_CAP_FILESYSTEM);

    neuronos_agent_params_t aparams = {
        .max_steps = max_steps,
        .max_tokens_per_step = max_tokens,
        .temperature = temperature,
        .verbose = verbose,
    };

    neuronos_agent_t * agent = neuronos_agent_create(model, tools, aparams);
    if (!agent) {
        fprintf(stderr, "Error: Failed to create agent\n");
        neuronos_tool_registry_free(tools);
        return 1;
    }

    fprintf(stderr, "NeuronOS Agent v%s\n", neuronos_version());
    fprintf(stderr, "Task: %s\n", prompt);
    fprintf(stderr, "Tools: %d registered\n", neuronos_tool_count(tools));
    fprintf(stderr, "Running...\n");

    neuronos_agent_result_t result = neuronos_agent_run(agent, prompt, agent_step, NULL);

    if (result.status == NEURONOS_OK && result.text) {
        printf("\n══ Answer ══\n%s\n", result.text);
    } else {
        fprintf(stderr, "\nAgent stopped (status=%d, steps=%d)\n", result.status, result.steps_taken);
    }

    if (verbose) {
        fprintf(stderr, "[%d steps, %.1f ms]\n", result.steps_taken, result.total_ms);
    }

    neuronos_agent_result_free(&result);
    neuronos_agent_free(agent);
    neuronos_tool_registry_free(tools);
    return (result.status == NEURONOS_OK) ? 0 : 1;
}

/* ============================================================
 * MAIN
 * ============================================================ */
int main(int argc, char * argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* ---- Parse global options first ---- */
    int n_threads = 0;
    int max_tokens = 256;
    int max_steps = 10;
    float temperature = 0.7f;
    const char * grammar_file = NULL;
    const char * models_dir = DEFAULT_MODELS_DIR;
    bool verbose = false;

    /* Collect options from all positions */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            n_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            max_tokens = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            max_steps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--temp") == 0 && i + 1 < argc) {
            temperature = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--grammar") == 0 && i + 1 < argc) {
            grammar_file = argv[++i];
        } else if (strcmp(argv[i], "--models") == 0 && i + 1 < argc) {
            models_dir = argv[++i];
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        }
    }

    const char * command = argv[1];

    /* ════════════════════════════════════════════
     * HWINFO — Hardware detection (no model needed)
     * ════════════════════════════════════════════ */
    if (strcmp(command, "hwinfo") == 0) {
        neuronos_hw_info_t hw = neuronos_detect_hardware();
        neuronos_hw_print_info(&hw);
        return 0;
    }

    /* ════════════════════════════════════════════
     * SCAN — Scan models directory
     * ════════════════════════════════════════════ */
    if (strcmp(command, "scan") == 0) {
        const char * scan_dir = (argc > 2 && argv[2][0] != '-') ? argv[2] : models_dir;

        neuronos_hw_info_t hw = neuronos_detect_hardware();
        fprintf(stderr, "Scanning: %s\n", scan_dir);
        fprintf(stderr, "RAM budget: %lld MB\n\n", (long long)hw.model_budget_mb);

        int count = 0;
        neuronos_model_entry_t * models = neuronos_model_scan(scan_dir, &hw, &count);

        if (!models || count == 0) {
            fprintf(stderr, "No .gguf models found in %s\n", scan_dir);
            return 1;
        }

        printf("%-4s %-40s %8s %8s %10s %7s  %s\n", "Rank", "Name", "Size MB", "RAM MB", "Params", "Score", "Fits?");
        printf("──── ──────────────────────────────────────── ────────"
               " ──────── ────────── ───────  ─────\n");

        for (int i = 0; i < count; i++) {
            const neuronos_model_entry_t * m = &models[i];
            printf("%-4d %-40.40s %7lld %7lld %8lldM %7.1f  %s\n", i + 1, m->name, (long long)m->file_size_mb,
                   (long long)m->est_ram_mb, (long long)(m->n_params_est / 1000000), m->score,
                   m->fits_in_ram ? "YES" : "NO");
        }

        const neuronos_model_entry_t * best = neuronos_model_select_best(models, count);
        if (best) {
            printf("\n★ Best model: %s (score=%.1f)\n", best->name, best->score);
            printf("  Path: %s\n", best->path);
        }

        neuronos_model_scan_free(models, count);
        return 0;
    }

    /* ════════════════════════════════════════════
     * AUTO — Auto-select model then run command
     * ════════════════════════════════════════════ */
    if (strcmp(command, "auto") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s auto <generate|agent> \"prompt\"\n", argv[0]);
            return 1;
        }

        const char * sub_command = argv[2];
        const char * prompt = NULL;
        for (int i = 3; i < argc; i++) {
            if (argv[i][0] != '-') {
                prompt = argv[i];
                break;
            }
        }

        /* Detect hardware */
        neuronos_hw_info_t hw = neuronos_detect_hardware();
        if (verbose)
            neuronos_hw_print_info(&hw);

        /* Scan for models */
        int count = 0;
        neuronos_model_entry_t * models = neuronos_model_scan(models_dir, &hw, &count);

        if (!models || count == 0) {
            fprintf(stderr, "Error: No .gguf models found in %s\n", models_dir);
            fprintf(stderr, "Use --models <dir> to specify models directory\n");
            return 1;
        }

        const neuronos_model_entry_t * best = neuronos_model_select_best(models, count);
        if (!best) {
            fprintf(stderr, "Error: No model fits in available RAM (%lld MB)\n", (long long)hw.model_budget_mb);
            neuronos_model_scan_free(models, count);
            return 1;
        }

        fprintf(stderr, "★ Auto-selected: %s (%.1f score, %lld MB)\n", best->name, best->score,
                (long long)best->file_size_mb);

        /* Init engine with detected thread count */
        neuronos_engine_params_t eparams = {
            .n_threads = n_threads > 0 ? n_threads : hw.n_cores_physical,
            .n_gpu_layers = 0,
            .verbose = verbose,
        };
        neuronos_engine_t * engine = neuronos_init(eparams);
        if (!engine) {
            fprintf(stderr, "Error: Failed to initialize engine\n");
            neuronos_model_scan_free(models, count);
            return 1;
        }

        /* Load the auto-selected model */
        neuronos_model_params_t mparams = {
            .model_path = best->path,
            .context_size = 2048,
            .use_mmap = true,
        };
        neuronos_model_t * model = neuronos_model_load(engine, mparams);
        if (!model) {
            fprintf(stderr, "Error: Failed to load model %s\n", best->path);
            neuronos_shutdown(engine);
            neuronos_model_scan_free(models, count);
            return 1;
        }

        int rc = 1;
        if (strcmp(sub_command, "generate") == 0) {
            rc = cmd_generate(model, prompt, max_tokens, temperature, grammar_file, verbose);
        } else if (strcmp(sub_command, "agent") == 0) {
            rc = cmd_agent(model, prompt, max_tokens, max_steps, temperature, verbose);
        } else {
            fprintf(stderr, "Unknown auto sub-command: %s\n", sub_command);
            fprintf(stderr, "Use: auto generate | auto agent\n");
        }

        neuronos_model_free(model);
        neuronos_shutdown(engine);
        neuronos_model_scan_free(models, count);
        return rc;
    }

    /* ════════════════════════════════════════════
     * Legacy commands: <model.gguf> <command> [prompt]
     * ════════════════════════════════════════════ */
    const char * model_path = argv[1];
    command = argc > 2 ? argv[2] : "info";
    const char * prompt = NULL;

    /* Find prompt (first positional arg after command that isn't an option) */
    for (int i = 3; i < argc; i++) {
        if (argv[i][0] != '-') {
            prompt = argv[i];
            break;
        }
    }

    /* Init engine */
    neuronos_engine_params_t eparams = {
        .n_threads = n_threads,
        .n_gpu_layers = 0,
        .verbose = verbose,
    };
    neuronos_engine_t * engine = neuronos_init(eparams);
    if (!engine) {
        fprintf(stderr, "Error: Failed to initialize engine\n");
        return 1;
    }

    /* INFO command */
    if (strcmp(command, "info") == 0) {
        neuronos_model_params_t mparams = {
            .model_path = model_path,
            .context_size = 512,
            .use_mmap = true,
        };
        neuronos_model_t * model = neuronos_model_load(engine, mparams);
        if (!model) {
            fprintf(stderr, "Error: Failed to load model\n");
            neuronos_shutdown(engine);
            return 1;
        }

        neuronos_model_info_t info = neuronos_model_info(model);
        printf("NeuronOS v%s\n", neuronos_version());
        printf("Model: %s\n", info.description);
        printf("Parameters: %lldM\n", (long long)(info.n_params / 1000000));
        printf("Size: %.1f MB\n", (double)info.model_size / (1024.0 * 1024.0));
        printf("Vocabulary: %d\n", info.n_vocab);
        printf("Context (training): %d\n", info.n_ctx_train);
        printf("Embedding dim: %d\n", info.n_embd);

        /* Also show hardware info */
        printf("\n");
        neuronos_hw_info_t hw = neuronos_detect_hardware();
        neuronos_hw_print_info(&hw);

        neuronos_model_free(model);
        neuronos_shutdown(engine);
        return 0;
    }

    /* Load model for generate/agent */
    neuronos_model_params_t mparams = {
        .model_path = model_path,
        .context_size = 2048,
        .use_mmap = true,
    };
    neuronos_model_t * model = neuronos_model_load(engine, mparams);
    if (!model) {
        fprintf(stderr, "Error: Failed to load model\n");
        neuronos_shutdown(engine);
        return 1;
    }

    int rc = 1;
    if (strcmp(command, "generate") == 0) {
        rc = cmd_generate(model, prompt, max_tokens, temperature, grammar_file, verbose);
    } else if (strcmp(command, "agent") == 0) {
        rc = cmd_agent(model, prompt, max_tokens, max_steps, temperature, verbose);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
    }

    neuronos_model_free(model);
    neuronos_shutdown(engine);
    return rc;
}
