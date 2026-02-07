/* ============================================================
 * NeuronOS — Tool Registry
 * Register, discover, and execute tools for the agent.
 *
 * Phase 2C: tool registration and dispatch.
 * ============================================================ */
#include "neuronos/neuronos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Constants ---- */
#define NEURONOS_MAX_TOOLS 64

/* ---- Internal struct ---- */
struct neuronos_tool_reg {
    neuronos_tool_desc_t tools[NEURONOS_MAX_TOOLS];
    int count;
};

/* ============================================================
 * REGISTRY LIFECYCLE
 * ============================================================ */

neuronos_tool_registry_t * neuronos_tool_registry_create(void) {
    neuronos_tool_registry_t * reg = calloc(1, sizeof(neuronos_tool_registry_t));
    return reg;
}

void neuronos_tool_registry_free(neuronos_tool_registry_t * reg) {
    free(reg);
}

/* ============================================================
 * REGISTER
 * ============================================================ */

int neuronos_tool_register(neuronos_tool_registry_t * reg, const neuronos_tool_desc_t * desc) {
    if (!reg || !desc || !desc->name || !desc->execute)
        return -1;
    if (reg->count >= NEURONOS_MAX_TOOLS)
        return -1;

    /* Check for duplicate name */
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->tools[i].name, desc->name) == 0) {
            return -1; /* duplicate */
        }
    }

    reg->tools[reg->count] = *desc;
    reg->count++;
    return 0;
}

/* ============================================================
 * EXECUTE
 * ============================================================ */

neuronos_tool_result_t neuronos_tool_execute(neuronos_tool_registry_t * reg, const char * tool_name,
                                             const char * args_json) {
    neuronos_tool_result_t result = {0};

    if (!reg || !tool_name) {
        result.success = false;
        result.error = strdup("Invalid arguments");
        return result;
    }

    /* Find tool */
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->tools[i].name, tool_name) == 0) {
            return reg->tools[i].execute(args_json ? args_json : "{}", reg->tools[i].user_data);
        }
    }

    result.success = false;
    result.error = strdup("Tool not found");
    return result;
}

void neuronos_tool_result_free(neuronos_tool_result_t * result) {
    if (!result)
        return;
    free(result->output);
    free(result->error);
    result->output = NULL;
    result->error = NULL;
}

/* ============================================================
 * INSPECTION
 * ============================================================ */

int neuronos_tool_count(const neuronos_tool_registry_t * reg) {
    return reg ? reg->count : 0;
}

const char * neuronos_tool_name(const neuronos_tool_registry_t * reg, int index) {
    if (!reg || index < 0 || index >= reg->count)
        return NULL;
    return reg->tools[index].name;
}

/* ============================================================
 * GBNF GRAMMAR GENERATION
 * ============================================================ */

/*
 * Generate GBNF rule for tool names:
 *   tool-name ::= "\"shell\"" | "\"read_file\"" | ...
 */
char * neuronos_tool_grammar_names(const neuronos_tool_registry_t * reg) {
    if (!reg || reg->count == 0)
        return strdup("tool-name ::= \"\\\"noop\\\"\"");

    /* Estimate buffer size: each tool adds ~30 chars */
    size_t cap = 256 + (size_t)reg->count * 40;
    char * buf = malloc(cap);
    if (!buf)
        return NULL;

    size_t len = 0;
    len += (size_t)snprintf(buf + len, cap - len, "tool-name ::= ");

    for (int i = 0; i < reg->count; i++) {
        if (i > 0) {
            len += (size_t)snprintf(buf + len, cap - len, " | ");
        }
        len += (size_t)snprintf(buf + len, cap - len, "\"\\\"");
        len += (size_t)snprintf(buf + len, cap - len, "%s", reg->tools[i].name);
        len += (size_t)snprintf(buf + len, cap - len, "\\\"\"");
    }

    return buf;
}

/*
 * Generate tool descriptions for the system prompt:
 * Available tools:
 * - shell: Execute a shell command. Args: {"command": "<string>"}
 * - read_file: Read a file. Args: {"path": "<string>"}
 * ...
 */
char * neuronos_tool_prompt_description(const neuronos_tool_registry_t * reg) {
    if (!reg || reg->count == 0) {
        return strdup("No tools available.\n");
    }

    size_t cap = 512 + (size_t)reg->count * 256;
    char * buf = malloc(cap);
    if (!buf)
        return NULL;

    size_t len = 0;
    len += (size_t)snprintf(buf + len, cap - len, "Available tools:\n");

    for (int i = 0; i < reg->count; i++) {
        len += (size_t)snprintf(buf + len, cap - len, "- %s: %s", reg->tools[i].name,
                                reg->tools[i].description ? reg->tools[i].description : "No description");

        if (reg->tools[i].args_schema_json) {
            len += (size_t)snprintf(buf + len, cap - len, " Args schema: %s", reg->tools[i].args_schema_json);
        }
        len += (size_t)snprintf(buf + len, cap - len, "\n");
    }

    return buf;
}

