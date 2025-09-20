#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

extern "C" {
#include <tracer_backend/timer/timer.h>
}

static std::atomic<int> g_shutdown_requests{0};

extern "C" void shutdown_initiate(void) {
    g_shutdown_requests.fetch_add(1, std::memory_order_relaxed);
}

class TimerIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(0, timer_init());
        g_shutdown_requests.store(0, std::memory_order_relaxed);
    }

    void TearDown() override {
        timer_cleanup();
    }
};

TEST_F(TimerIntegrationTest, timer_integration__start_and_expire__then_shutdown_triggered_once) {
    ASSERT_EQ(0, timer_start(180));

    std::vector<uint64_t> samples;
    std::thread sampler([&]() {
        while (timer_is_active()) {
            samples.push_back(timer_remaining_ms());
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    sampler.join();

    EXPECT_FALSE(timer_is_active());
    EXPECT_EQ(1, g_shutdown_requests.load(std::memory_order_relaxed));

    ASSERT_FALSE(samples.empty());
    for (size_t i = 1; i < samples.size(); ++i) {
        EXPECT_LE(samples[i], samples[i - 1]);
    }
}

TEST_F(TimerIntegrationTest, timer_integration__cancel_from_worker__then_shutdown_not_called) {
    ASSERT_EQ(0, timer_start(400));

    std::thread canceller([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        timer_cancel();
    });

    for (int i = 0; i < 60 && timer_is_active(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    canceller.join();

    EXPECT_FALSE(timer_is_active());
    EXPECT_EQ(0u, timer_remaining_ms());
    EXPECT_EQ(0, g_shutdown_requests.load(std::memory_order_relaxed));

    ASSERT_EQ(0, timer_start(120));
    EXPECT_EQ(0, timer_cancel());
    for (int i = 0; i < 40 && timer_is_active(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(0, g_shutdown_requests.load(std::memory_order_relaxed));
}
