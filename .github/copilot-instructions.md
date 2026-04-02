# NeuronOS — Copilot Instructions

## Project Overview

NeuronOS is an AI agent runtime written in **pure C11** with **zero external dependencies**. It includes inference, ReAct agent loop, 3-tier memory, MCP server/client, and HTTP API in ~12,600 lines of C.

## Language & Build Rules

### C11 Strict
- Standard: **C11** (`-std=c11`)
- Compiler flags: `-Wall -Wextra -Werror -pedantic`
- **No C++ allowed**, no `extern "C"`, no C++ headers
- **No external dependencies** — everything is implemented from scratch
  - No curl, no jansson, no sqlite amalgamation downloads at build time
  - SQLite is the ONE exception — it's bundled as amalgamation in `vendor/sqlite3.c`
- Use `stdint.h` types: `uint32_t`, `int64_t`, etc. Never use bare `int` for sizes
- All strings are UTF-8 `char*`, no `wchar_t`

### Build System
```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Test
cd build && ctest --output-on-failure

# Debug build
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=ON
```

### File Organization
```
src/
  neuronos.c          # Main entry, CLI parsing
  agent.c/h           # ReAct agent loop
  memory.c/h          # 3-tier MemGPT (Core/Recall/Archival)
  tools.c/h           # Built-in tool registry
  inference.c/h       # Model loading, token generation
  hal.c/h             # Hardware Abstraction Layer
  hal_avx2.c          # AVX2 SIMD backend
  hal_avxvnni.c       # AVX-VNNI backend
  hal_neon.c          # ARM NEON backend
  hal_vulkan.c        # Vulkan compute backend
  hal_scalar.c        # Portable scalar fallback
  mcp_server.c/h      # MCP JSON-RPC over STDIO
  mcp_client.c/h      # MCP client (call external servers)
  http_server.c/h     # OpenAI-compatible HTTP API
  json.c/h            # JSON parser/serializer (custom)
  grammar.c/h         # GBNF grammar engine
  model_registry.c/h  # Model download + management
  web_ui.c/h          # Embedded web interface
vendor/
  sqlite3.c/h         # SQLite amalgamation (bundled)
tests/
  test_*.c            # One test file per module
```

## HAL Abstraction Pattern

All SIMD/compute code goes through the HAL. **Never use intrinsics directly in non-hal files.**

```c
// hal.h — interface
typedef struct {
    void (*mat_mul)(float* out, const float* a, const float* b, int m, int n, int k);
    void (*quantize_q4)(uint8_t* out, const float* in, int n);
    void (*dequantize_q4)(float* out, const uint8_t* in, int n);
    void (*softmax)(float* out, const float* in, int n);
    const char* name;
} hal_backend_t;

// Auto-select best backend at runtime
hal_backend_t* hal_detect(void);

// In any file that needs compute:
hal_backend_t* hal = hal_detect();
hal->mat_mul(out, a, b, m, n, k);
```

When adding a new HAL operation:
1. Add function pointer to `hal_backend_t`
2. Implement scalar version in `hal_scalar.c` (REQUIRED — this is the fallback)
3. Implement optimized versions in relevant backends
4. Update `hal_detect()` capability checks

## Memory Architecture (MemGPT 3-Tier)

```
Core Memory      — System prompt + persona, always in context
                   Stored in SQLite `core_memory` table
                   Modified by agent via `core_memory_replace` tool

Recall Memory    — Conversation history, auto-managed
                   Stored in SQLite `recall_memory` table
                   Agent searches via `conversation_search` tool
                   FTS5 full-text index for fast retrieval

Archival Memory  — Long-term knowledge, unlimited size
                   Stored in SQLite `archival_memory` table
                   Agent stores/retrieves via `archival_memory_insert` / `archival_memory_search`
                   FTS5 indexed
```

All three tiers persist across sessions. The agent decides what to remember. The context window is managed by injecting relevant recall/archival results alongside core memory.

## How to Add a New Built-in Tool

1. Define the tool in `tools.c`:
```c
static tool_result_t tool_my_new_tool(agent_t* agent, const json_value_t* args) {
    // Extract args using json_get_string(), json_get_number()
    const char* param = json_get_string(args, "param");
    if (!param) return tool_error("missing required parameter: param");

    // Do work...

    // Return result
    return tool_success(json_string("result value"));
}
```

2. Register in `tools_init()`:
```c
tool_register(&(tool_def_t){
    .name = "my_new_tool",
    .description = "One-line description for the agent",
    .parameters = "{"type":"object","properties":{"param":{"type":"string","description":"What this param is"}},"required":["param"]}",
    .execute = tool_my_new_tool
});
```

3. Add GBNF grammar rule in `grammar.c` so the agent can emit structured calls to the tool.

4. Write test in `tests/test_tools.c`:
```c
static void test_my_new_tool(void) {
    agent_t* agent = test_create_agent();
    json_value_t* args = json_parse("{\"param\":\"test_value\"}");
    tool_result_t result = tool_my_new_tool(agent, args);
    ASSERT(result.success);
    ASSERT_STR_CONTAINS(result.output, "expected");
    json_free(args);
    agent_free(agent);
}
```

5. Run: `cd build && ctest --output-on-failure`

## Test Patterns

- Every module has `tests/test_<module>.c`
- Use the internal test harness macros: `ASSERT()`, `ASSERT_EQ()`, `ASSERT_STR_EQ()`
- Tests must be deterministic — no network calls, no random seeds without fixed seed
- Memory leak detection: debug builds run with `-DSANITIZE=ON` (ASan + UBSan)
- All 27 tests must pass before any PR merge

```bash
# Run all tests
cd build && ctest --output-on-failure

# Run specific test
cd build && ctest -R test_memory --output-on-failure

# Run with sanitizers
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=ON
cmake --build build-debug && cd build-debug && ctest --output-on-failure
```

## Code Style

- 4-space indentation, no tabs
- Function naming: `module_action_noun()` → `agent_run_step()`, `memory_recall_search()`
- Structs: `module_name_t` → `agent_t`, `tool_result_t`
- Constants: `NEURONOS_MAX_CONTEXT_LEN`
- Always check return values. Every `malloc` has a NULL check. Every `fopen` has an error path.
- No global mutable state except the HAL singleton (detected once at startup)
- Comments: explain **why**, not **what**. The C code should be readable on its own.

## CI/CD

- GitHub Actions builds on every push: Linux x64, Linux ARM64, macOS x64, macOS ARM64, Windows x64
- All 27 tests run on all platforms
- Release tags trigger binary artifact uploads to GitHub Releases
- No Docker, no containers — single static binary per platform