/* ============================================================
 * BUILT-IN TOOLS
 * ============================================================ */

/* --- shell tool --- */
static neuronos_tool_result_t tool_shell(const char * args_json, void * user_data) {
    (void)user_data;
    neuronos_tool_result_t result = {0};

    /* Minimal JSON parsing: extract "command" field */
    const char * cmd_start = strstr(args_json, "\"command\"");
    if (!cmd_start) {
        result.success = false;
        result.error = strdup("Missing 'command' argument");
        return result;
    }

    /* Find the value string */
    cmd_start = strchr(cmd_start + 9, '"');
    if (!cmd_start) {
        result.success = false;
        result.error = strdup("Invalid 'command' format");
        return result;
    }
    cmd_start++; /* skip opening quote */

    /* Find closing quote (handle escapes simply) */
    const char * cmd_end = cmd_start;
    while (*cmd_end && *cmd_end != '"') {
        if (*cmd_end == '\\')
            cmd_end++; /* skip escaped char */
        if (*cmd_end)
            cmd_end++;
    }

    size_t cmd_len = (size_t)(cmd_end - cmd_start);
    char * command = malloc(cmd_len + 1);
    memcpy(command, cmd_start, cmd_len);
    command[cmd_len] = '\0';

    /* Execute with popen */
    FILE * fp = popen(command, "r");
    free(command);

    if (!fp) {
        result.success = false;
        result.error = strdup("Failed to execute command");
        return result;
    }

    /* Read output */
    size_t out_cap = 4096;
    size_t out_len = 0;
    char * out_buf = malloc(out_cap);

    char line[512];
    while (fgets(line, (int)sizeof(line), fp)) {
        size_t line_len = strlen(line);
        while (out_len + line_len + 1 > out_cap) {
            out_cap *= 2;
            out_buf = realloc(out_buf, out_cap);
        }
        memcpy(out_buf + out_len, line, line_len);
        out_len += line_len;
    }
    out_buf[out_len] = '\0';

    int status = pclose(fp);
    result.success = (status == 0);
    result.output = out_buf;

    if (!result.success) {
        char err_msg[64];
        snprintf(err_msg, sizeof(err_msg), "Command exited with status %d", status);
        result.error = strdup(err_msg);
    }

    return result;
}

