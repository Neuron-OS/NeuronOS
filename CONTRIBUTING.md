# Contributing to NeuronOS

## Quick Start

```bash
git clone --recursive https://github.com/Neuron-OS/neuronos.git
cd neuronos
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/bin/test_hal && ./build/bin/test_memory && ./build/bin/test_engine
```

## Rules

- **Language**: C11 for all public APIs. C++ only for llama.cpp integration.
- **Prefix**: All public symbols use `neuronos_` prefix.
- **Style**: `snake_case` functions/variables, `UPPER_CASE` macros/enums, 120-char max line width.
- **Tests**: All 27 tests must pass before submitting a PR.
- **Dependencies**: No Python/Node/Go/Rust runtime deps. Embed C libraries only.
- **llama.cpp**: Wrap it, never modify it directly.

## Module Boundaries

| Module | May depend on | Must NOT depend on |
|--------|--------------|-------------------|
| HAL | libc, intrinsics | engine, agent, llama.cpp |
| Engine | HAL, llama.cpp | agent, tools, CLI |
| Memory | SQLite | engine, agent, llama.cpp |
| Agent | Engine, Memory | CLI, server, MCP |
| Interface | Agent, Engine, HAL | (leaf nodes) |

## Pull Requests

1. Fork and create a feature branch
2. Make your changes
3. Run all tests: `./build/bin/test_hal && ./build/bin/test_memory && ./build/bin/test_engine`
4. Submit PR with the template filled out

## Reporting Issues

Use [GitHub Issues](https://github.com/Neuron-OS/neuronos/issues) with the provided templates.
