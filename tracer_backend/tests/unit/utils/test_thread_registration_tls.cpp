#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <unordered_set>

extern "C" {
#include <tracer_backend/utils/thread_registry.h>
#include <tracer_backend/ada/thread.h>
}

class RegistryFixture : public ::testing::Test {
protected:
    void* mem{nullptr};
    size_t size{0};
    ThreadRegistry* reg{nullptr};

    void SetUp() override {
        size = thread_registry_calculate_memory_size_with_capacity(MAX_THREADS);
        void* ptr = nullptr;
        int rc = posix_memalign(&ptr, 4096, size);
        ASSERT_EQ(rc, 0);
        ASSERT_NE(ptr, nullptr);
        mem = ptr;
        reg = thread_registry_init(mem, size);
        ASSERT_NE(reg, nullptr);
        ada_set_global_registry(reg);
    }

    void TearDown() override {
        ada_set_global_registry(nullptr);
        if (reg) thread_registry_deinit(reg);
        if (mem) free(mem);
    }
};

TEST(ADATLS, tls_state__uninitialized__then_all_fields_zero) {
    ada_reset_tls_state();
    auto* st = ada_get_tls_state();
    ASSERT_NE(st, nullptr);
    EXPECT_EQ(st->lanes, nullptr);
    EXPECT_EQ(st->reentrancy, 0u);
    EXPECT_EQ(st->call_depth, 0u);
    EXPECT_EQ(st->thread_id, 0u);
    EXPECT_FALSE(atomic_load(&st->registered));
}

TEST_F(RegistryFixture, ada_get_thread_lane__triggers_registration_and_caches) {
    ada_reset_tls_state();
    ThreadLaneSet* first = ada_get_thread_lane();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(ada_get_tls_state()->lanes, first);
    ThreadLaneSet* second = ada_get_thread_lane();
    EXPECT_EQ(second, first);
}

TEST_F(RegistryFixture, thread_registry__concurrent_registration__unique_pointers) {
    const int N = 16;
    std::vector<std::thread> ths;
    std::vector<ThreadLaneSet*> ptrs(N, nullptr);
    for (int i = 0; i < N; ++i) {
        ths.emplace_back([i, &ptrs]() {
            ada_reset_tls_state();
            ThreadLaneSet* lanes = ada_get_thread_lane();
            ASSERT_NE(lanes, nullptr);
            ptrs[i] = lanes;
        });
    }
    for (auto& t : ths) t.join();
    std::unordered_set<ThreadLaneSet*> set;
    for (auto* p : ptrs) {
        ASSERT_NE(p, nullptr);
        set.insert(p);
    }
    EXPECT_EQ((int)set.size(), N);
}

TEST(ADATLS, reentrancy_guard__enter_exit__tracks_depth) {
    ada_reset_tls_state();
    auto g1 = ada_enter_trace();
    EXPECT_FALSE(g1.was_reentrant);
    EXPECT_EQ(ada_get_tls_state()->call_depth, g1.prev_depth + 1);
    auto g2 = ada_enter_trace();
    EXPECT_TRUE(g2.was_reentrant);
    ada_exit_trace(g2);
    ada_exit_trace(g1);
    EXPECT_EQ(ada_get_tls_state()->call_depth, 0u);
}
