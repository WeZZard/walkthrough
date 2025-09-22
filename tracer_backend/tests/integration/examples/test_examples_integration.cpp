#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::filesystem::path workspace_root() {
    const char *env = std::getenv("ADA_WORKSPACE_ROOT");
    if (env && env[0] != '\0') {
        return std::filesystem::path(env);
    }
    return std::filesystem::current_path();
}

std::string shell_quote(const std::string &value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    for (char c : value) {
        if (c == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

std::filesystem::path make_temp_dir(const std::string &prefix) {
    auto base = std::filesystem::temp_directory_path();
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto candidate = base / (prefix + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(attempt));
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec)) {
            return candidate;
        }
    }
    throw std::runtime_error("failed to allocate temporary directory");
}

int run_in_directory(const std::string &command, const std::filesystem::path &directory) {
    auto previous = std::filesystem::current_path();
    std::error_code ec;
    std::filesystem::current_path(directory, ec);
    if (ec) {
        throw std::runtime_error("failed to change directory: " + ec.message());
    }
    int status = std::system(command.c_str());
    std::filesystem::current_path(previous);
    return status;
}

std::string read_file(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::vector<std::string> split_lines(const std::string &content) {
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

} // namespace

TEST(examples__thread_trace__spawn_workers_and_log_iterations, integration) {
    // M1_E5_I4_TECH_DESIGN
    auto workspace = workspace_root();
    auto source = workspace / "tracer_backend/examples/intermediate/thread_trace.c";
    ASSERT_TRUE(std::filesystem::exists(source)) << "missing source: " << source;

    auto temp = make_temp_dir("thread-trace-example-");
    std::string compile_cmd = "gcc -Wall -Wextra -Werror -std=c11 -pthread -o thread_trace " + shell_quote(source.string());
    ASSERT_EQ(run_in_directory(compile_cmd, temp), 0) << "failed to compile thread_trace";
    ASSERT_TRUE(std::filesystem::exists(temp / "thread_trace"));

    std::string run_cmd = "./thread_trace > output.txt";
    ASSERT_EQ(run_in_directory(run_cmd, temp), 0) << "thread_trace execution failed";

    auto output = read_file(temp / "output.txt");
    auto lines = split_lines(output);

    ASSERT_FALSE(lines.empty());
    EXPECT_NE(std::string::npos, output.find("Thread tracing demo"));
    EXPECT_NE(std::string::npos, output.find("All workers"));

    std::map<int, int> iteration_counts;
    for (const auto &line : lines) {
        int worker_id = -1;
        int iteration = -1;
        if (std::sscanf(line.c_str(), "[worker %d] iteration %d", &worker_id, &iteration) == 2) {
            ++iteration_counts[worker_id];
            EXPECT_GE(iteration, 1);
            EXPECT_LE(iteration, 5);
        }
    }

    ASSERT_EQ(iteration_counts.size(), 4U);
    for (int worker = 0; worker < 4; ++worker) {
        auto it = iteration_counts.find(worker);
        ASSERT_NE(it, iteration_counts.end()) << "missing logs for worker " << worker;
        EXPECT_EQ(it->second, 5);
    }
}

TEST(examples__perf_profile__contrast_algorithms, integration) {
    // M1_E5_I4_TECH_DESIGN
    auto workspace = workspace_root();
    auto source = workspace / "tracer_backend/examples/intermediate/perf_profile.c";
    ASSERT_TRUE(std::filesystem::exists(source)) << "missing source: " << source;

    auto temp = make_temp_dir("perf-profile-example-");
    std::string compile_cmd = "gcc -Wall -Wextra -Werror -std=c11 -O2 -o perf_profile " + shell_quote(source.string());
    ASSERT_EQ(run_in_directory(compile_cmd, temp), 0) << "failed to compile perf_profile";
    ASSERT_TRUE(std::filesystem::exists(temp / "perf_profile"));

    std::string run_cmd = "./perf_profile > output.txt";
    ASSERT_EQ(run_in_directory(run_cmd, temp), 0) << "perf_profile execution failed";

    auto output = read_file(temp / "output.txt");
    EXPECT_NE(std::string::npos, output.find("Performance profiling example"));
    EXPECT_NE(std::string::npos, output.find("Bubble sort"));
    EXPECT_NE(std::string::npos, output.find("Quicksort"));
    EXPECT_NE(std::string::npos, output.find("Arrays sorted"));

    double bubble_us = 0.0;
    double quick_us = 0.0;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        double value = 0.0;
        if (std::sscanf(line.c_str(), "Bubble sort average: %lf", &value) == 1) {
            bubble_us = value;
        }
        if (std::sscanf(line.c_str(), "Quicksort average: %lf", &value) == 1) {
            quick_us = value;
        }
    }

    EXPECT_GT(bubble_us, 0.0);
    EXPECT_GT(quick_us, 0.0);
    EXPECT_GT(bubble_us, quick_us);
}

