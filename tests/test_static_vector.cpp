#include "ingot.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int tests_passed = 0;
static int tests_failed = 0;

#define test_assert_(cond, ...) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "FAIL: " __VA_ARGS__); \
            std::fprintf(stderr, "\n  at %s:%d\n", __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while (0)

#define test_run_(name) \
    do { \
        std::printf("  %s ... ", name); \
    } while (0)

#define test_done_() \
    do { \
        tests_passed++; \
        std::printf("ok\n"); \
    } while (0)

static void test_construct_destroy() {
    test_run_("construct/destroy");
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 10);
    test_assert_(v.data != nullptr, "data should not be null");
    test_assert_(sv_count(v) == 0, "count should be 0");
    test_assert_(sv_capacity(v) == 10, "capacity should be 10");
    test_assert_(v.alloc == &heap, "alloc should be heap");

    ingot::sv_destroy(v);
    test_assert_(v.data == nullptr, "data should be null after destroy");
    test_assert_(v.count == 0, "count should be 0 after destroy");
    test_assert_(v.capacity == 0, "capacity should be 0 after destroy");
    test_assert_(v.alloc == nullptr, "alloc should be null after destroy");
    test_done_();
}

static void test_double_destroy_is_noop() {
    test_run_("double destroy is no-op");
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 5);
    ingot::sv_destroy(v);
    ingot::sv_destroy(v); /* should not crash */
    test_assert_(v.data == nullptr, "data should stay null");
    test_done_();
}

static void test_push_pop() {
    test_run_("push/pop");
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 5);

    ingot::sv_push(v, 10);
    test_assert_(sv_count(v) == 1, "count after push");
    test_assert_(v[0] == 10, "value after push");

    ingot::sv_push(v, 20);
    ingot::sv_push(v, 30);
    test_assert_(sv_count(v) == 3, "count after 3 pushes");
    test_assert_(v[0] == 10, "v[0]");
    test_assert_(v[1] == 20, "v[1]");
    test_assert_(v[2] == 30, "v[2]");

    ingot::sv_pop(v);
    test_assert_(sv_count(v) == 2, "count after pop");

    ingot::sv_pop(v);
    ingot::sv_pop(v);
    test_assert_(sv_count(v) == 0, "count after all popped");

    ingot::sv_destroy(v);
    test_done_();
}

static void test_clear() {
    test_run_("clear");
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 10);
    ingot::sv_push(v, 1);
    ingot::sv_push(v, 2);
    ingot::sv_push(v, 3);

    ingot::sv_clear(v);
    test_assert_(sv_count(v) == 0, "count after clear");
    test_assert_(sv_capacity(v) == 10, "capacity unchanged");
    test_assert_(v.data != nullptr, "data still valid");

    ingot::sv_destroy(v);
    test_done_();
}

static void test_empty_full() {
    test_run_("empty/full");
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 3);
    test_assert_(sv_empty(v), "should be empty initially");
    test_assert_(!sv_full(v), "should not be full initially");

    ingot::sv_push(v, 1);
    test_assert_(!sv_empty(v), "not empty after push");

    ingot::sv_push(v, 2);
    ingot::sv_push(v, 3);
    test_assert_(sv_full(v), "should be full after 3 pushes");

    ingot::sv_destroy(v);
    test_done_();
}

static void test_iteration() {
    test_run_("iteration (range-for)");
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 5);
    ingot::sv_push(v, 10);
    ingot::sv_push(v, 20);
    ingot::sv_push(v, 30);

    int expected = 10;
    int count = 0;
    for (int x : v) {
        test_assert_(x == expected, "iter value mismatch");
        expected += 10;
        count++;
    }
    test_assert_(count == 3, "iter should visit 3 elements");

    ingot::sv_destroy(v);
    test_done_();
}

static void test_arena_allocator() {
    test_run_("arena allocator");
    alignas(alignof(std::max_align_t)) char buffer[1024];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    ingot::static_vector_t<int> v;
    ingot::sv_create(v, arena, 64);

    test_assert_(v.data != nullptr, "arena allocation should succeed");

    for (int i = 0; i < 10; ++i) {
        ingot::sv_push(v, i * i);
    }
    test_assert_(v[5] == 25, "v[5] should be 25");

    ingot::sv_destroy(v);
    arena.reset();
    arena.destroy();
    test_done_();
}

static void test_arena_resize() {
    test_run_("arena resize");
    alignas(alignof(std::max_align_t)) char buffer[1024];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    void* p = arena.alloc(32, 8);
    test_assert_(p != nullptr, "initial alloc should succeed");

    bool ok = arena.resize(p, 32, 64, 8);
    test_assert_(ok, "resize of last alloc should succeed");

    arena.free(p, 64);
    arena.reset();
    arena.destroy();
    test_done_();
}

static void test_arena_resize_not_last_fails() {
    test_run_("arena resize (not last)");
    alignas(alignof(std::max_align_t)) char buffer[1024];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    void* p1 = arena.alloc(32, 8);
    void* p2 = arena.alloc(32, 8);
    test_assert_(p1 != nullptr && p2 != nullptr, "both allocs should succeed");

    bool ok = arena.resize(p1, 32, 64, 8);
    test_assert_(!ok, "resize of non-last alloc should fail");

    arena.reset();
    arena.destroy();
    test_done_();
}

static void test_heap_resize_always_fails() {
    test_run_("heap resize always false");
    ingot::heap_allocator_t heap;

    void* p = heap.alloc(32, 8);
    test_assert_(p != nullptr, "heap alloc should succeed");

    bool ok = heap.resize(p, 32, 64, 8);
    test_assert_(!ok, "heap resize should return false");

    heap.free(p, 32);
    test_done_();
}

static void test_pod_constraint() {
    test_run_("POD type constraint");
    static_assert(std::is_trivially_copyable_v<ingot::static_vector_t<int>>,
                  "static_vector_t should be trivially copyable");
    static_assert(std::is_standard_layout_v<ingot::static_vector_t<int>>,
                  "static_vector_t should be standard layout");
    test_done_();
}

int main() {
    std::printf("test_static_vector:\n");

    test_construct_destroy();
    test_double_destroy_is_noop();
    test_push_pop();
    test_clear();
    test_empty_full();
    test_iteration();
    test_arena_allocator();
    test_arena_resize();
    test_arena_resize_not_last_fails();
    test_heap_resize_always_fails();
    test_pod_constraint();

    std::printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
