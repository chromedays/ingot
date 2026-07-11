#include <cstring>

#include "doctest.h"

#include "ingot.h"

struct test_vec3 { float x, y, z; };
format_register_(test_vec3, "({}, {}, {})", _v.x, _v.y, _v.z)

struct test_point2 { int x, y; };
format_register_(test_point2, "[{}, {}]", _v.x, _v.y)

struct test_tagged { int id; const char* name; };
format_register_(test_tagged, "[{}, {}]", _v.id, ingot::str_from_cstr(_v.name))

struct test_ssb_val { int a, b; };
format_register_(test_ssb_val, "({}, {})", _v.a, _v.b)

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

TEST_CASE("string_t: UDL _str constexpr") {
    using namespace ingot;
    constexpr string_t s = "hello"_str;
    static_assert(s.len == 5, "UDL _str length must be constexpr");
    CHECK_MESSAGE(str_len(s) == 5, "UDL len is string length");
    CHECK_MESSAGE(str_at(s, 0) == 'h', "UDL first char");
    CHECK_MESSAGE(str_at(s, 4) == 'o', "UDL last char");
}

TEST_CASE("string_t: UDL _str equivalency with str_lit") {
    using namespace ingot;
    constexpr string_t a = str_lit("hello");
    constexpr string_t b = "hello"_str;
    static_assert(a.len == b.len, "len must match");
    CHECK_MESSAGE(str_len(a) == str_len(b), "len must match");
    CHECK_MESSAGE(str_equal(a, b), "must be equal");
}

TEST_CASE("string_t: UDL _str empty literal") {
    using namespace ingot;
    constexpr string_t s = ""_str;
    static_assert(s.len == 0, "empty string len must be 0");
    CHECK_MESSAGE(str_is_empty(s), "empty string by UDL");
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

TEST_CASE("string_builder_t: create/destroy") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 16);
    REQUIRE_MESSAGE(b.data != nullptr, "data should not be null after create");
    CHECK_MESSAGE(ingot::sb_len(b) == 0, "len 0 after create");
    CHECK_MESSAGE(ingot::sb_is_empty(b), "should be empty after create");
    CHECK_MESSAGE(ingot::sb_capacity(b) == 16, "capacity preserved");
    CHECK_MESSAGE(b.alloc == &heap, "alloc stored");
    ingot::sb_destroy(b);
    CHECK_MESSAGE(b.data == nullptr, "data null after destroy");
    CHECK_MESSAGE(b.alloc == nullptr, "alloc cleared");
}

TEST_CASE("string_builder_t: double destroy safe") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 4);
    ingot::sb_destroy(b);
    ingot::sb_destroy(b);
    CHECK_MESSAGE(b.data == nullptr, "still null");
}

