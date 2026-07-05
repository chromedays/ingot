#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ingot.h"

TEST_CASE("construct/destroy") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 10);
    CHECK_MESSAGE(v.data != nullptr, "data should not be null");
    CHECK(sv_count(v) == 0);
    CHECK(sv_capacity(v) == 10);
    CHECK(v.alloc == &heap);

    ingot::sv_destroy(v);
    CHECK(v.data == nullptr);
    CHECK(v.count == 0);
    CHECK(v.capacity == 0);
    CHECK(v.alloc == nullptr);
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
    CHECK(sv_count(v) == 1);
    CHECK(v[0] == 10);

    ingot::sv_push(v, 20);
    ingot::sv_push(v, 30);
    CHECK(sv_count(v) == 3);
    CHECK(v[0] == 10);
    CHECK(v[1] == 20);
    CHECK(v[2] == 30);

    ingot::sv_pop(v);
    CHECK(sv_count(v) == 2);

    ingot::sv_pop(v);
    ingot::sv_pop(v);
    CHECK(sv_count(v) == 0);

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
    CHECK(sv_count(v) == 0);
    CHECK(sv_capacity(v) == 10);
    CHECK(v.data != nullptr);

    ingot::sv_destroy(v);
}

TEST_CASE("empty/full") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;

    ingot::sv_create(v, heap, 3);
    CHECK(sv_empty(v));
    CHECK(!sv_full(v));

    ingot::sv_push(v, 1);
    CHECK(!sv_empty(v));

    ingot::sv_push(v, 2);
    ingot::sv_push(v, 3);
    CHECK(sv_full(v));

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
        CHECK(x == expected);
        expected += 10;
        count++;
    }
    CHECK(count == 3);

    ingot::sv_destroy(v);
}

TEST_CASE("arena allocator") {
    alignas(alignof(std::max_align_t)) char buffer[1024];

    ingot::arena_allocator_t arena;
    arena.construct(buffer, sizeof(buffer));

    ingot::static_vector_t<int> v;
    ingot::sv_create(v, arena, 64);

    CHECK_MESSAGE(v.data != nullptr, "arena allocation should succeed");

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
    CHECK(ok);

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
    CHECK(!ok);

    arena.reset();
    arena.destroy();
}

TEST_CASE("heap resize always false") {
    ingot::heap_allocator_t heap;

    void* p = heap.alloc(32, 8);
    REQUIRE_MESSAGE(p != nullptr, "heap alloc should succeed");

    bool ok = heap.resize(p, 32, 64, 8);
    CHECK(!ok);

    heap.free(p, 32);
}

TEST_CASE("POD type constraint") {
    static_assert(std::is_trivially_copyable_v<ingot::static_vector_t<int>>,
                  "static_vector_t should be trivially copyable");
    static_assert(std::is_standard_layout_v<ingot::static_vector_t<int>>,
                  "static_vector_t should be standard layout");
}
