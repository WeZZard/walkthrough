#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tracer_backend/cli_parser.h>

static const FlagDefinition kFlagRegistry[] = {
    {"help", 'h', false, "Show help information"},
    {"version", 'v', false, "Show version information"},
    {"output", 'o', true, "Output directory"},
    {"duration", 'd', true, "Tracing duration"},
    {"stack-bytes", 's', true, "Stack capture size"},
    {"pre-roll-sec", 0, true, "Pre-roll buffer seconds"},
    {"post-roll-sec", 0, true, "Post-roll buffer seconds"},
    {"trigger", 't', true, "Trigger specification"},
    {"exclude", 'x', true, "Module exclusion list"},
};

static const size_t kFlagRegistryCount = sizeof(kFlagRegistry) / sizeof(kFlagRegistry[0]);

static void cli_parser_clear_error(CLIParser* parser);
static void cli_parser_set_error(CLIParser* parser, const char* fmt, ...);
static bool cli_arg_is_help(const char* arg);
static bool cli_arg_is_version(const char* arg);
static void cli_reset_spawn_args(TracerConfig* config);
static void cli_reset_triggers(TracerConfig* config);
static void cli_reset_filters(TracerConfig* config);
static const FlagDefinition* cli_lookup_long_flag(const CLIParser* parser, const char* name, size_t name_len);
static const FlagDefinition* cli_lookup_short_flag(const CLIParser* parser, char short_name);
static bool cli_skip_known_flag(const CLIParser* parser, const char* arg, int* index);
static bool cli_parse_spawn_mode_args(CLIParser* parser);
static bool cli_parse_attach_mode_args(CLIParser* parser);
static bool cli_dispatch_flag(CLIParser* parser, const FlagDefinition* definition, const char* value);
static bool handle_output_flag(CLIParser* parser, const char* value);
static bool handle_duration_flag(CLIParser* parser, const char* value);
static bool handle_stack_flag(CLIParser* parser, const char* value);
static bool handle_help_flag(CLIParser* parser);
static bool handle_version_flag(CLIParser* parser);
static bool handle_trigger_flag(CLIParser* parser, const char* value);
static bool handle_pre_roll_flag(CLIParser* parser, const char* value);
static bool handle_post_roll_flag(CLIParser* parser, const char* value);
static bool handle_exclude_flag(CLIParser* parser, const char* value);
static int cli_find_next_unconsumed(const CLIParser* parser, int start_index);
static bool cli_append_trigger(CLIParser* parser, TriggerType type, char* raw_value, char* symbol, char* module, uint32_t time_seconds);
static bool cli_ensure_trigger_capacity(TriggerList* list, size_t required);
static char* cli_strdup(const char* source);
static char* cli_strndup(const char* source, size_t length);
static bool cli_parse_u32(const char* value, uint32_t max_value, uint32_t* out);
static bool cli_append_filter_module(TracerConfig* config, char* module_name);
static bool cli_ensure_filter_capacity(TracerConfig* config, size_t required);
static bool cli_module_exists(const TracerConfig* config, const char* module_name);
static bool cli_validate_module_name(const char* module_name);
static void cli_trim_bounds(const char* start, size_t length, size_t* offset, size_t* trimmed_length);

#define CLI_DURATION_MAX_SECONDS 86400u
#define CLI_STACK_MAX_BYTES 512u
#define CLI_PERSISTENCE_MAX_SECONDS 86400u

CLIParser* cli_parser_create(int argc, char** argv) {
    CLIParser* parser = (CLIParser*)calloc(1u, sizeof(CLIParser));
    if (parser == NULL) {
        return NULL;
    }

    parser->argc = argc;
    parser->argv = argv;
    parser->current_arg = 0;
    parser->detected_mode = MODE_INVALID;
    parser->has_error = false;
    parser->error_message[0] = '\0';
    parser->config.mode = MODE_INVALID;
    parser->config.spawn.executable = NULL;
    parser->config.spawn.argv = NULL;
    parser->config.spawn.argc = 0;
    parser->config.attach.pid = 0;
    parser->config.attach.process_name = NULL;
    parser->config.output.output_dir = NULL;
    parser->config.output.output_specified = false;
    parser->config.timing.duration_seconds = 0;
    parser->config.timing.duration_specified = false;
    parser->config.timing.pre_roll_seconds = 0;
    parser->config.timing.pre_roll_specified = false;
    parser->config.timing.post_roll_seconds = 0;
    parser->config.timing.post_roll_specified = false;
    parser->config.capture.stack_bytes = 0;
    parser->config.capture.stack_specified = false;
    parser->config.triggers.entries = NULL;
    parser->config.triggers.count = 0;
    parser->config.triggers.capacity = 0;
    parser->config.filters.modules = NULL;
    parser->config.filters.count = 0;
    parser->config.filters.capacity = 0;
    parser->config.filters.exclude_specified = false;
    parser->config.help_requested = false;
    parser->config.version_requested = false;
    parser->flag_registry = kFlagRegistry;
    parser->flag_count = kFlagRegistryCount;
    parser->consumed_args = NULL;

    if (argc > 0) {
        parser->consumed_args = (bool*)calloc((size_t)argc, sizeof(bool));
        if (parser->consumed_args == NULL) {
            free(parser);
            return NULL;
        }
    }

    return parser;
}