TEST_CASE("string_builder_t: lazy allocation") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    CHECK_MESSAGE(b.data == nullptr, "no alloc when capacity=0");
    CHECK_MESSAGE(ingot::sb_len(b) == 0, "len 0");
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: append_char and growth") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 2);
    int64_t cap_before = ingot::sb_capacity(b);
    CHECK_MESSAGE(cap_before == 2, "initial capacity 2");

    ingot::sb_append(b, 'a');
    ingot::sb_append(b, 'b');
    CHECK_MESSAGE(ingot::sb_len(b) == 2, "len 2, still fits");
    CHECK_MESSAGE(ingot::sb_capacity(b) == 2, "no growth yet");

    ingot::sb_append(b, 'c');
    CHECK_MESSAGE(ingot::sb_len(b) == 3, "len 3 after overflow append");
    CHECK_MESSAGE(ingot::sb_capacity(b) >= 3, "capacity grew");
    CHECK_MESSAGE(ingot::sb_capacity(b) >= cap_before * 2, "at least doubled");

    ingot::sb_append(b, 'd');
    CHECK_MESSAGE(b.data[0] == 'a', "byte[0] preserved");
    CHECK_MESSAGE(b.data[1] == 'b', "byte[1] preserved");
    CHECK_MESSAGE(b.data[2] == 'c', "byte[2] preserved");
    CHECK_MESSAGE(b.data[3] == 'd', "byte[3] preserved");
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: append_cstr/bytes/view") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_append(b, "hello, ");
    ingot::sb_append(b, ingot::str_from_cstr("world"));
    ingot::sb_append_bytes(b, "!", 1);

    CHECK_MESSAGE(ingot::sb_len(b) == 13, "total len");
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("hello, world!")),
                  "assembled string");
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: reserve") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 4);
    ingot::sb_reserve(b, 100);
    CHECK_MESSAGE(ingot::sb_capacity(b) >= 100, "capacity >= reserved");
    CHECK_MESSAGE(ingot::sb_len(b) == 0, "len unchanged");
    ingot::sb_reserve(b, 50);
    CHECK_MESSAGE(ingot::sb_capacity(b) >= 100, "reserve does not shrink");
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: clear/truncate/pop") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 16);
    ingot::sb_append(b, "hello");
    int64_t cap = ingot::sb_capacity(b);

    ingot::sb_truncate(b, 3);
    CHECK_MESSAGE(ingot::sb_len(b) == 3, "truncated to 3");
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("hel")),
                  "contents after truncate");

    ingot::sb_pop(b);
    CHECK_MESSAGE(ingot::sb_len(b) == 2, "pop removes one byte");

    ingot::sb_clear(b);
    CHECK_MESSAGE(ingot::sb_len(b) == 0, "clear zeroes len");
    CHECK_MESSAGE(ingot::sb_capacity(b) == cap, "clear keeps capacity");
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: at and mutable data") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 8);
    ingot::sb_append(b, "abc");
    CHECK_MESSAGE(ingot::sb_at(b, 1) == 'b', "at 1");
    ingot::sb_data(b)[0] = 'A';
    CHECK_MESSAGE(ingot::sb_at(b, 0) == 'A', "mutable data write");
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: to_cstring") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_append(b, "hello");

    char* cstr = ingot::sb_to_cstring(b, heap);
    REQUIRE_MESSAGE(cstr != nullptr, "to_cstring should allocate");
    CHECK_MESSAGE(std::strcmp(cstr, "hello") == 0, "NUL-terminated copy");
    CHECK_MESSAGE(cstr[5] == '\0', "explicit NUL at len");
    heap.free(cstr, ingot::sb_len(b) + 1);
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: to_cstring empty") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 4);
    char* cstr = ingot::sb_to_cstring(b, heap);
    REQUIRE_MESSAGE(cstr != nullptr, "to_cstring should allocate");
    CHECK_MESSAGE(std::strcmp(cstr, "") == 0, "empty cstring");
    CHECK_MESSAGE(cstr[0] == '\0', "NUL at position 0");
    heap.free(cstr, 1);
    ingot::sb_destroy(b);
}

TEST_CASE("static_string_builder_t: create and append") {
    ingot::static_string_builder_t<16> b;
    ingot::ssb_create(b);
    CHECK_MESSAGE(ingot::ssb_len(b) == 0, "len 0 after create");
    CHECK_MESSAGE(ingot::ssb_is_empty(b), "empty");

    ingot::ssb_append(b, "hello");
    CHECK_MESSAGE(ingot::ssb_len(b) == 5, "len 5");
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b), ingot::str_from_cstr("hello")),
                  "contents");
    CHECK_MESSAGE(ingot::ssb_capacity(b) == 16, "capacity is N");
}

TEST_CASE("static_string_builder_t: is_full and remaining") {
    ingot::static_string_builder_t<3> b;
    ingot::ssb_create(b);
    CHECK_MESSAGE(!ingot::ssb_is_full(b), "not full initially");
    ingot::ssb_append(b, 'a');
    ingot::ssb_append(b, 'b');
    ingot::ssb_append(b, 'c');
    CHECK_MESSAGE(ingot::ssb_is_full(b), "full at N");
    CHECK_MESSAGE(ingot::ssb_len(b) == 3, "len == N");
}

TEST_CASE("static_string_builder_t: append_view/bytes") {
    ingot::static_string_builder_t<32> b;
    ingot::ssb_create(b);
    ingot::ssb_append(b, ingot::str_from_cstr("foo"));
    ingot::ssb_append_bytes(b, "bar", 3);
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b), ingot::str_from_cstr("foobar")),
                  "assembled");
}

