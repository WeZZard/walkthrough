#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

extern "C" {
#include "tracer_backend/docs/doc_builder.h"
#include "tracer_backend/docs/example_runner.h"
#include "tracer_backend/docs/platform_check.h"
#include "tracer_backend/docs/troubleshoot.h"
}

namespace {

std::filesystem::path create_temp_dir(const std::string &prefix) {
    auto base = std::filesystem::temp_directory_path();
    for (int i = 0; i < 10; ++i) {
        auto candidate = base / (prefix + std::to_string(std::rand()) + std::to_string(i));
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec)) {
            return candidate;
        }
    }
    throw std::runtime_error("could not allocate temporary directory");
}

} // namespace

TEST(docs_integration__generate_getting_started__produces_complete_artifact, end_to_end) {
    auto *builder = tracer_doc_builder_create();
    ASSERT_NE(builder, nullptr);
    auto *runner = tracer_example_runner_create();
    ASSERT_NE(runner, nullptr);

    char document[8192];
    size_t written = 0;
    auto status = tracer_doc_builder_generate_getting_started(builder, ".", document, sizeof(document), &written);
    ASSERT_EQ(status, TRACER_DOCS_STATUS_OK);
    ASSERT_GT(written, 0U);

    std::string doc_string(document);
    EXPECT_NE(doc_string.find("Quick Reference"), std::string::npos);
    EXPECT_NE(doc_string.find("Example Workflow"), std::string::npos);

    auto workspace = create_temp_dir("docs-integration-");
    auto doc_path = workspace / "getting_started.md";

    {
        std::ofstream out(doc_path);
        out << doc_string;
    }
    EXPECT_TRUE(std::filesystem::exists(doc_path));

    auto c_example_path = workspace / "run.c";
    {
        std::ofstream source(c_example_path);
        source << "#include <stdio.h>\n";
        source << "int main(void) { puts(\"integration-docs\"); return 0; }\n";
    }

    char stdout_buffer[256];
    tracer_example_result_t runner_result{};
    status = tracer_example_runner_execute_and_verify(
        runner,
        c_example_path.c_str(),
        "integration-docs",
        stdout_buffer,
        sizeof(stdout_buffer),
        &runner_result
    );

    EXPECT_EQ(status, TRACER_DOCS_STATUS_OK) << stdout_buffer;
    EXPECT_TRUE(runner_result.stdout_matched);

    tracer_troubleshoot_report_t report{};
    status = tracer_troubleshoot_generate_report(&report);
    EXPECT_EQ(status, TRACER_DOCS_STATUS_OK);

    char troubleshoot_buffer[512];
    size_t troubleshoot_written = 0;
    status = tracer_troubleshoot_render_report(&report, troubleshoot_buffer, sizeof(troubleshoot_buffer), &troubleshoot_written);
    EXPECT_EQ(status, TRACER_DOCS_STATUS_OK);
    EXPECT_NE(std::string(troubleshoot_buffer).find("Actionable steps"), std::string::npos);

    tracer_platform_status_t platform{};
    tracer_platform_snapshot(&platform);
    char summary[256];
    size_t summary_written = 0;
    status = tracer_platform_render_summary(&platform, summary, sizeof(summary), &summary_written);
    EXPECT_EQ(status, TRACER_DOCS_STATUS_OK);
    EXPECT_GT(summary_written, 0U);

    std::filesystem::remove_all(workspace);

    tracer_example_runner_destroy(runner);
    tracer_doc_builder_destroy(builder);
}