void cli_parser_destroy(CLIParser* parser) {
    if (parser == NULL) {
        return;
    }

    cli_reset_spawn_args(&parser->config);
    cli_reset_triggers(&parser->config);
    cli_reset_filters(&parser->config);

    if (parser->consumed_args != NULL) {
        free(parser->consumed_args);
        parser->consumed_args = NULL;
    }

    free(parser);
}

ExecutionMode cli_parser_detect_mode(CLIParser* parser) {
    if (parser == NULL) {
        return MODE_INVALID;
    }

    cli_parser_clear_error(parser);
    parser->detected_mode = MODE_INVALID;
    parser->config.mode = MODE_INVALID;
    parser->current_arg = 0;

    if (parser->argc <= 1) {
        cli_parser_set_error(parser, "No command specified.");
        return MODE_INVALID;
    }

    int index = 1;
    if (index < parser->argc) {
        const char* candidate = parser->argv[index];
        if (candidate != NULL && strcmp(candidate, "trace") == 0) {
            index++;
        }
    }

    for (int i = index; i < parser->argc; ++i) {
        const char* arg = parser->argv[i];
        if (arg == NULL || arg[0] == '\0') {
            continue;
        }

        if (cli_arg_is_help(arg)) {
            parser->detected_mode = MODE_HELP;
            parser->config.mode = MODE_HELP;
            parser->current_arg = i + 1;
            return MODE_HELP;
        }
        if (cli_arg_is_version(arg)) {
            parser->detected_mode = MODE_VERSION;
            parser->config.mode = MODE_VERSION;
            parser->current_arg = i + 1;
            return MODE_VERSION;
        }

        if (arg[0] == '-') {
            // Skip over other flags during mode detection; they are handled later.
            continue;
        }

        if (strcmp(arg, "spawn") == 0) {
            parser->detected_mode = MODE_SPAWN;
            parser->config.mode = MODE_SPAWN;
            parser->current_arg = i + 1;
            return MODE_SPAWN;
        }

        if (strcmp(arg, "attach") == 0) {
            parser->detected_mode = MODE_ATTACH;
            parser->config.mode = MODE_ATTACH;
            parser->current_arg = i + 1;
            return MODE_ATTACH;
        }

        // Note: "help" and "version" commands are already handled by
        // cli_arg_is_help() and cli_arg_is_version() above

        cli_parser_set_error(parser, "Invalid command '%s'. Expected 'spawn' or 'attach'.", arg);
        parser->detected_mode = MODE_INVALID;
        parser->config.mode = MODE_INVALID;
        parser->current_arg = i;
        return MODE_INVALID;
    }

    cli_parser_set_error(parser, "No command specified after 'trace'.");
    parser->detected_mode = MODE_INVALID;
    parser->config.mode = MODE_INVALID;
    parser->current_arg = parser->argc;
    return MODE_INVALID;
}

bool cli_parse_mode_args(CLIParser* parser) {
    if (parser == NULL) {
        return false;
    }

    if (parser->has_error) {
        return false;
    }

    if (parser->consumed_args != NULL && parser->argc > 0) {
        memset(parser->consumed_args, 0, (size_t)parser->argc * sizeof(bool));
    }

    switch (parser->config.mode) {
        case MODE_SPAWN:
            return cli_parse_spawn_mode_args(parser);
        case MODE_ATTACH:
            return cli_parse_attach_mode_args(parser);
        case MODE_HELP:
        case MODE_VERSION:
            return true;
        default:
            cli_parser_set_error(parser, "Cannot parse positional arguments without a valid mode.");
            return false;
    }
}

