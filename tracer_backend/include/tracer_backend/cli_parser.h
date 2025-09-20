#ifndef TRACER_BACKEND_CLI_PARSER_H
#define TRACER_BACKEND_CLI_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLI_PARSER_ERROR_LEN 256

// Execution modes supported by the CLI parser.
typedef enum ExecutionMode {
    MODE_INVALID = 0,
    MODE_SPAWN,
    MODE_ATTACH,
    MODE_HELP,
    MODE_VERSION
} ExecutionMode;

// Definition for a CLI flag (long + short variant and expected value requirements).
typedef struct FlagDefinition {
    const char* long_name;
    char short_name;
    bool expects_value;
    const char* description;
} FlagDefinition;

typedef enum TriggerType {
    TRIGGER_TYPE_INVALID = 0,
    TRIGGER_TYPE_SYMBOL,
    TRIGGER_TYPE_CRASH,
    TRIGGER_TYPE_TIME
} TriggerType;

typedef struct TriggerDefinition {
    TriggerType type;
    char* raw_value;
    char* symbol_name;
    char* module_name;
    uint32_t time_seconds;
} TriggerDefinition;

typedef struct TriggerList {
    TriggerDefinition* entries;
    size_t count;
    size_t capacity;
} TriggerList;

// Parsed tracer configuration placeholder for early iterations.
typedef struct TracerConfig {
    ExecutionMode mode;
    struct {
        const char* executable;
        char** argv;
        int argc;
    } spawn;
    struct {
        pid_t pid;
        const char* process_name;
    } attach;
    struct {
        const char* output_dir;
        bool output_specified;
    } output;
    struct {
        uint32_t duration_seconds;
        bool duration_specified;
        uint32_t pre_roll_seconds;
        bool pre_roll_specified;
        uint32_t post_roll_seconds;
        bool post_roll_specified;
    } timing;
    struct {
        uint32_t stack_bytes;
        bool stack_specified;
    } capture;
    TriggerList triggers;
    struct {
        char** modules;
        size_t count;
        size_t capacity;
        bool exclude_specified;
    } filters;
    bool help_requested;
    bool version_requested;
} TracerConfig;

// Parser instance used across parsing stages.
typedef struct CLIParser {
    int argc;
    char** argv;
    int current_arg;
    ExecutionMode detected_mode;
    bool has_error;
    char error_message[CLI_PARSER_ERROR_LEN];
    TracerConfig config;
    const FlagDefinition* flag_registry;
    size_t flag_count;
    bool* consumed_args;
} CLIParser;

CLIParser* cli_parser_create(int argc, char** argv);
void cli_parser_destroy(CLIParser* parser);

ExecutionMode cli_parser_detect_mode(CLIParser* parser);

bool cli_parse_mode_args(CLIParser* parser);
bool cli_parse_flags(CLIParser* parser);

bool cli_parser_has_error(const CLIParser* parser);
const char* cli_parser_get_error(const CLIParser* parser);

TracerConfig* cli_parser_get_config(CLIParser* parser);
const TracerConfig* cli_parser_get_config_const(const CLIParser* parser);

const FlagDefinition* cli_parser_get_flags(size_t* count);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // TRACER_BACKEND_CLI_PARSER_H