/* --- read_file tool --- */
static neuronos_tool_result_t tool_read_file(const char * args_json, void * user_data) {
    (void)user_data;
    neuronos_tool_result_t result = {0};

    const char * path_start = strstr(args_json, "\"path\"");
    if (!path_start) {
        result.success = false;
        result.error = strdup("Missing 'path' argument");
        return result;
    }
    path_start = strchr(path_start + 6, '"');
    if (!path_start) {
        result.success = false;
        result.error = strdup("Invalid 'path'");
        return result;
    }
    path_start++;
    const char * path_end = strchr(path_start, '"');
    if (!path_end) {
        result.success = false;
        result.error = strdup("Invalid 'path'");
        return result;
    }

    size_t path_len = (size_t)(path_end - path_start);
    char * path = malloc(path_len + 1);
    memcpy(path, path_start, path_len);
    path[path_len] = '\0';

    FILE * fp = fopen(path, "r");
    free(path);

    if (!fp) {
        result.success = false;
        result.error = strdup("File not found or cannot read");
        return result;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Limit to 32KB for context budget */
    if (fsize > 32 * 1024)
        fsize = 32 * 1024;

    char * content = malloc((size_t)fsize + 1);
    size_t nread = fread(content, 1, (size_t)fsize, fp);
    content[nread] = '\0';
    fclose(fp);

    result.success = true;
    result.output = content;
    return result;
}

/* --- write_file tool --- */
static neuronos_tool_result_t tool_write_file(const char * args_json, void * user_data) {
    (void)user_data;
    neuronos_tool_result_t result = {0};

    /* Extract path */
    const char * path_start = strstr(args_json, "\"path\"");
    if (!path_start) {
        result.success = false;
        result.error = strdup("Missing 'path'");
        return result;
    }
    path_start = strchr(path_start + 6, '"');
    if (!path_start) {
        result.success = false;
        result.error = strdup("Invalid 'path'");
        return result;
    }
    path_start++;
    const char * path_end = strchr(path_start, '"');
    if (!path_end) {
        result.success = false;
        result.error = strdup("Invalid 'path'");
        return result;
    }

    size_t path_len = (size_t)(path_end - path_start);
    char * path = malloc(path_len + 1);
    memcpy(path, path_start, path_len);
    path[path_len] = '\0';

    /* Extract content */
    const char * cnt_start = strstr(args_json, "\"content\"");
    if (!cnt_start) {
        free(path);
        result.success = false;
        result.error = strdup("Missing 'content'");
        return result;
    }
    cnt_start = strchr(cnt_start + 9, '"');
    if (!cnt_start) {
        free(path);
        result.success = false;
        result.error = strdup("Invalid 'content'");
        return result;
    }
    cnt_start++;
    const char * cnt_end = cnt_start;
    while (*cnt_end && *cnt_end != '"') {
        if (*cnt_end == '\\')
            cnt_end++;
        if (*cnt_end)
            cnt_end++;
    }

    size_t cnt_len = (size_t)(cnt_end - cnt_start);
    char * content = malloc(cnt_len + 1);
    memcpy(content, cnt_start, cnt_len);
    content[cnt_len] = '\0';

    FILE * fp = fopen(path, "w");
    free(path);

    if (!fp) {
        free(content);
        result.success = false;
        result.error = strdup("Cannot write file");
        return result;
    }

    fwrite(content, 1, cnt_len, fp);
    fclose(fp);
    free(content);

    result.success = true;
    result.output = strdup("File written successfully");
    return result;
}

/* --- calculate tool --- */
static neuronos_tool_result_t tool_calculate(const char * args_json, void * user_data) {
    (void)user_data;
    neuronos_tool_result_t result = {0};

    /* Simple: extract "expression" and eval via shell bc */
    const char * expr_start = strstr(args_json, "\"expression\"");
    if (!expr_start) {
        result.success = false;
        result.error = strdup("Missing 'expression' argument");
        return result;
    }
    expr_start = strchr(expr_start + 12, '"');
    if (!expr_start) {
        result.success = false;
        result.error = strdup("Invalid 'expression'");
        return result;
    }
    expr_start++;
    const char * expr_end = strchr(expr_start, '"');
    if (!expr_end) {
        result.success = false;
        result.error = strdup("Invalid 'expression'");
        return result;
    }

    size_t expr_len = (size_t)(expr_end - expr_start);
    /* Build command: echo 'EXPR' | bc -l */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "echo '%.*s' | bc -l 2>&1", (int)expr_len, expr_start);

    FILE * fp = popen(cmd, "r");
    if (!fp) {
        result.success = false;
        result.error = strdup("bc not available");
        return result;
    }

    char out[256];
    if (fgets(out, (int)sizeof(out), fp)) {
        /* Trim trailing newline */
        size_t len = strlen(out);
        while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
            out[--len] = '\0';
        }
        result.output = strdup(out);
    } else {
        result.output = strdup("0");
    }

    pclose(fp);
    result.success = true;
    return result;
}

/* ---- Register defaults ---- */
int neuronos_tool_register_defaults(neuronos_tool_registry_t * reg, uint32_t allowed_caps) {
    if (!reg)
        return -1;
    int registered = 0;

    if (allowed_caps & NEURONOS_CAP_SHELL) {
        neuronos_tool_desc_t desc = {
            .name = "shell",
            .description = "Execute a shell command and return its output.",
            .args_schema_json = "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":"
                                "\"The shell command to execute\"}},\"required\":[\"command\"]}",
            .execute = tool_shell,
            .user_data = NULL,
            .required_caps = NEURONOS_CAP_SHELL,
        };
        if (neuronos_tool_register(reg, &desc) == 0)
            registered++;
    }

    if (allowed_caps & NEURONOS_CAP_FILESYSTEM) {
        neuronos_tool_desc_t desc_read = {
            .name = "read_file",
            .description = "Read the contents of a file (max 32KB).",
            .args_schema_json = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":"
                                "\"File path to read\"}},\"required\":[\"path\"]}",
            .execute = tool_read_file,
            .user_data = NULL,
            .required_caps = NEURONOS_CAP_FILESYSTEM,
        };
        if (neuronos_tool_register(reg, &desc_read) == 0)
            registered++;

        neuronos_tool_desc_t desc_write = {
            .name = "write_file",
            .description = "Write content to a file.",
            .args_schema_json = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{"
                                "\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}",
            .execute = tool_write_file,
            .user_data = NULL,
            .required_caps = NEURONOS_CAP_FILESYSTEM,
        };
        if (neuronos_tool_register(reg, &desc_write) == 0)
            registered++;
    }

    {
        neuronos_tool_desc_t desc_calc = {
            .name = "calculate",
            .description = "Evaluate a mathematical expression (uses bc).",
            .args_schema_json =
                "{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\",\"description\":\"Math "
                "expression, e.g. 2+2, sqrt(144)\"}},\"required\":[\"expression\"]}",
            .execute = tool_calculate,
            .user_data = NULL,
            .required_caps = 0, /* no special capabilities needed */
        };
        if (neuronos_tool_register(reg, &desc_calc) == 0)
            registered++;
    }

    return registered;
}