bool cli_parse_flags(CLIParser* parser) {
    if (parser == NULL) {
        return false;
    }

    if (parser->has_error) {
        return false;
    }

    parser->config.output.output_dir = NULL;
    parser->config.output.output_specified = false;
    parser->config.timing.duration_seconds = 0;
    parser->config.timing.duration_specified = false;
    parser->config.timing.pre_roll_seconds = 0;
    parser->config.timing.pre_roll_specified = false;
    parser->config.timing.post_roll_seconds = 0;
    parser->config.timing.post_roll_specified = false;
    parser->config.capture.stack_bytes = 0;
    parser->config.capture.stack_specified = false;
    parser->config.filters.exclude_specified = false;
    parser->config.help_requested = false;
    parser->config.version_requested = false;
    cli_reset_triggers(&parser->config);
    cli_reset_filters(&parser->config);

    for (int i = parser->current_arg; i < parser->argc; ++i) {
        if (parser->consumed_args != NULL && parser->consumed_args[i]) {
            continue;
        }

        char* arg = parser->argv[i];
        if (arg == NULL || arg[0] == '\0') {
            continue;
        }

        if (strcmp(arg, "--") == 0) {
            if (parser->consumed_args != NULL) {
                parser->consumed_args[i] = true;
            }
            continue;
        }

        if (arg[0] != '-' || arg[1] == '\0') {
            continue;
        }

        if (arg[1] == '-') {
            const char* flag_body = arg + 2;
            if (*flag_body == '\0') {
                continue;
            }

            const char* value = NULL;
            const char* assignment = strchr(flag_body, '=');
            size_t name_len = assignment != NULL ? (size_t)(assignment - flag_body) : strlen(flag_body);
            const FlagDefinition* definition = cli_lookup_long_flag(parser, flag_body, name_len);
            if (definition == NULL) {
                cli_parser_set_error(parser, "Unknown flag '--%.*s'.", (int)name_len, flag_body);
                return false;
            }

            if (parser->consumed_args != NULL) {
                parser->consumed_args[i] = true;
            }

            if (!definition->expects_value) {
                if (assignment != NULL) {
                    cli_parser_set_error(parser, "Flag '--%.*s' does not accept a value.", (int)name_len, flag_body);
                    return false;
                }

                if (!cli_dispatch_flag(parser, definition, NULL)) {
                    return false;
                }
                continue;
            }

            if (assignment != NULL) {
                value = assignment + 1;
                if (value[0] == '\0') {
                    cli_parser_set_error(parser, "Flag '--%.*s' requires a value.", (int)name_len, flag_body);
                    return false;
                }
            } else {
                int value_index = cli_find_next_unconsumed(parser, i + 1);
                if (value_index < 0) {
                    cli_parser_set_error(parser, "Flag '--%.*s' requires a value.", (int)name_len, flag_body);
                    return false;
                }

                value = parser->argv[value_index];
                if (value == NULL || value[0] == '\0') {
                    cli_parser_set_error(parser, "Flag '--%.*s' requires a value.", (int)name_len, flag_body);
                    return false;
                }

                if (parser->consumed_args != NULL) {
                    parser->consumed_args[value_index] = true;
                }
            }

            if (!cli_dispatch_flag(parser, definition, value)) {
                return false;
            }

            continue;
        }

        char short_name = arg[1];
        const FlagDefinition* definition = cli_lookup_short_flag(parser, short_name);
        if (definition == NULL) {
            cli_parser_set_error(parser, "Unknown flag '-%c'.", short_name);
            return false;
        }

        if (parser->consumed_args != NULL) {
            parser->consumed_args[i] = true;
        }

        const char* inline_value = arg + 2;
        const char* value = NULL;

        if (!definition->expects_value) {
            if (inline_value[0] != '\0') {
                cli_parser_set_error(parser, "Flag '-%c' does not accept a value.", short_name);
                return false;
            }

            if (!cli_dispatch_flag(parser, definition, NULL)) {
                return false;
            }
            continue;
        }

        if (inline_value[0] != '\0') {
            if (inline_value[0] == '=') {
                inline_value++;
            }
            if (inline_value[0] == '\0') {
                cli_parser_set_error(parser, "Flag '-%c' requires a value.", short_name);
                return false;
            }
            value = inline_value;
        } else {
            int value_index = cli_find_next_unconsumed(parser, i + 1);
            if (value_index < 0) {
                cli_parser_set_error(parser, "Flag '-%c' requires a value.", short_name);
                return false;
            }

            value = parser->argv[value_index];
            if (value == NULL || value[0] == '\0') {
                cli_parser_set_error(parser, "Flag '-%c' requires a value.", short_name);
                return false;
            }

            if (parser->consumed_args != NULL) {
                parser->consumed_args[value_index] = true;
            }
        }

        if (!cli_dispatch_flag(parser, definition, value)) {
            return false;
        }
    }

    return !parser->has_error;
}

bool cli_parser_has_error(const CLIParser* parser) {
    if (parser == NULL) {
        return true;
    }

    return parser->has_error;
}

const char* cli_parser_get_error(const CLIParser* parser) {
    if (parser == NULL) {
        return "cli_parser: parser is NULL";
    }

    if (!parser->has_error || parser->error_message[0] == '\0') {
        return "";
    }

    return parser->error_message;
}

TracerConfig* cli_parser_get_config(CLIParser* parser) {
    if (parser == NULL) {
        return NULL;
    }

    return &parser->config;
}

const TracerConfig* cli_parser_get_config_const(const CLIParser* parser) {
    if (parser == NULL) {
        return NULL;
    }

    return &parser->config;
}

const FlagDefinition* cli_parser_get_flags(size_t* count) {
    if (count != NULL) {
        *count = kFlagRegistryCount;
    }

    return kFlagRegistry;
}