TEST(examples__memory_debug__surfacing_issues, integration) {
    // M1_E5_I4_TECH_DESIGN
    auto workspace = workspace_root();
    auto source = workspace / "tracer_backend/examples/advanced/memory_debug.c";
    ASSERT_TRUE(std::filesystem::exists(source)) << "missing source: " << source;

    auto temp = make_temp_dir("memory-debug-example-");
    std::string compile_cmd = "gcc -Wall -Wextra -Werror -std=c11 -o memory_debug " + shell_quote(source.string());
    ASSERT_EQ(run_in_directory(compile_cmd, temp), 0) << "failed to compile memory_debug";
    ASSERT_TRUE(std::filesystem::exists(temp / "memory_debug"));

    std::string run_cmd = "./memory_debug > output.txt";
    ASSERT_EQ(run_in_directory(run_cmd, temp), 0) << "memory_debug execution failed";

    auto output = read_file(temp / "output.txt");
    EXPECT_NE(std::string::npos, output.find("Memory debugging example"));
    EXPECT_NE(std::string::npos, output.find("leaky_function"));
    EXPECT_NE(std::string::npos, output.find("use_after_free"));
    EXPECT_NE(std::string::npos, output.find("linked list"));
    EXPECT_NE(std::string::npos, output.find("double_free"));
}

TEST(examples__signal_trace__handles_signals_and_cleans_up, integration) {
    // M1_E5_I4_TECH_DESIGN
    auto workspace = workspace_root();
    auto source = workspace / "tracer_backend/examples/advanced/signal_trace.c";
    ASSERT_TRUE(std::filesystem::exists(source)) << "missing source: " << source;

    auto temp = make_temp_dir("signal-trace-example-");
    std::string compile_cmd = "gcc -Wall -Wextra -Werror -std=c11 -o signal_trace " + shell_quote(source.string());
    ASSERT_EQ(run_in_directory(compile_cmd, temp), 0) << "failed to compile signal_trace";
    ASSERT_TRUE(std::filesystem::exists(temp / "signal_trace"));

    std::string run_script;
    run_script += "./signal_trace > output.txt &\n";
    run_script += "pid=$!\n";
    run_script += "sleep 1\n";
    run_script += "kill -INT $pid\n";
    run_script += "sleep 1\n";
    run_script += "kill -INT $pid\n";
    run_script += "sleep 1\n";
    run_script += "kill -TERM $pid\n";
    run_script += "wait $pid\n";

    std::string run_cmd = "bash -c " + shell_quote(run_script);
    ASSERT_EQ(run_in_directory(run_cmd, temp), 0) << "signal_trace execution failed";

    auto output = read_file(temp / "output.txt");
    EXPECT_NE(std::string::npos, output.find("Signal tracing example"));
    EXPECT_NE(std::string::npos, output.find("SIGINT"));
    EXPECT_NE(std::string::npos, output.find("SIGTERM"));
    EXPECT_NE(std::string::npos, output.find("cleanup"));
}

