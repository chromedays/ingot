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