static bool cli_parse_spawn_mode_args(CLIParser* parser) {
    cli_reset_spawn_args(&parser->config);

    const int total_args = parser->argc;
    int* spawn_indices = NULL;
    if (total_args > 0) {
        spawn_indices = (int*)calloc((size_t)total_args, sizeof(int));
        if (spawn_indices == NULL) {
            cli_parser_set_error(parser, "Failed to allocate spawn index buffer.");
            return false;
        }
    }

    bool collect_all = false;
    bool have_executable = false;
    int spawn_count = 0;

    for (int i = parser->current_arg; i < total_args; ++i) {
        char* arg = parser->argv[i];
        if (arg == NULL || arg[0] == '\0') {
            continue;
        }

        if (strcmp(arg, "--") == 0) {
            if (parser->consumed_args != NULL) {
                parser->consumed_args[i] = true;
            }
            collect_all = true;
            continue;
        }

        if (!have_executable) {
            if (!collect_all) {
                int original_index = i;
                if (cli_skip_known_flag(parser, arg, &i)) {
                    continue;
                }

                if (arg[0] == '-') {
                    continue;
                }

                if (i != original_index) {
                    i = original_index;
                }
            }

            spawn_indices[spawn_count++] = i;
            have_executable = true;
            continue;
        }

        if (!collect_all) {
            int original_index = i;
            if (cli_skip_known_flag(parser, arg, &i)) {
                continue;
            }
            if (arg[0] == '-') {
                continue;
            }
            if (i != original_index) {
                i = original_index;
            }
        }

        spawn_indices[spawn_count++] = i;
    }

    if (!have_executable || spawn_count == 0) {
        free(spawn_indices);
        cli_parser_set_error(parser, "Spawn mode requires an executable argument.");
        return false;
    }

    char** spawn_args = (char**)calloc((size_t)spawn_count, sizeof(char*));
    if (spawn_args == NULL) {
        free(spawn_indices);
        cli_parser_set_error(parser, "Failed to allocate spawn argument list.");
        return false;
    }

    for (int i = 0; i < spawn_count; ++i) {
        int index = spawn_indices[i];
        spawn_args[i] = parser->argv[index];
        if (parser->consumed_args != NULL) {
            parser->consumed_args[index] = true;
        }
    }

    parser->config.spawn.argv = spawn_args;
    parser->config.spawn.argc = spawn_count;
    parser->config.spawn.executable = spawn_args[0];

    free(spawn_indices);
    return true;
}

static bool cli_parse_attach_mode_args(CLIParser* parser) {
    parser->config.attach.pid = 0;
    parser->config.attach.process_name = NULL;

    int pid_index = -1;
    int process_name_index = -1;
    bool collect_all = false;

    for (int i = parser->current_arg; i < parser->argc; ++i) {
        char* arg = parser->argv[i];
        if (arg == NULL || arg[0] == '\0') {
            continue;
        }

        if (strcmp(arg, "--") == 0) {
            if (parser->consumed_args != NULL) {
                parser->consumed_args[i] = true;
            }
            collect_all = true;
            continue;
        }

        if (pid_index == -1) {
            if (!collect_all) {
                int original_index = i;
                if (cli_skip_known_flag(parser, arg, &i)) {
                    continue;
                }
                if (arg[0] == '-') {
                    continue;
                }
                if (i != original_index) {
                    i = original_index;
                }
            }

            pid_index = i;
            continue;
        }

        if (process_name_index == -1) {
            if (!collect_all) {
                int original_index = i;
                if (cli_skip_known_flag(parser, arg, &i)) {
                    continue;
                }
                if (arg[0] == '-') {
                    continue;
                }
                if (i != original_index) {
                    i = original_index;
                }
            }

            process_name_index = i;
            continue;
        }
    }

    if (pid_index == -1) {
        cli_parser_set_error(parser, "Attach mode requires a PID argument.");
        return false;
    }

    const char* pid_value = parser->argv[pid_index];
    char* endptr = NULL;
    errno = 0;
    long parsed = strtol(pid_value, &endptr, 10);
    if (pid_value[0] == '\0' || endptr == pid_value || *endptr != '\0' || errno != 0 || parsed <= 0) {
        cli_parser_set_error(parser, "Invalid PID '%s'.", pid_value);
        return false;
    }

    parser->config.attach.pid = (pid_t)parsed;
    if (parser->consumed_args != NULL) {
        parser->consumed_args[pid_index] = true;
    }

    if (process_name_index != -1) {
        parser->config.attach.process_name = parser->argv[process_name_index];
        if (parser->consumed_args != NULL) {
            parser->consumed_args[process_name_index] = true;
        }
    }

    return true;
}

static void cli_reset_spawn_args(TracerConfig* config) {
    if (config == NULL) {
        return;
    }

    if (config->spawn.argv != NULL) {
        free(config->spawn.argv);
        config->spawn.argv = NULL;
    }

    config->spawn.executable = NULL;
    config->spawn.argc = 0;
}

