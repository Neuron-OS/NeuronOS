# Changelog

All notable changes to NeuronOS are documented here.

## [0.9.2] - 2026-02-18

### Changed
- **Default mode**: Running `neuronos` without arguments now launches interactive terminal chat (like Claude Code) instead of web server
- Improved first-run welcome experience with model auto-detection

### Fixed
- Windows cp1252 encoding error in embed_webui.py (Unicode arrow replaced)
- Win32 popen/pclose compatibility in model_registry.c
- Release asset naming consistency across all platforms

## [0.9.1] - 2026-02-14

### Added
- **MCP Client**: Pure C implementation (~1,370 lines) for connecting to external MCP servers
- **Model Registry**: Multi-model catalog with HW-aware scoring and auto-download
- **Web UI**: Embedded single-binary chat interface with SSE streaming
- **WASM Runtime**: Emscripten build for browser-based agent execution
- **GPU Support**: CUDA backend for Q4_K_M models, Vulkan GPU detection
- **GPU Auto-Tuning**: Intelligent layer offloading based on VRAM budget
- **Memory Tools**: `memory_store`, `memory_search`, `memory_core_update` registered for MCP
- **JSON Parser**: Unified parser replacing 4 duplicated implementations (15 tests)
- **CLI Modes**: 8 modes (run, chat, agent, serve, mcp, model, hwinfo, scan)
- **One-Command Installer**: HW-aware model selection and auto-download

### Fixed
- Buffer safety hardening across agent and memory modules
- Recall memory garbage collection
- Windows build portability (POSIX to Win32 API guards)
- macOS build (`sys/sysctl.h` include)

## [0.8.0] - 2026-02-07

### Added
- **MemGPT 3-Tier Memory**: Core/Recall/Archival with SQLite + FTS5
- **MCP Server**: JSON-RPC 2.0 over STDIO transport
- **PDF Tool**: `read_pdf` for document reading
- **Release Infrastructure**: CI/CD for 5 platforms (Linux x86_64, Linux ARM64, macOS ARM64, Windows x64, Android ARM64)

### Core
- ReAct agent loop with GBNF-constrained tool calling
- HAL with runtime ISA dispatch (scalar, AVX2, AVX-VNNI, NEON)
- OpenAI-compatible HTTP server (`/v1/chat/completions`)
- 9 built-in tools (shell, read_file, write_file, list_dir, search_files, read_pdf, http_get, calculate, get_time)
- 27/27 tests passing (4 HAL + 11 engine + 12 memory)
