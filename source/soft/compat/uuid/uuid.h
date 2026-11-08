/* uuid.h
The part of libuuid that EsUuid uses, for the Miyoo build (no util-linux on the device).

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <cstdio>
#include <cstring>
#include <random>

typedef unsigned char uuid_t[16];

inline void uuid_copy(uuid_t dst, const uuid_t src)
{
	std::memcpy(dst, src, 16);
}

inline int uuid_compare(const uuid_t a, const uuid_t b)
{
	return std::memcmp(a, b, 16);
}

inline int uuid_is_null(const uuid_t id)
{
	for(int i = 0; i < 16; ++i)
		if(id[i])
			return 0;
	return 1;
}

// Version 4 (random), RFC 4122 variant, as libuuid's uuid_generate_random.
inline void uuid_generate_random(uuid_t out)
{
	bool filled = false;
	if(FILE *f = std::fopen("/dev/urandom", "rb"))
	{
		filled = std::fread(out, 1, 16, f) == 16;
		std::fclose(f);
	}
	if(!filled)
	{
		std::random_device device;
		for(int i = 0; i < 16; ++i)
			out[i] = static_cast<unsigned char>(device());
	}
	out[6] = static_cast<unsigned char>((out[6] & 0x0F) | 0x40);
	out[8] = static_cast<unsigned char>((out[8] & 0x3F) | 0x80);
}

// Accepts exactly xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (either case). Returns 0 on success.
inline int uuid_parse(const char *in, uuid_t out)
{
	if(!in || std::strlen(in) != 36)
		return -1;
	int byte = 0;
	for(int i = 0; i < 36; )
	{
		if(i == 8 || i == 13 || i == 18 || i == 23)
		{
			if(in[i++] != '-')
				return -1;
			continue;
		}
		int value = 0;
		for(int k = 0; k < 2; ++k, ++i)
		{
			char c = in[i];
			int digit = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10
				: (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
			if(digit < 0)
				return -1;
			value = value * 16 + digit;
		}
		out[byte++] = static_cast<unsigned char>(value);
	}
	return 0;
}

// Writes 36 characters and a terminating null.
inline void uuid_unparse_lower(const uuid_t id, char *out)
{
	static const char HEX[] = "0123456789abcdef";
	for(int i = 0, o = 0; i < 16; ++i)
	{
		if(i == 4 || i == 6 || i == 8 || i == 10)
			out[o++] = '-';
		out[o++] = HEX[id[i] >> 4];
		out[o++] = HEX[id[i] & 15];
		out[o] = '\0';
	}
}