static void cli_reset_triggers(TracerConfig* config) {
    if (config == NULL) {
        return;
    }

    if (config->triggers.entries != NULL) {
        for (size_t i = 0; i < config->triggers.count; ++i) {
            TriggerDefinition* entry = &config->triggers.entries[i];
            if (entry->raw_value != NULL) {
                free(entry->raw_value);
                entry->raw_value = NULL;
            }
            if (entry->symbol_name != NULL) {
                free(entry->symbol_name);
                entry->symbol_name = NULL;
            }
            if (entry->module_name != NULL) {
                free(entry->module_name);
                entry->module_name = NULL;
            }
        }

        free(config->triggers.entries);
        config->triggers.entries = NULL;
    }

    config->triggers.count = 0;
    config->triggers.capacity = 0;
}

static void cli_reset_filters(TracerConfig* config) {
    if (config == NULL) {
        return;
    }

    if (config->filters.modules != NULL) {
        for (size_t i = 0; i < config->filters.count; ++i) {
            free(config->filters.modules[i]);
            config->filters.modules[i] = NULL;
        }
        free(config->filters.modules);
        config->filters.modules = NULL;
    }

    config->filters.count = 0;
    config->filters.capacity = 0;
    config->filters.exclude_specified = false;
}

static const FlagDefinition* cli_lookup_long_flag(const CLIParser* parser, const char* name, size_t name_len) {
    if (parser == NULL || name == NULL || name_len == 0) {
        return NULL;
    }

    for (size_t i = 0; i < parser->flag_count; ++i) {
        const FlagDefinition* definition = &parser->flag_registry[i];
        if (definition->long_name == NULL) {
            continue;
        }

        size_t def_len = strlen(definition->long_name);
        if (def_len == name_len && strncmp(definition->long_name, name, name_len) == 0) {
            return definition;
        }
    }

    return NULL;
}

static const FlagDefinition* cli_lookup_short_flag(const CLIParser* parser, char short_name) {
    if (parser == NULL || short_name == '\0') {
        return NULL;
    }

    for (size_t i = 0; i < parser->flag_count; ++i) {
        const FlagDefinition* definition = &parser->flag_registry[i];
        if (definition->short_name == short_name) {
            return definition;
        }
    }

    return NULL;
}

static bool cli_skip_known_flag(const CLIParser* parser, const char* arg, int* index) {
    if (parser == NULL || arg == NULL || index == NULL) {
        return false;
    }

    if (arg[0] != '-' || arg[1] == '\0') {
        return false;
    }

    if (arg[1] == '-') {
        const char* body = arg + 2;
        if (*body == '\0') {
            return false;
        }

        const char* assignment = strchr(body, '=');
        size_t name_len = assignment != NULL ? (size_t)(assignment - body) : strlen(body);
        const FlagDefinition* definition = cli_lookup_long_flag(parser, body, name_len);
        if (definition == NULL) {
            return false;
        }

        if (definition->expects_value && assignment == NULL) {
            if (*index + 1 < parser->argc) {
                (*index)++;
            }
        }

        return true;
    }

    const FlagDefinition* definition = cli_lookup_short_flag(parser, arg[1]);
    if (definition == NULL) {
        return false;
    }

    if (definition->expects_value) {
        const char* inline_value = arg + 2;
        if (*inline_value == '\0' && *index + 1 < parser->argc) {
            (*index)++;
        }
    }

    return true;
}

static int cli_find_next_unconsumed(const CLIParser* parser, int start_index) {
    if (parser == NULL) {
        return -1;
    }

    for (int i = start_index; i < parser->argc; ++i) {
        if (parser->consumed_args != NULL && parser->consumed_args[i]) {
            continue;
        }

        if (parser->argv[i] == NULL || parser->argv[i][0] == '\0') {
            continue;
        }

        return i;
    }

    return -1;
}

static bool cli_dispatch_flag(CLIParser* parser, const FlagDefinition* definition, const char* value) {
    if (parser == NULL || definition == NULL) {
        return false;
    }

    const char* name = definition->long_name != NULL ? definition->long_name : "";

    if (strcmp(name, "output") == 0) {
        return handle_output_flag(parser, value);
    }
    if (strcmp(name, "duration") == 0) {
        return handle_duration_flag(parser, value);
    }
    if (strcmp(name, "stack-bytes") == 0) {
        return handle_stack_flag(parser, value);
    }
    if (strcmp(name, "help") == 0) {
        return handle_help_flag(parser);
    }
    if (strcmp(name, "version") == 0) {
        return handle_version_flag(parser);
    }
    if (strcmp(name, "trigger") == 0) {
        return handle_trigger_flag(parser, value);
    }
    if (strcmp(name, "pre-roll-sec") == 0) {
        return handle_pre_roll_flag(parser, value);
    }
    if (strcmp(name, "post-roll-sec") == 0) {
        return handle_post_roll_flag(parser, value);
    }
    if (strcmp(name, "exclude") == 0) {
        return handle_exclude_flag(parser, value);
    }

    // Recognised but unhandled flags fall through without additional actions.
    return true;
}

