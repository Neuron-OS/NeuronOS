# NeuronOS — Product Identity

> Sovereign AI agent runtime. Pure C11. Zero dependencies. Runs anywhere.

## What It Is

NeuronOS is the first **full AI agent engine** written in pure C11 — not just inference, not just serving, but a complete ReAct agent runtime with memory, tools, and multi-platform hardware acceleration. It runs on a Raspberry Pi with 1.5GB RAM.

- **~12,600 lines of C11**, 19 source files
- **ReAct agent engine** with 12 built-in tools
- **GBNF grammar-constrained tool calling** — structured output without prompt hacking
- **3-tier MemGPT memory**: Core (persona/system) / Recall (conversation history) / Archival (long-term knowledge) backed by SQLite + FTS5
- **MCP Server** (JSON-RPC over STDIO) + **MCP Client** (~1,370 LOC)
- **HAL** with 5 ISA backends: scalar, AVX2, AVX-VNNI, NEON, Vulkan
- **OpenAI-compatible HTTP API** with SSE streaming
- **Embedded Web UI**, model registry with auto-download
- **27/27 tests passing**, v0.9.2
- **BitNet 1.58-bit** quantization — runs real agents on edge hardware
- Build: `cmake` / Test: `ctest`
- CI/CD: GitHub releases for Linux x64, Linux ARM64, macOS x64, macOS ARM64, Windows x64

## Market Position

### Target Markets
1. **IoT / Edge AI** — Factories, drones, robots, kiosks that need local AI agents without cloud dependency
2. **Privacy-first devices** — Medical devices, government systems, air-gapped environments
3. **Embedded AI products** — Hardware companies shipping AI-powered devices
4. **Developer tools** — Anyone building agents who wants C-level performance and no Python bloat

### Competitive Landscape

| Feature | NeuronOS | llama.cpp | Ollama | LangChain |
|---|---|---|---|---|
| Language | C11 | C/C++ | Go + llama.cpp | Python |
| Inference | ✅ | ✅ | ✅ | ❌ (wraps others) |
| Agent engine (ReAct) | ✅ | ❌ | ❌ | ✅ |
| Persistent memory | ✅ (3-tier MemGPT) | ❌ | ❌ | ❌ (stateless) |
| Tool calling (grammar) | ✅ (GBNF) | Partial | ❌ | Prompt-based |
| MCP support | ✅ Server + Client | ❌ | ❌ | Partial |
| Edge viable (1.5GB) | ✅ | ✅ (inference only) | ❌ (needs 4GB+) | ❌ |
| Dependencies | 0 | 0 | Go runtime | Python + 200 deps |
| Hardware accel | 5 ISA backends | 4+ backends | Via llama.cpp | N/A |

**Key differentiator**: llama.cpp does inference. Ollama does serving. NeuronOS does **agents** — with memory, tools, and planning — in a single binary that fits on a Raspberry Pi.

## Pricing Model — Open-Core

### Free Tier (MIT License)
- Full source code, all features
- Community support via GitHub
- Use in personal/hobby/startup projects
- No telemetry, no phone-home

### Pro License — $99/device/year
- Commercial use in closed-source products
- Priority GitHub issues
- License key validation (offline-capable)
- Up to 100 devices

### Enterprise License — $999/year unlimited
- Unlimited devices
- Dedicated support channel
- Custom HAL backend consultation
- SLA: 48h response time

### Managed API (Future — M2+)
- Cloud-hosted NeuronOS agents
- Per-request pricing: $0.001/agent step
- For customers who don't want to self-host

## Current Status

| Metric | Value |
|---|---|
| Version | v0.9.2 |
| Tests | 27/27 passing |
| LOC | ~12,600 C11 |
| Platforms | 5 (Linux/macOS/Windows × x64/ARM64) |
| Models tested | BitNet 1.58b, Qwen 2.5, Phi-3, Llama 3.2 |
| MCP tools | 12 built-in |
| Documentation | Incomplete — blocks v1.0 |

## Blockers to Revenue

1. **Documentation** — No public docs site, README is the only reference
2. **License enforcement** — No Pro license key system built yet
3. **Website** — No marketing site, no way to purchase
4. **Model registry** — Works but list is small, needs GGUF ecosystem coverage
5. **Windows testing** — CI builds but limited manual testing

## One-Liner

**NeuronOS**: The AI agent that runs on a Raspberry Pi.