TEST_CASE("static_string_builder_t: clear/truncate/pop/at") {
    ingot::static_string_builder_t<16> b;
    ingot::ssb_create(b);
    ingot::ssb_append(b, "hello");

    CHECK_MESSAGE(ingot::ssb_at(b, 0) == 'h', "at 0");
    CHECK_MESSAGE(ingot::ssb_at(b, 4) == 'o', "at 4");

    ingot::ssb_truncate(b, 3);
    CHECK_MESSAGE(ingot::ssb_len(b) == 3, "truncate to 3");

    ingot::ssb_pop(b);
    CHECK_MESSAGE(ingot::ssb_len(b) == 2, "pop");

    ingot::ssb_clear(b);
    CHECK_MESSAGE(ingot::ssb_len(b) == 0, "clear");
    CHECK_MESSAGE(ingot::ssb_capacity(b) == 16, "capacity unchanged");
}

TEST_CASE("static_string_builder_t: to_cstring") {
    ingot::static_string_builder_t<16> b;
    ingot::ssb_create(b);
    ingot::ssb_append(b, "hi");

    ingot::heap_allocator_t heap;
    char* cstr = ingot::ssb_to_cstring(b, heap);
    REQUIRE_MESSAGE(cstr != nullptr, "allocated");
    CHECK_MESSAGE(std::strcmp(cstr, "hi") == 0, "NUL-terminated copy");
    CHECK_MESSAGE(cstr[2] == '\0', "NUL at len");
    heap.free(cstr, ingot::ssb_len(b) + 1);
}

TEST_CASE("utf8: decode ASCII") {
    const char* s = "A";
    int width = 0;
    char32_t r = ingot::utf8_decode_rune(s, 1, &width);
    CHECK_MESSAGE(r == U'A', "ASCII codepoint");
    CHECK_MESSAGE(width == 1, "ASCII width 1");
}

TEST_CASE("utf8: decode multibyte") {
    const char* s = "\xEC\x84\xB8";  // 세 (U+C138)
    int width = 0;
    char32_t r = ingot::utf8_decode_rune(s, 3, &width);
    CHECK_MESSAGE(r == 0xC138, "Korean syllable 세");
    CHECK_MESSAGE(width == 3, "3-byte width");
}

TEST_CASE("utf8: decode invalid byte") {
    const char* s = "\xFF";
    int width = 0;
    char32_t r = ingot::utf8_decode_rune(s, 1, &width);
    CHECK_MESSAGE(r == ingot::utf8_rune_error, "invalid -> U+FFFD");
    CHECK_MESSAGE(width == 1, "advance 1 byte");
}

TEST_CASE("utf8: encode roundtrip") {
    char32_t runes[] = {U'A', 0xC138, U'\U0001F600'};  // A, 세, 😀
    for (char32_t r : runes) {
        char buf[4] = {0, 0, 0, 0};
        int width = ingot::utf8_encode_rune(r, buf);
        CHECK_MESSAGE(width > 0, "valid rune encodes");
        int dwidth = 0;
        char32_t back = ingot::utf8_decode_rune(buf, 4, &dwidth);
        CHECK_MESSAGE(back == r, "roundtrip preserves codepoint");
        CHECK_MESSAGE(dwidth == width, "roundtrip preserves width");
    }
}

TEST_CASE("utf8: encode invalid rune returns 0") {
    char buf[4] = {0, 0, 0, 0};
    int width = ingot::utf8_encode_rune(0x110000, buf);  // out of range
    CHECK_MESSAGE(width == 0, "out-of-range rejected");
    width = ingot::utf8_encode_rune(0xD800, buf);  // surrogate
    CHECK_MESSAGE(width == 0, "surrogate rejected");
}

TEST_CASE("utf8: validate") {
    CHECK_MESSAGE(ingot::utf8_validate(ingot::str_from_cstr("hello")), "ASCII valid");
    CHECK_MESSAGE(ingot::utf8_validate(ingot::str_from_cstr("Hello, 세계")), "multibyte valid");
    CHECK_MESSAGE(ingot::utf8_validate(ingot::str_from("", 0)), "empty valid");

    const char* bad = "\xFF\xFE";
    CHECK_MESSAGE(!ingot::utf8_validate(ingot::str_from(bad, 2)), "invalid bytes rejected");

    const char* truncated = "\xEC\x84";  // 3바이트 시퀀스인데 2바이트만
    CHECK_MESSAGE(!ingot::utf8_validate(ingot::str_from(truncated, 2)), "truncated rejected");
}