static bool handle_output_flag(CLIParser* parser, const char* value) {
    if (parser == NULL) {
        return false;
    }

    if (value == NULL || value[0] == '\0') {
        cli_parser_set_error(parser, "Flag '--output' requires a non-empty path.");
        return false;
    }

    parser->config.output.output_dir = value;
    parser->config.output.output_specified = true;
    return true;
}

static bool handle_duration_flag(CLIParser* parser, const char* value) {
    if (parser == NULL) {
        return false;
    }

    uint32_t parsed_value = 0;
    if (value == NULL || !cli_parse_u32(value, CLI_DURATION_MAX_SECONDS, &parsed_value)) {
        cli_parser_set_error(parser, "Invalid duration '%s'. Expected 0-%u seconds.", value != NULL ? value : "");
        return false;
    }

    parser->config.timing.duration_seconds = parsed_value;
    parser->config.timing.duration_specified = true;
    return true;
}

static bool handle_stack_flag(CLIParser* parser, const char* value) {
    if (parser == NULL) {
        return false;
    }

    uint32_t parsed_value = 0;
    if (value == NULL || !cli_parse_u32(value, CLI_STACK_MAX_BYTES, &parsed_value)) {
        cli_parser_set_error(parser, "Invalid stack byte count '%s'. Expected value between 0 and %u.", value != NULL ? value : "", CLI_STACK_MAX_BYTES);
        return false;
    }

    parser->config.capture.stack_bytes = parsed_value;
    parser->config.capture.stack_specified = true;
    return true;
}

static bool handle_help_flag(CLIParser* parser) {
    if (parser == NULL) {
        return false;
    }

    parser->config.help_requested = true;
    parser->config.mode = MODE_HELP;
    return true;
}

static bool handle_version_flag(CLIParser* parser) {
    if (parser == NULL) {
        return false;
    }

    parser->config.version_requested = true;
    parser->config.mode = MODE_VERSION;
    return true;
}

static bool handle_trigger_flag(CLIParser* parser, const char* value) {
    if (parser == NULL) {
        return false;
    }

    if (value == NULL || value[0] == '\0') {
        cli_parser_set_error(parser, "Flag '--trigger' requires a value.");
        return false;
    }

    if (strcmp(value, "crash") == 0) {
        char* raw_copy = cli_strdup(value);
        if (raw_copy == NULL) {
            cli_parser_set_error(parser, "Failed to allocate memory for trigger value.");
            return false;
        }

        if (!cli_append_trigger(parser, TRIGGER_TYPE_CRASH, raw_copy, NULL, NULL, 0)) {
            free(raw_copy);
            return false;
        }

        return true;
    }

    if (strncmp(value, "symbol=", 7) == 0) {
        const char* spec = value + 7;
        if (*spec == '\0') {
            cli_parser_set_error(parser, "Invalid trigger 'symbol=' requires a symbol name.");
            return false;
        }

        const char* symbol_start = spec;
        const char* module_start = NULL;
        size_t module_length = 0;

        const char* double_colon = strstr(spec, "::");
        if (double_colon != NULL) {
            module_start = spec;
            module_length = (size_t)(double_colon - spec);
            symbol_start = double_colon + 2;
        } else {
            const char* at_separator = strchr(spec, '@');
            const char* colon_separator = strchr(spec, ':');
            const char* separator = NULL;

            if (at_separator != NULL && colon_separator != NULL) {
                separator = at_separator < colon_separator ? at_separator : colon_separator;
            } else if (at_separator != NULL) {
                separator = at_separator;
            } else if (colon_separator != NULL) {
                separator = colon_separator;
            }

            if (separator != NULL) {
                module_start = spec;
                module_length = (size_t)(separator - spec);
                symbol_start = separator + 1;
            }
        }

        if (*symbol_start == '\0') {
            cli_parser_set_error(parser, "Invalid trigger 'symbol=' requires a non-empty symbol name.");
            return false;
        }

        char* raw_copy = cli_strdup(value);
        if (raw_copy == NULL) {
            cli_parser_set_error(parser, "Failed to allocate memory for trigger value.");
            return false;
        }

        char* symbol_copy = cli_strdup(symbol_start);
        if (symbol_copy == NULL) {
            free(raw_copy);
            cli_parser_set_error(parser, "Failed to allocate memory for symbol trigger.");
            return false;
        }

        char* module_copy = NULL;
        if (module_start != NULL && module_length > 0) {
            module_copy = cli_strndup(module_start, module_length);
            if (module_copy == NULL) {
                free(raw_copy);
                free(symbol_copy);
                cli_parser_set_error(parser, "Failed to allocate memory for trigger module name.");
                return false;
            }
        }

        if (!cli_append_trigger(parser, TRIGGER_TYPE_SYMBOL, raw_copy, symbol_copy, module_copy, 0)) {
            free(raw_copy);
            free(symbol_copy);
            free(module_copy);
            return false;
        }

        return true;
    }

    if (strncmp(value, "time=", 5) == 0) {
        const char* time_value = value + 5;
        if (*time_value == '\0') {
            cli_parser_set_error(parser, "Invalid trigger 'time=' requires a numeric value.");
            return false;
        }

        uint32_t seconds = 0;
        if (!cli_parse_u32(time_value, CLI_PERSISTENCE_MAX_SECONDS, &seconds)) {
            cli_parser_set_error(parser, "Invalid trigger time '%s'. Expected 0-%u seconds.", time_value, CLI_PERSISTENCE_MAX_SECONDS);
            return false;
        }

        char* raw_copy = cli_strdup(value);
        if (raw_copy == NULL) {
            cli_parser_set_error(parser, "Failed to allocate memory for trigger value.");
            return false;
        }

        if (!cli_append_trigger(parser, TRIGGER_TYPE_TIME, raw_copy, NULL, NULL, seconds)) {
            free(raw_copy);
            return false;
        }

        return true;
    }

    cli_parser_set_error(parser, "Invalid trigger '%s'. Expected 'symbol=<name>', 'crash', or 'time=<seconds>'.", value);
    return false;
}

