# NeuronOS — Research Synthesis & Design Decisions
## Findings from Claude Code, OpenClaw, and Agent Science

**Date:** 2025-07
**Version:** v0.4.0 Planning
**Status:** Applied to implementation

---

## 1. Key Patterns Extracted

### From Claude Code (Anthropic)
| Pattern | Description | NeuronOS Adoption |
|---------|-------------|-------------------|
| **nO Master Loop** | Single-threaded while(tool_call) → execute → feed results | ✅ Already have ReAct loop |
| **Compressor wU2** | Auto-summarize at ~92% context window | ✅ **Implementing** as `neuronos_context_compact()` |
| **CLAUDE.md** | Markdown project memory, loaded at session start | ✅ **Adopting** `.neuronos/memory.md` |
| **TodoWrite** | Structured planning via JSON task list | 🔮 Future: agent self-planning |
| **dispatch_agent** | Sub-agents with depth limits (max 1 level) | 🔮 Future: sub-agent support |
| **Flat History** | No complex threading, simple message list | ✅ Already using flat arrays |
| **Regex > Embeddings** | GrepTool uses regex search, not vector DB | ✅ Tool approach matches |
| **Permission System** | Tools require allow/deny for risky ops | ✅ Already have CAP flags |
| **Diff-first Workflow** | Edit files via diffs, not full rewrites | 🔮 Future: diff tool |

### From OpenClaw (Moltbot)
| Pattern | Description | NeuronOS Adoption |
|---------|-------------|-------------------|
| **Externalized Memory** | Markdown-based persistent notes, context=cache disk=truth | ✅ **Implementing** memory layer |
| **/compact Command** | Manual + auto summarization trigger | ✅ **Implementing** in CLI |
| **Write Before Compact** | Always persist important info before compacting | ✅ Design rule |
| **Session Isolation** | Each conversation gets own namespace | 🔮 Future: multi-session |
| **Autonomous Invocation** | Cron/event triggers for always-on agents | 🔮 Future: daemon mode |
| **Minimal Architecture** | Triggering + Persistent State + Session Glue | ✅ Design principle |

### From Agent Science (ArXiv 2601.01743)
| Pattern | Description | NeuronOS Adoption |
|---------|-------------|-------------------|
| **Structured Action Spaces** | Typed tool schemas + structured outputs | ✅ Already have GBNF + JSON schemas |
| **Budgeted Systems** | Allocate test-time compute, track token usage | ✅ **Implementing** budget tracking |
| **Trace-centric** | Log full trajectories for auditability | ✅ **Implementing** step logging |
| **Reflection Pattern** | Self-evaluate after each step | 🔮 Future: reflection step |
| **Four Pillars** | Reflection, Tool Use, Planning, Collaboration | 🔮 Partial (Tool Use ✅, Planning 🔄) |

### From Context Compaction Research
| Pattern | Description | NeuronOS Adoption |
|---------|-------------|-------------------|
| **Token Threshold** | Trigger at ~80-92% context window usage | ✅ Default: 85% of n_ctx |
| **Retention Window** | Keep last N messages verbatim | ✅ Keep last 6 exchanges |
| **Hierarchical Summary** | Old → summary → older → meta-summary | ✅ Single-level for now |
| **Virtual Files** | Large outputs become file references | 🔮 Future: virtual file system |
| **Atomic Pairs** | Never split tool-call + result pairs | ✅ Design rule |

---

## 2. Auto-Model Selection Design

### Problem
User has hardware X and models directory with N models. Which model runs best?

### Algorithm: Hardware-Aware Model Scoring

```
Score(model, hardware) =
    fits_in_memory(model, hw.ram)     × 1000     (hard constraint)
  + quality_tier(model.params)         × 100      (bigger = smarter)
  + speed_estimate(model, hw)          × 10       (tokens/sec estimate)
  + format_bonus(model.quant_type)     × 5        (I2_S > Q4 for BitNet)
```

### Hardware Detection (extend HAL)
1. **RAM:** `/proc/meminfo` (Linux), `sysconf(_SC_PHYS_PAGES)` (POSIX)
2. **CPU cores:** `sysconf(_SC_NPROCESSORS_ONLN)`
3. **CPU arch:** Already in HAL (`neuronos_hal_get_features()`)
4. **GPU VRAM:** Future (vulkan query / nvidia-smi)

### GGUF Model Scanner
1. Walk models directory recursively
2. For each `.gguf` file, read GGUF metadata header:
   - `general.architecture` (llama, falcon, etc.)
   - `general.name`
   - File size → estimate memory footprint
   - Quant type from filename or metadata
3. Score each model against hardware
4. Return sorted list, pick top

### Implementation
- `neuronos_hw_info_t neuronos_detect_hardware(void)` — RAM, cores, arch, features
- `neuronos_model_scan()` — scan directory for .gguf files
- `neuronos_model_select()` — score and select best

---

## 3. Cross-Platform Validation Strategy

### Tier 1: Native CI (GitHub Actions)
| Platform | Runner | Compiler | Status |
|----------|--------|----------|--------|
| x86_64 Linux | `ubuntu-latest` | clang-18 | 🟢 Primary |
| ARM64 Linux | `ubuntu-latest` (arm64) | gcc-13 | 🟢 Native runner |
| macOS ARM64 | `macos-latest` | Apple Clang | 🟡 When ready |

### Tier 2: QEMU Emulation
| Platform | Action | Notes |
|----------|--------|-------|
| RISC-V 64 | `uraimo/run-on-arch-action@v3` | ~10x slower, but validates compilation |
| ARM32 | `uraimo/run-on-arch-action@v3` | Raspberry Pi compat |

### Tier 3: Special Targets
| Platform | Method | Notes |
|----------|--------|-------|
| WASM | Emscripten + Node.js | Compile-only initially |
| Windows | `windows-latest` + MSVC | Future |

### Test Matrix
1. **Compile-only tests** (all platforms): `cmake .. && cmake --build . -j4`
2. **Unit tests without model** (all): HAL probe, tool registry, JSON parsing
3. **Unit tests with model** (x86_64 only): inference, grammar, agent loop

---

## 4. Changes for v0.4.0

### New APIs
```c
// Hardware detection
neuronos_hw_info_t neuronos_detect_hardware(void);

// Model scanning & selection
neuronos_model_entry_t *neuronos_model_scan(const char *dir, int *count);
neuronos_model_entry_t *neuronos_model_select(const neuronos_model_entry_t *models,
                                               int count,
                                               const neuronos_hw_info_t *hw);
void neuronos_model_scan_free(neuronos_model_entry_t *entries, int count);

// Context compaction
int neuronos_context_compact(neuronos_agent_t *agent);
int neuronos_context_token_count(const neuronos_agent_t *agent);
```

### New CLI Commands
```bash
# Auto-detect best model and run agent
neuronos-cli auto agent "task description"

# Scan models in directory
neuronos-cli scan [models-dir]

# Show hardware info
neuronos-cli hwinfo
```
