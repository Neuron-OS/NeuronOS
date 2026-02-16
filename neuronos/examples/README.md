# NeuronOS Integration Examples

## Claude Code (Anthropic Messages API)

NeuronOS serves as a local model backend for Claude Code via the Anthropic Messages API (`POST /v1/messages`).

### Setup

1. Start NeuronOS server:
```bash
neuronos serve --port 8080
```

2. Configure Claude Code environment:
```bash
export ANTHROPIC_BASE_URL=http://localhost:8080
export ANTHROPIC_AUTH_TOKEN=neuronos
export ANTHROPIC_MODEL=neuronos-local
```

3. Or add to `~/.claude/settings.json`:
```json
{
  "env": {
    "ANTHROPIC_BASE_URL": "http://localhost:8080",
    "ANTHROPIC_AUTH_TOKEN": "neuronos",
    "ANTHROPIC_MODEL": "neuronos-local"
  }
}
```

4. Use Claude Code normally — it will route requests through NeuronOS:
```bash
claude "explain this codebase"
```

### Test with curl

```bash
# Non-streaming
curl http://localhost:8080/v1/messages \
  -H "Content-Type: application/json" \
  -H "x-api-key: neuronos" \
  -d '{
    "model": "neuronos-local",
    "max_tokens": 256,
    "messages": [{"role": "user", "content": "Hello!"}]
  }'

# Streaming
curl http://localhost:8080/v1/messages \
  -H "Content-Type: application/json" \
  -H "x-api-key: neuronos" \
  -d '{
    "model": "neuronos-local",
    "max_tokens": 256,
    "stream": true,
    "messages": [{"role": "user", "content": "Hello!"}]
  }'
```

---

## OpenCode (OpenAI-compatible API)

NeuronOS works with OpenCode out of the box via the OpenAI Chat API (`POST /v1/chat/completions`).

### Setup

1. Start NeuronOS server:
```bash
neuronos serve --port 8080
```

2. Create `~/.config/opencode/opencode.json`:
```json
{
  "provider": {
    "neuronos": {
      "npm": "@ai-sdk/openai-compatible",
      "options": {
        "baseURL": "http://localhost:8080/v1",
        "name": "neuronos"
      },
      "models": {
        "neuronos-local": {
          "name": "NeuronOS BitNet 2B (1.58-bit)",
          "attachment": false
        }
      }
    }
  }
}
```

3. Create `~/.local/share/opencode/auth.json`:
```json
{
  "neuronos": {
    "type": "api",
    "key": "sk-neuronos-local"
  }
}
```

4. Run OpenCode:
```bash
opencode
```

### Test with curl

```bash
curl http://localhost:8080/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "neuronos-local",
    "messages": [{"role": "user", "content": "Hello!"}],
    "max_tokens": 256,
    "stream": true
  }'
```

---

## MCP Server (Claude Code + OpenCode)

NeuronOS exposes its tools via MCP (STDIO transport), allowing Claude Code and OpenCode to use NeuronOS tools directly.

### Claude Code
```bash
claude mcp add neuronos -- neuronos mcp
```

### OpenCode
```bash
opencode mcp add neuronos -- neuronos mcp
```

### Available MCP Tools
- `shell` — Execute shell commands
- `read_file` — Read file contents
- `write_file` — Write files
- `list_dir` — List directory contents
- `search_files` — Search files by pattern
- `read_pdf` — Extract text from PDFs
- `http_get` — HTTP GET requests
- `calculate` — Mathematical expressions
- `get_time` — Current date/time
- `memory_store` — Store facts in persistent memory
- `memory_search` — Search persistent memory (FTS5)
- `memory_core_update` — Update agent core memory blocks
