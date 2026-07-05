#include "doctest.h"

#include "ingot.h"

TEST_CASE("construct/destroy") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 10);
    REQUIRE_MESSAGE(v.data != nullptr, "data should not be null");
    CHECK_MESSAGE(sv_count(v) == 0, "count should be 0");
    CHECK_MESSAGE(sv_capacity(v) == 10, "capacity should be 10");
    CHECK_MESSAGE(v.alloc == &heap, "alloc should be heap");

    ingot::sv_destroy(v);
    CHECK_MESSAGE(v.data == nullptr, "data should be null after destroy");
    CHECK_MESSAGE(v.count == 0, "count should be 0 after destroy");
    CHECK_MESSAGE(v.capacity == 0, "capacity should be 0 after destroy");
    CHECK_MESSAGE(v.alloc == nullptr, "alloc should be null after destroy");
}

TEST_CASE("double destroy is no-op") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 5);
    ingot::sv_destroy(v);
    ingot::sv_destroy(v); /* should not crash */
    CHECK(v.data == nullptr);
}

TEST_CASE("push/pop") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 5);

    ingot::sv_push(v, 10);
    CHECK_MESSAGE(sv_count(v) == 1, "count after push");
    CHECK_MESSAGE(v[0] == 10, "value after push");

    ingot::sv_push(v, 20);
    ingot::sv_push(v, 30);
    CHECK_MESSAGE(sv_count(v) == 3, "count after 3 pushes");
    CHECK_MESSAGE(v[0] == 10, "v[0]");
    CHECK_MESSAGE(v[1] == 20, "v[1]");
    CHECK_MESSAGE(v[2] == 30, "v[2]");

    ingot::sv_pop(v);
    CHECK_MESSAGE(sv_count(v) == 2, "count after pop");

    ingot::sv_pop(v);
    ingot::sv_pop(v);
    CHECK_MESSAGE(sv_count(v) == 0, "count after all popped");

    ingot::sv_destroy(v);
}

TEST_CASE("clear") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 10);
    ingot::sv_push(v, 1);
    ingot::sv_push(v, 2);
    ingot::sv_push(v, 3);

    ingot::sv_clear(v);
    CHECK_MESSAGE(sv_count(v) == 0, "count after clear");
    CHECK_MESSAGE(sv_capacity(v) == 10, "capacity unchanged");
    CHECK_MESSAGE(v.data != nullptr, "data still valid");

    ingot::sv_destroy(v);
}

TEST_CASE("empty/full") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 3);
    CHECK_MESSAGE(sv_empty(v), "should be empty initially");
    CHECK_MESSAGE(!sv_full(v), "should not be full initially");

    ingot::sv_push(v, 1);
    CHECK_MESSAGE(!sv_empty(v), "not empty after push");

    ingot::sv_push(v, 2);
    ingot::sv_push(v, 3);
    CHECK_MESSAGE(sv_full(v), "should be full after 3 pushes");

    ingot::sv_destroy(v);
}

TEST_CASE("iteration (range-for)") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 5);
    ingot::sv_push(v, 10);
    ingot::sv_push(v, 20);
    ingot::sv_push(v, 30);

    int expected = 10;
    int count = 0;
    for (int x : v) {
        CHECK_MESSAGE(x == expected, "iter value mismatch");
        expected += 10;
        count++;
    }
    CHECK_MESSAGE(count == 3, "iter should visit 3 elements");

    ingot::sv_destroy(v);
}

TEST_CASE("arena allocator") {
    alignas(alignof(std::max_align_t)) char buffer[1024];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    ingot::static_vector_t<int> v;
    ingot::sv_create(v, arena, 64);

    REQUIRE_MESSAGE(v.data != nullptr, "arena allocation should succeed");

    for (int i = 0; i < 10; ++i) {
        ingot::sv_push(v, i * i);
    }
    CHECK(v[5] == 25);

    ingot::sv_destroy(v);
    arena.reset();
    arena.destroy();
}

TEST_CASE("arena resize") {
    alignas(alignof(std::max_align_t)) char buffer[1024];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    void* p = arena.alloc(32, 8);
    REQUIRE_MESSAGE(p != nullptr, "initial alloc should succeed");

    bool ok = arena.resize(p, 32, 64, 8);
    CHECK_MESSAGE(ok, "resize of last alloc should succeed");

    arena.free(p, 64);
    arena.reset();
    arena.destroy();
}

TEST_CASE("arena resize (not last)") {
    alignas(alignof(std::max_align_t)) char buffer[1024];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    void* p1 = arena.alloc(32, 8);
    void* p2 = arena.alloc(32, 8);
    REQUIRE_MESSAGE(p1 != nullptr, "first alloc should succeed");
    REQUIRE_MESSAGE(p2 != nullptr, "second alloc should succeed");

    bool ok = arena.resize(p1, 32, 64, 8);
    CHECK_MESSAGE(!ok, "resize of non-last alloc should fail");

    arena.reset();
    arena.destroy();
}

TEST_CASE("heap resize always false") {
    ingot::heap_allocator_t heap;

    void* p = heap.alloc(32, 8);
    REQUIRE_MESSAGE(p != nullptr, "heap alloc should succeed");

    bool ok = heap.resize(p, 32, 64, 8);
    CHECK_MESSAGE(!ok, "heap resize should return false");

    heap.free(p, 32);
}

TEST_CASE("POD type constraint") {
    static_assert(std::is_trivially_copyable_v<ingot::static_vector_t<int>>,
                  "static_vector_t should be trivially copyable");
    static_assert(std::is_standard_layout_v<ingot::static_vector_t<int>>,
                  "static_vector_t should be standard layout");
}