TEST_CASE("utf8: rune_count") {
    CHECK_MESSAGE(ingot::utf8_rune_count(ingot::str_from_cstr("abc")) == 3, "ASCII 3 runes");
    CHECK_MESSAGE(ingot::utf8_rune_count(ingot::str_from_cstr("세계")) == 2, "2 Korean runes");
    CHECK_MESSAGE(ingot::utf8_rune_count(ingot::str_from_cstr("Hello, 세계")) == 9,
                  "mixed ASCII + multibyte");
    CHECK_MESSAGE(ingot::utf8_rune_count(ingot::str_from("", 0)) == 0, "empty 0 runes");
}

TEST_CASE("utf8_rune_view: range-for over multibyte") {
    ingot::string_t s = ingot::str_from_cstr("A세");  // A(1) + 세(3) = 4 bytes, 2 runes
    char32_t collected[4];
    int count = 0;
    for (char32_t r : ingot::utf8_runes(s)) {
        collected[count++] = r;
    }
    CHECK_MESSAGE(count == 2, "2 runes from 4 bytes");
    CHECK_MESSAGE(collected[0] == U'A', "first rune A");
    CHECK_MESSAGE(collected[1] == 0xC138, "second rune 세");
}

TEST_CASE("utf8_rune_view: empty string") {
    ingot::string_t e = ingot::str_from("", 0);
    int count = 0;
    for (char32_t r : ingot::utf8_runes(e)) {
        (void)r;
        count++;
    }
    CHECK_MESSAGE(count == 0, "no runes from empty");
}

TEST_CASE("utf8_rune_view: invalid byte yields replacement") {
    const char* bad = "\xFF\x41";  // invalid + 'A'
    ingot::string_t s = ingot::str_from(bad, 2);
    int count = 0;
    char32_t first = 0;
    for (char32_t r : ingot::utf8_runes(s)) {
        if (count == 0) first = r;
        count++;
    }
    CHECK_MESSAGE(count == 2, "2 runes (1 invalid + 1 valid)");
    CHECK_MESSAGE(first == ingot::utf8_rune_error, "invalid byte -> U+FFFD");
}

// === sb_format / ssb_format ===

TEST_CASE("sb_format: basic integer") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("{}"), 42);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("42")),
                  "42");

    ingot::sb_clear(b);
    ingot::sb_format(b, ingot::str_lit("{}"), 0);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("0")),
                  "0");

    ingot::sb_clear(b);
    ingot::sb_format(b, ingot::str_lit("{}"), -7);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("-7")),
                  "-7");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: large integers") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("{}"), INT64_MAX);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("9223372036854775807")),
                  "INT64_MAX");

    ingot::sb_clear(b);
    ingot::sb_format(b, ingot::str_lit("{}"), INT64_MIN);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("-9223372036854775808")),
                  "INT64_MIN");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: unsigned integer") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("{}"), 42u);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("42")),
                  "unsigned 42");

    ingot::sb_clear(b);
    uint64_t big = UINT64_MAX;
    ingot::sb_format(b, ingot::str_lit("{}"), big);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("18446744073709551615")),
                  "UINT64_MAX");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: string_t") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::string_t name = ingot::str_from_cstr("world");
    ingot::sb_format(b, ingot::str_lit("hello, {}"), name);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("hello, world")),
                  "string_t arg");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: const char*") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("{}, {}!"), "hello", "world");
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("hello, world!")),
                  "const char* args");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: char") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("{}"), 'X');
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("X")),
                  "char arg");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: bool") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("{}"), true);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("true")),
                  "true");

    ingot::sb_clear(b);
    ingot::sb_format(b, ingot::str_lit("{}"), false);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("false")),
                  "false");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: float") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("{}"), 3.14);
    CHECK_MESSAGE(ingot::str_len(ingot::sb_to_string(b)) > 0,
                  "float produces non-empty output");

    // %g format: check prefix to avoid locale-dependent decimal separator issues
    ingot::string_t result = ingot::sb_to_string(b);
    CHECK_MESSAGE(result.data[0] == '3', "float starts with 3");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: double") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    double val = 2.5;
    ingot::sb_format(b, ingot::str_lit("{}"), val);
    ingot::string_t result = ingot::sb_to_string(b);
    CHECK_MESSAGE(result.data[0] == '2', "double starts with 2");
    CHECK_MESSAGE(ingot::str_len(result) > 0, "double non-empty");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: char32_t") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("{}"), U'A');
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("A")),
                  "ASCII rune");

    ingot::sb_clear(b);
    // 한 (U+D55C) = 3 bytes in UTF-8
    ingot::sb_format(b, ingot::str_lit("{}"), U'한');
    CHECK_MESSAGE(ingot::str_len(ingot::sb_to_string(b)) == 3,
                  "Korean rune is 3 bytes");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: multiple placeholders mixed types") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("{} + {} = {}"), 1, 2, 3);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("1 + 2 = 3")),
                  "three ints");

    ingot::sb_clear(b);
    ingot::sb_format(b, ingot::str_lit("int={} str={} bool={}"), 42,
                      ingot::str_lit("hi"), true);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("int=42 str=hi bool=true")),
                  "mixed types");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: escaped braces") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("{{}}"));
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("{}")),
                  "escaped {{}}");

    ingot::sb_clear(b);
    ingot::sb_format(b, ingot::str_lit("{{hello}}"));
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b), ingot::str_from_cstr("{hello}")),
                  "escaped {{ and }}");

    ingot::sb_clear(b);
    ingot::sb_format(b, ingot::str_lit("pre {{}}, post"));
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("pre {}, post")),
                  "escaped in middle of text");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: no placeholders") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("hello world"));
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("hello world")),
                  "no placeholders passthrough");

    ingot::sb_clear(b);
    ingot::sb_format(b, ingot::str_from("", 0));
    CHECK_MESSAGE(ingot::str_is_empty(ingot::sb_to_string(b)),
                  "empty format string");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: append to non-empty builder") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_append(b, ingot::str_from_cstr("start:"));
    ingot::sb_format(b, ingot::str_lit(" {}"), 99);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("start: 99")),
                  "append to existing content");

    ingot::sb_destroy(b);
}

