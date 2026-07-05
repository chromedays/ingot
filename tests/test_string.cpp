#include "doctest.h"

#include "ingot.h"

TEST_CASE("string types smoke") {
    CHECK(true);
}

TEST_CASE("string_t: str_from") {
    const char* p = "hello";
    ingot::string_t s = ingot::str_from(p, 5);
    CHECK_MESSAGE(ingot::str_len(s) == 5, "len should be 5");
    CHECK_MESSAGE(ingot::str_data(s) == p, "data should point to source");
    CHECK_MESSAGE(!ingot::str_is_empty(s), "should not be empty");
}

TEST_CASE("string_t: str_from_cstr") {
    ingot::string_t s = ingot::str_from_cstr("hello");
    CHECK_MESSAGE(ingot::str_len(s) == 5, "len via strlen");
    CHECK_MESSAGE(ingot::str_data(s)[0] == 'h', "first byte");
    CHECK_MESSAGE(ingot::str_data(s)[4] == 'o', "last byte");
}

TEST_CASE("string_t: str_lit constexpr") {
    constexpr ingot::string_t s = ingot::str_lit("hello");
    static_assert(s.len == 5, "str_lit length must be constexpr");
    CHECK_MESSAGE(ingot::str_len(s) == 5, "literal len excludes NUL");
}

TEST_CASE("string_t: str_at") {
    ingot::string_t s = ingot::str_from_cstr("abc");
    CHECK_MESSAGE(ingot::str_at(s, 0) == 'a', "at 0");
    CHECK_MESSAGE(ingot::str_at(s, 2) == 'c', "at 2");
}

TEST_CASE("string_t: empty view") {
    ingot::string_t e = ingot::str_from("", 0);
    CHECK_MESSAGE(ingot::str_is_empty(e), "empty");
    CHECK_MESSAGE(ingot::str_len(e) == 0, "len 0");
}

TEST_CASE("string_t: str_equal") {
    ingot::string_t a = ingot::str_from_cstr("hello");
    ingot::string_t b = ingot::str_from_cstr("hello");
    ingot::string_t c = ingot::str_from_cstr("world");
    CHECK_MESSAGE(ingot::str_equal(a, b), "equal contents");
    CHECK_MESSAGE(!ingot::str_equal(a, c), "different contents");
    CHECK_MESSAGE(ingot::str_equal(ingot::str_from("", 0), ingot::str_from("", 0)),
                  "two empty views equal");
    CHECK_MESSAGE(!ingot::str_equal(ingot::str_from_cstr("hi"), ingot::str_from_cstr("hi!")),
                  "different lengths not equal");
}

TEST_CASE("string_t: str_slice") {
    ingot::string_t s = ingot::str_from_cstr("hello world");
    ingot::string_t sub = ingot::str_slice(s, 6, 11);
    CHECK_MESSAGE(ingot::str_equal(sub, ingot::str_from_cstr("world")), "substring");
    CHECK_MESSAGE(ingot::str_len(sub) == 5, "substring len");
    ingot::string_t empty_slice = ingot::str_slice(s, 0, 0);
    CHECK_MESSAGE(ingot::str_is_empty(empty_slice), "zero-width slice");
    ingot::string_t full_slice = ingot::str_slice(s, 0, 11);
    CHECK_MESSAGE(ingot::str_equal(full_slice, s), "full slice equals source");
}

TEST_CASE("string_t: byte range-for") {
    ingot::string_t s = ingot::str_from_cstr("abc");
    const char expected[] = {'a', 'b', 'c'};
    int count = 0;
    for (char c : s) {
        CHECK_MESSAGE(c == expected[count], "byte mismatch");
        count++;
    }
    CHECK_MESSAGE(count == 3, "should visit 3 bytes");
}