static bool handle_pre_roll_flag(CLIParser* parser, const char* value) {
    if (parser == NULL) {
        return false;
    }

    uint32_t parsed_value = 0;
    if (value == NULL || !cli_parse_u32(value, CLI_PERSISTENCE_MAX_SECONDS, &parsed_value)) {
        cli_parser_set_error(parser, "Invalid pre-roll seconds '%s'. Expected 0-%u.", value != NULL ? value : "", CLI_PERSISTENCE_MAX_SECONDS);
        return false;
    }

    parser->config.timing.pre_roll_seconds = parsed_value;
    parser->config.timing.pre_roll_specified = true;
    return true;
}

static bool handle_post_roll_flag(CLIParser* parser, const char* value) {
    if (parser == NULL) {
        return false;
    }

    uint32_t parsed_value = 0;
    if (value == NULL || !cli_parse_u32(value, CLI_PERSISTENCE_MAX_SECONDS, &parsed_value)) {
        cli_parser_set_error(parser, "Invalid post-roll seconds '%s'. Expected 0-%u.", value != NULL ? value : "", CLI_PERSISTENCE_MAX_SECONDS);
        return false;
    }

    parser->config.timing.post_roll_seconds = parsed_value;
    parser->config.timing.post_roll_specified = true;
    return true;
}

static bool handle_exclude_flag(CLIParser* parser, const char* value) {
    if (parser == NULL) {
        return false;
    }

    if (value == NULL || value[0] == '\0') {
        cli_parser_set_error(parser, "Flag '--exclude' requires a value.");
        return false;
    }

    const char* cursor = value;
    while (*cursor != '\0') {
        const char* delimiter = strchr(cursor, ',');
        size_t segment_length = delimiter != NULL ? (size_t)(delimiter - cursor) : strlen(cursor);

        size_t offset = 0;
        size_t trimmed_length = segment_length;
        cli_trim_bounds(cursor, segment_length, &offset, &trimmed_length);

        if (trimmed_length == 0) {
            cli_parser_set_error(parser, "Invalid module exclusion in '%s'. Empty module name not allowed.", value);
            return false;
        }

        const char* module_start = cursor + offset;
        char* module_copy = cli_strndup(module_start, trimmed_length);
        if (module_copy == NULL) {
            cli_parser_set_error(parser, "Failed to allocate memory for exclusion list.");
            return false;
        }

        if (!cli_validate_module_name(module_copy)) {
            cli_parser_set_error(parser, "Invalid module name '%s' in exclusion list.", module_copy);
            free(module_copy);
            return false;
        }

        if (cli_module_exists(&parser->config, module_copy)) {
            // Duplicate modules are ignored silently to keep CLI forgiving.
            free(module_copy);
        } else if (!cli_append_filter_module(&parser->config, module_copy)) {
            cli_parser_set_error(parser, "Failed to record module exclusion.");
            free(module_copy);
            return false;
        }

        if (delimiter == NULL) {
            break;
        }
        cursor = delimiter + 1;
    }

    if (parser->config.filters.count > 0) {
        parser->config.filters.exclude_specified = true;
    }

    return true;
}

static bool cli_append_trigger(CLIParser* parser, TriggerType type, char* raw_value, char* symbol, char* module, uint32_t time_seconds) {
    if (parser == NULL || raw_value == NULL) {
        return false;
    }

    TriggerList* list = &parser->config.triggers;

    for (size_t i = 0; i < list->count; ++i) {
        TriggerDefinition* existing = &list->entries[i];
        if (existing->raw_value != NULL && strcmp(existing->raw_value, raw_value) == 0) {
            cli_parser_set_error(parser, "Duplicate trigger '%s'.", raw_value);
            return false;
        }
    }

    if (!cli_ensure_trigger_capacity(list, list->count + 1)) {
        cli_parser_set_error(parser, "Failed to allocate memory for trigger list.");
        return false;
    }

    TriggerDefinition* entry = &list->entries[list->count++];
    entry->type = type;
    entry->raw_value = raw_value;
    entry->symbol_name = symbol;
    entry->module_name = module;
    entry->time_seconds = time_seconds;

    return true;
}