// --- ssb_format ---

TEST_CASE("ssb_format: basic") {
    ingot::static_string_builder_t<128> b;
    ingot::ssb_create(b);

    ingot::ssb_format(b, ingot::str_lit("answer={}"), 42);
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("answer=42")),
                  "ssb integer");

    ingot::ssb_clear(b);
    ingot::ssb_format(b, ingot::str_lit("{} {} {}"), ingot::str_lit("a"),
                      ingot::str_lit("b"), ingot::str_lit("c"));
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("a b c")),
                  "ssb multiple strings");
}

TEST_CASE("ssb_format: no args") {
    ingot::static_string_builder_t<64> b;
    ingot::ssb_create(b);

    ingot::ssb_format(b, ingot::str_lit("static text"));
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("static text")),
                  "ssb no args");
}

TEST_CASE("ssb_format: bool and char") {
    ingot::static_string_builder_t<64> b;
    ingot::ssb_create(b);

    ingot::ssb_format(b, ingot::str_lit("bool={} char={}"), false, 'Z');
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("bool=false char=Z")),
                  "ssb bool and char");
}

TEST_CASE("ssb_format: int types") {
    ingot::static_string_builder_t<128> b;
    ingot::ssb_create(b);

    int32_t i32 = -123;
    ingot::ssb_format(b, ingot::str_lit("{}"), i32);
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("-123")),
                  "int32_t");

    ingot::ssb_clear(b);
    uint32_t u32 = 456;
    ingot::ssb_format(b, ingot::str_lit("{}"), u32);
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("456")),
                  "uint32_t");

    ingot::ssb_clear(b);
    int64_t i64 = -999;
    ingot::ssb_format(b, ingot::str_lit("{}"), i64);
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("-999")),
                  "int64_t");

    ingot::ssb_clear(b);
    uint64_t u64 = 777;
    ingot::ssb_format(b, ingot::str_lit("{}"), u64);
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("777")),
                  "uint64_t");
}

TEST_CASE("ssb_format: escaped braces") {
    ingot::static_string_builder_t<64> b;
    ingot::ssb_create(b);

    ingot::ssb_format(b, ingot::str_lit("open={{ close=}}"));
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("open={ close=}")),
                  "ssb escaped braces");
}

TEST_CASE("ssb_format: append to non-empty") {
    ingot::static_string_builder_t<128> b;
    ingot::ssb_create(b);

    ingot::ssb_append(b, ingot::str_from_cstr("pre:"));
    ingot::ssb_format(b, ingot::str_lit("{}"), 10);
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("pre:10")),
                   "ssb append to existing");
}

TEST_CASE("ssb_format: user-defined type") {
    ingot::static_string_builder_t<128> b;
    ingot::ssb_create(b);

    ingot::ssb_format(b, ingot::str_lit("val: {}"), test_ssb_val{3, 7});
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b),
                                   ingot::str_from_cstr("val: (3, 7)")),
                  "ssb user-defined type");
}

