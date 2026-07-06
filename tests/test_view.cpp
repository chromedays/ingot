#include "doctest.h"

#include "ingot.h"

TEST_CASE("view types smoke") {
    CHECK(true);
}

TEST_CASE("view_t: view_from ptr+len") {
    int buf[] = {10, 20, 30};
    ingot::view_t<int> v = ingot::view_from(buf, 3);
    CHECK_MESSAGE(ingot::view_len(v) == 3, "len should be 3");
    CHECK_MESSAGE(ingot::view_data(v) == buf, "data should point to source");
    CHECK_MESSAGE(!ingot::view_is_empty(v), "should not be empty");
}

TEST_CASE("view_t: view_from empty") {
    ingot::view_t<int> v = ingot::view_from(static_cast<int*>(nullptr), 0);
    CHECK_MESSAGE(ingot::view_is_empty(v), "empty view");
    CHECK_MESSAGE(ingot::view_len(v) == 0, "len 0");
}

TEST_CASE("view_t: view_from C array") {
    int arr[] = {1, 2, 3, 4};
    ingot::view_t<int> v = ingot::view_from(arr);
    CHECK_MESSAGE(ingot::view_len(v) == 4, "C array len 4");
    CHECK_MESSAGE(ingot::view_data(v) == arr, "points to array");
}

TEST_CASE("view_t: view_from static_vector mutable") {
    ingot::heap_allocator_t alloc;
    ingot::static_vector_t<int> sv;
    ingot::sv_create(sv, alloc, 4);
    ingot::sv_push(sv, 1);
    ingot::sv_push(sv, 2);
    ingot::sv_push(sv, 3);

    ingot::view_t<int> v = ingot::view_from(sv);
    CHECK_MESSAGE(ingot::view_len(v) == 3, "len matches sv count");
    CHECK_MESSAGE(ingot::view_data(v) == sv.data, "data points to sv buffer");

    // mutable: 요소 수정이 원본에 반영
    ingot::view_data(v)[0] = 99;
    CHECK_MESSAGE(sv.data[0] == 99, "mutation visible through source");

    ingot::sv_destroy(sv);
}

TEST_CASE("view_t: view_from static_vector const") {
    ingot::heap_allocator_t alloc;
    ingot::static_vector_t<int> sv;
    ingot::sv_create(sv, alloc, 2);
    ingot::sv_push(sv, 7);

    const ingot::static_vector_t<int>& csv = sv;
    ingot::view_t<const int> cv = ingot::view_from(csv);
    CHECK_MESSAGE(ingot::view_len(cv) == 1, "const view len");
    CHECK_MESSAGE(*ingot::view_data(cv) == 7, "const view reads element");

    ingot::sv_destroy(sv);
}

TEST_CASE("view_t: view_at") {
    int buf[] = {100, 200, 300};
    ingot::view_t<int> v = ingot::view_from(buf, 3);

    CHECK_MESSAGE(ingot::view_at(v, 0) == 100, "at 0");
    CHECK_MESSAGE(ingot::view_at(v, 2) == 300, "at last index");

    // mutable: 참조 반환으로 수정
    ingot::view_at(v, 1) = 250;
    CHECK_MESSAGE(buf[1] == 250, "view_at mutation visible in source");
}

TEST_CASE("view_t: view_at const view") {
    const int buf[] = {5, 10};
    ingot::view_t<const int> cv = ingot::view_from(buf, 2);
    CHECK_MESSAGE(ingot::view_at(cv, 0) == 5, "const view read at 0");
    CHECK_MESSAGE(ingot::view_at(cv, 1) == 10, "const view read at 1");
}

TEST_CASE("view_t: view_slice") {
    int buf[] = {0, 10, 20, 30, 40, 50};
    ingot::view_t<int> v = ingot::view_from(buf, 6);

    ingot::view_t<int> mid = ingot::view_slice(v, 1, 4);
    CHECK_MESSAGE(ingot::view_len(mid) == 3, "slice len = high-low");
    CHECK_MESSAGE(ingot::view_data(mid) == buf + 1, "slice data offset");
    CHECK_MESSAGE(ingot::view_at(mid, 0) == 10, "slice[0]");
    CHECK_MESSAGE(ingot::view_at(mid, 2) == 30, "slice[last]");
}

TEST_CASE("view_t: view_slice edge cases") {
    int buf[] = {1, 2, 3};
    ingot::view_t<int> v = ingot::view_from(buf, 3);

    // 전체 슬라이스
    ingot::view_t<int> all = ingot::view_slice(v, 0, 3);
    CHECK_MESSAGE(ingot::view_len(all) == 3, "full slice");

    // 빈 슬라이스 (low == high)
    ingot::view_t<int> empty = ingot::view_slice(v, 2, 2);
    CHECK_MESSAGE(ingot::view_is_empty(empty), "empty slice");
    CHECK_MESSAGE(ingot::view_len(empty) == 0, "empty slice len 0");

    // 슬라이스의 슬라이스
    ingot::view_t<int> sub = ingot::view_slice(ingot::view_slice(v, 0, 3), 1, 2);
    CHECK_MESSAGE(ingot::view_len(sub) == 1, "nested slice");
    CHECK_MESSAGE(ingot::view_at(sub, 0) == 2, "nested slice value");
}

TEST_CASE("view_t: pointer iteration") {
    int buf[] = {1, 2, 3};
    ingot::view_t<int> v = ingot::view_from(buf, 3);

    int sum = 0;
    for (int* p = ingot::view_begin(v); p != ingot::view_end(v); ++p) {
        sum += *p;
    }
    CHECK_MESSAGE(sum == 6, "manual pointer iteration sum");
}

TEST_CASE("view_t: range-for via ADL") {
    int buf[] = {5, 10, 15};
    ingot::view_t<int> v = ingot::view_from(buf, 3);

    int product = 1;
    for (int& x : v) {
        product *= x;
    }
    CHECK_MESSAGE(product == 750, "range-for product");

    // range-for 내 수정
    for (int& x : v) {
        x += 1;
    }
    CHECK_MESSAGE(buf[0] == 6, "range-for mutation visible");
    CHECK_MESSAGE(buf[2] == 16, "range-for mutation last");
}

TEST_CASE("view_t: range-for const view") {
    const int buf[] = {2, 4, 6};
    ingot::view_t<const int> cv = ingot::view_from(buf, 3);

    int total = 0;
    for (const int& x : cv) {
        total += x;
    }
    CHECK_MESSAGE(total == 12, "const view range-for");
}
