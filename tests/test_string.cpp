#include <cstring>

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

    ingot::sb_append_char(b, 'a');
    ingot::sb_append_char(b, 'b');
    CHECK_MESSAGE(ingot::sb_len(b) == 2, "len 2, still fits");
    CHECK_MESSAGE(ingot::sb_capacity(b) == 2, "no growth yet");

    ingot::sb_append_char(b, 'c');
    CHECK_MESSAGE(ingot::sb_len(b) == 3, "len 3 after overflow append");
    CHECK_MESSAGE(ingot::sb_capacity(b) >= 3, "capacity grew");
    CHECK_MESSAGE(ingot::sb_capacity(b) >= cap_before * 2, "at least doubled");

    ingot::sb_append_char(b, 'd');
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

    ingot::sb_append_cstr(b, "hello, ");
    ingot::sb_append_view(b, ingot::str_from_cstr("world"));
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
    ingot::sb_append_cstr(b, "hello");
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
    ingot::sb_append_cstr(b, "abc");
    CHECK_MESSAGE(ingot::sb_at(b, 1) == 'b', "at 1");
    ingot::sb_data(b)[0] = 'A';
    CHECK_MESSAGE(ingot::sb_at(b, 0) == 'A', "mutable data write");
    ingot::sb_destroy(b);
}

TEST_CASE("string_builder_t: to_cstring") {
    ingot::heap_allocator_t heap;
    ingot::string_builder_t b;
    ingot::sb_create(b, heap, 0);
    ingot::sb_append_cstr(b, "hello");

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

    ingot::ssb_append_cstr(b, "hello");
    CHECK_MESSAGE(ingot::ssb_len(b) == 5, "len 5");
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b), ingot::str_from_cstr("hello")),
                  "contents");
    CHECK_MESSAGE(ingot::ssb_capacity(b) == 16, "capacity is N");
}

TEST_CASE("static_string_builder_t: is_full and remaining") {
    ingot::static_string_builder_t<3> b;
    ingot::ssb_create(b);
    CHECK_MESSAGE(!ingot::ssb_is_full(b), "not full initially");
    ingot::ssb_append_char(b, 'a');
    ingot::ssb_append_char(b, 'b');
    ingot::ssb_append_char(b, 'c');
    CHECK_MESSAGE(ingot::ssb_is_full(b), "full at N");
    CHECK_MESSAGE(ingot::ssb_len(b) == 3, "len == N");
}

TEST_CASE("static_string_builder_t: append_view/bytes") {
    ingot::static_string_builder_t<32> b;
    ingot::ssb_create(b);
    ingot::ssb_append_view(b, ingot::str_from_cstr("foo"));
    ingot::ssb_append_bytes(b, "bar", 3);
    CHECK_MESSAGE(ingot::str_equal(ingot::ssb_to_string(b), ingot::str_from_cstr("foobar")),
                  "assembled");
}

TEST_CASE("static_string_builder_t: clear/truncate/pop/at") {
    ingot::static_string_builder_t<16> b;
    ingot::ssb_create(b);
    ingot::ssb_append_cstr(b, "hello");

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
    ingot::ssb_append_cstr(b, "hi");

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