TEST_CASE("sb_format: user-defined struct via macro") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("vec: {}"), test_vec3{1.5f, 2.5f, 3.5f});
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("vec: (1.5, 2.5, 3.5)")),
                  "vec3 formatted");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: local struct via macro") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("p: {}"), test_point2{10, 20});
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("p: [10, 20]")),
                  "point2 formatted");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: user-defined type with name field") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("t: {}"), test_tagged{1, "a"});
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("t: [1, a]")),
                  "tagged formatted");

    ingot::sb_destroy(b);
}

TEST_CASE("sb_format: mixed builtin and user-defined types") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::sb_format(b, ingot::str_lit("v={} n={} b={}"),
                     test_vec3{1, 2, 3}, 42, true);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("v=(1, 2, 3) n=42 b=true")),
                  "mixed types");

    ingot::sb_destroy(b);
}

TEST_CASE("format container: empty static_vector") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;
    ingot::sv_create(v, heap, 4);

    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_format(b, ingot::str_lit("v: {}"), v);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("v: []")),
                  "empty vector");

    ingot::sb_destroy(b);
    ingot::sv_destroy(v);
}

TEST_CASE("format container: static_vector with elements") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<int> v;
    ingot::sv_create(v, heap, 4);
    ingot::sv_push(v, 1);
    ingot::sv_push(v, 2);
    ingot::sv_push(v, 3);

    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_format(b, ingot::str_lit("v: {}"), v);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("v: [1, 2, 3]")),
                  "vector with elements");

    ingot::sb_destroy(b);
    ingot::sv_destroy(v);
}

TEST_CASE("format container: recursive formatting") {
    ingot::heap_allocator_t heap;
    ingot::static_vector_t<test_vec3> v;
    ingot::sv_create(v, heap, 4);
    ingot::sv_push(v, test_vec3{1, 2, 3});
    ingot::sv_push(v, test_vec3{4, 5, 6});

    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_format(b, ingot::str_lit("v: {}"), v);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("v: [(1, 2, 3), (4, 5, 6)]")),
                  "recursive vector");

    ingot::sb_destroy(b);
    ingot::sv_destroy(v);
}

TEST_CASE("format container: string_t") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::string_t s = ingot::str_from_cstr("hello");
    ingot::sb_format(b, ingot::str_lit("s: {}"), s);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("s: hello")),
                  "string_t");

    ingot::sb_destroy(b);
}

TEST_CASE("format container: string_builder_t") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t sb;
    ingot::sb_create(sb, heap, 64);
    ingot::sb_append(sb, ingot::str_from_cstr("hi"));

    ingot::string_builder_t out;
    ingot::sb_create(out, heap, 0);
    ingot::sb_format(out, ingot::str_lit("b: {}"), sb);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(out),
                                   ingot::str_from_cstr("b: hi [2/64]")),
                  "string_builder_t");

    ingot::sb_destroy(out);
    ingot::sb_destroy(sb);
}

TEST_CASE("format container: static_string_builder_t") {
    ingot::heap_allocator_t heap;
    ingot::static_string_builder_t<32> ssb;
    ingot::ssb_create(ssb);
    ingot::ssb_append(ssb, ingot::str_from_cstr("abc"));

    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_format(b, ingot::str_lit("b: {}"), ssb);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("b: abc [3/32]")),
                  "static_string_builder_t");

    ingot::sb_destroy(b);
}

TEST_CASE("format container: utf8_rune_view_t") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);

    ingot::utf8_rune_view_t r = ingot::utf8_runes(ingot::str_from_cstr("한글"));
    ingot::sb_format(b, ingot::str_lit("r: {}"), r);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("r: 한글")),
                  "utf8_rune_view_t");

    ingot::sb_destroy(b);
}

TEST_CASE("format container: view_t") {
    ingot::heap_allocator_t heap;
    int arr[] = {10, 20, 30};
    ingot::view_t<int> v = ingot::view_from(arr);

    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_format(b, ingot::str_lit("v: {}"), v);
    CHECK_MESSAGE(ingot::str_equal(ingot::sb_to_string(b),
                                   ingot::str_from_cstr("v: [10, 20, 30]")),
                  "view_t");

    ingot::sb_destroy(b);
}
