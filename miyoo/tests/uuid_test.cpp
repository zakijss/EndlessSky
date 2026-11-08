// Self-check for source/soft/compat/uuid/uuid.h: c++ -std=c++20 -I source/soft/compat miyoo/tests/uuid_test.cpp
#include <uuid/uuid.h>
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
	uuid_t a, b;
	const char *text = "123e4567-e89b-42d3-a456-426614174000";
	assert(uuid_parse(text, a) == 0);
	char out[37];
	uuid_unparse_lower(a, out);
	assert(std::strcmp(out, text) == 0);
	assert(uuid_parse("123E4567-E89B-42D3-A456-426614174000", b) == 0 && uuid_compare(a, b) == 0);
	assert(uuid_parse("123e4567e89b-42d3-a456-426614174000-", b) == -1);
	assert(uuid_parse("123e4567-e89b-42d3-a456-42661417400g", b) == -1);
	assert(uuid_parse("short", b) == -1);

	uuid_generate_random(a);
	uuid_generate_random(b);
	assert(uuid_compare(a, b) != 0 && !uuid_is_null(a));
	assert((a[6] >> 4) == 4 && (a[8] & 0xC0) == 0x80);
	uuid_unparse_lower(a, out);
	assert(std::strlen(out) == 36 && out[14] == '4');

	uuid_t zero = {};
	assert(uuid_is_null(zero));
	uuid_copy(b, a);
	assert(uuid_compare(a, b) == 0);
	std::puts("uuid ok");
}