static bool cli_ensure_trigger_capacity(TriggerList* list, size_t required) {
    if (list == NULL) {
        return false;
    }

    if (required <= list->capacity) {
        return true;
    }

    size_t new_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    TriggerDefinition* resized = (TriggerDefinition*)realloc(list->entries, new_capacity * sizeof(TriggerDefinition));
    if (resized == NULL) {
        return false;
    }

    // Initialize newly allocated slots to zero to avoid stale pointers on cleanup.
    size_t old_capacity = list->capacity;
    for (size_t i = old_capacity; i < new_capacity; ++i) {
        resized[i].type = TRIGGER_TYPE_INVALID;
        resized[i].raw_value = NULL;
        resized[i].symbol_name = NULL;
        resized[i].module_name = NULL;
        resized[i].time_seconds = 0;
    }

    list->entries = resized;
    list->capacity = new_capacity;
    return true;
}

static bool cli_parse_u32(const char* value, uint32_t max_value, uint32_t* out) {
    if (value == NULL || out == NULL) {
        return false;
    }

    char* endptr = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value, &endptr, 10);
    if (value[0] == '\0' || endptr == value || *endptr != '\0' || errno != 0) {
        return false;
    }

    if (parsed > max_value) {
        return false;
    }

    *out = (uint32_t)parsed;
    return true;
}

static bool cli_append_filter_module(TracerConfig* config, char* module_name) {
    if (config == NULL || module_name == NULL) {
        return false;
    }

    if (!cli_ensure_filter_capacity(config, config->filters.count + 1)) {
        return false;
    }

    config->filters.modules[config->filters.count++] = module_name;
    return true;
}

static bool cli_ensure_filter_capacity(TracerConfig* config, size_t required) {
    if (config == NULL) {
        return false;
    }

    if (required <= config->filters.capacity) {
        return true;
    }

    size_t new_capacity = config->filters.capacity == 0 ? 4 : config->filters.capacity * 2;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    char** resized = (char**)realloc(config->filters.modules, new_capacity * sizeof(char*));
    if (resized == NULL) {
        return false;
    }

    config->filters.modules = resized;
    config->filters.capacity = new_capacity;
    return true;
}

static bool cli_module_exists(const TracerConfig* config, const char* module_name) {
    if (config == NULL || module_name == NULL) {
        return false;
    }

    for (size_t i = 0; i < config->filters.count; ++i) {
        const char* existing = config->filters.modules[i];
        if (existing != NULL && strcmp(existing, module_name) == 0) {
            return true;
        }
    }

    return false;
}

static bool cli_validate_module_name(const char* module_name) {
    if (module_name == NULL || module_name[0] == '\0') {
        return false;
    }

    for (const char* ptr = module_name; *ptr != '\0'; ++ptr) {
        unsigned char ch = (unsigned char)*ptr;
        if (!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '/')) {
            return false;
        }
    }

    return true;
}

static void cli_trim_bounds(const char* start, size_t length, size_t* offset, size_t* trimmed_length) {
    if (start == NULL || offset == NULL || trimmed_length == NULL) {
        return;
    }

    size_t begin = 0;
    while (begin < length && isspace((unsigned char)start[begin])) {
        begin++;
    }

    size_t end = length;
    while (end > begin && isspace((unsigned char)start[end - 1])) {
        end--;
    }

    *offset = begin;
    *trimmed_length = end > begin ? (end - begin) : 0;
}

static char* cli_strdup(const char* source) {
    if (source == NULL) {
        return NULL;
    }

    size_t length = strlen(source);
    char* copy = (char*)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, source, length + 1);
    return copy;
}

static char* cli_strndup(const char* source, size_t length) {
    if (source == NULL) {
        return NULL;
    }

    char* copy = (char*)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, source, length);
    copy[length] = '\0';
    return copy;
}

static void cli_parser_clear_error(CLIParser* parser) {
    if (parser == NULL) {
        return;
    }

    parser->has_error = false;
    parser->error_message[0] = '\0';
}

static void cli_parser_set_error(CLIParser* parser, const char* fmt, ...) {
    if (parser == NULL) {
        return;
    }

    parser->has_error = true;

    if (fmt == NULL) {
        parser->error_message[0] = '\0';
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(parser->error_message, sizeof(parser->error_message), fmt, args);
    va_end(args);
}

static bool cli_arg_is_help(const char* arg) {
    if (arg == NULL) {
        return false;
    }

    return (strcmp(arg, "--help") == 0) || (strcmp(arg, "-h") == 0) || (strcmp(arg, "help") == 0);
}

static bool cli_arg_is_version(const char* arg) {
    if (arg == NULL) {
        return false;
    }

    return (strcmp(arg, "--version") == 0) || (strcmp(arg, "-v") == 0) || (strcmp(arg, "version") == 0);
}
