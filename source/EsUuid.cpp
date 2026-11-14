/* EsUuid.cpp
Copyright (c) 2021 by Benjamin Hauch

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "EsUuid.h"

#include "Logger.h"
#ifdef _WIN32
#include "text/Utf8.h"

#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__ANDROID__)
// ESMP: Android lacks libuuid. This minimal shim implements the handful of
// libuuid calls Endless Sky uses (RFC-4122 v4 UUIDs).
#include <array>
#include <cstring>
#include <fstream>
#include <random>
#include <string>

using uuid_t = std::array<unsigned char, 16>;

namespace {

	int uuid_parse(const char *in, uuid_t &out)
	{
		auto hexVal = [](char c) -> int {
			if(c >= '0' && c <= '9') return c - '0';
			if(c >= 'a' && c <= 'f') return c - 'a' + 10;
			if(c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};
		int nibbles = 0;
		size_t index = 0;
		unsigned char current = 0;
		for(const char *p = in; *p; ++p)
		{
			if(*p == '-')
				continue;
			int v = hexVal(*p);
			if(v < 0 || index >= 16)
				return -1;
			current = static_cast<unsigned char>((current << 4) | v);
			if(++nibbles % 2 == 0)
				out[index++] = current;
		}
		return (nibbles == 32 && index == 16) ? 0 : -1;
	}

	void uuid_generate_random(uuid_t &out)
	{
		// Prefer the kernel's UUID generator, falling back to random_device.
		std::ifstream kernel("/proc/sys/kernel/random/uuid");
		std::string line;
		if(std::getline(kernel, line) && uuid_parse(line.c_str(), out) == 0)
			return;
		std::random_device device;
		for(size_t i = 0; i + 3 < out.size(); i += 4)
		{
			const unsigned int value = device();
			out[i] = static_cast<unsigned char>(value);
			out[i + 1] = static_cast<unsigned char>(value >> 8);
			out[i + 2] = static_cast<unsigned char>(value >> 16);
			out[i + 3] = static_cast<unsigned char>(value >> 24);
		}
		out[6] = static_cast<unsigned char>((out[6] & 0x0f) | 0x40); // version 4
		out[8] = static_cast<unsigned char>((out[8] & 0x3f) | 0x80); // RFC-4122 variant
	}

	int uuid_is_null(const uuid_t &id)
	{
		for(const unsigned char b : id)
			if(b)
				return 0;
		return 1;
	}

	int uuid_compare(const uuid_t &a, const uuid_t &b)
	{
		const int result = std::memcmp(a.data(), b.data(), a.size());
		return result < 0 ? -1 : (result > 0 ? 1 : 0);
	}

	void uuid_unparse_lower(const uuid_t &id, char *out)
	{
		static constexpr char hex[] = "0123456789abcdef";
		size_t o = 0;
		for(size_t i = 0; i < 16; ++i)
		{
			if(i == 4 || i == 6 || i == 8 || i == 10)
				out[o++] = '-';
			out[o++] = hex[id[i] >> 4];
			out[o++] = hex[id[i] & 0xf];
		}
		out[o] = '\0';
	}
}
#else
#include <uuid/uuid.h>
#endif

#include <stdexcept>

using namespace std;

namespace {
#ifndef _WIN32
	constexpr size_t UUID_BUFFER_LENGTH = 37;
#endif

	EsUuid::UuidType ParseUuid(const string &str)
	{
		EsUuid::UuidType value;
#ifdef _WIN32
		auto data = Utf8::ToUTF16(str, false);
		RPC_STATUS status = UuidFromStringW(reinterpret_cast<RPC_WSTR>(&data[0]), &value.id);
		if(status == RPC_S_INVALID_STRING_UUID)
			throw invalid_argument("Cannot convert \"" + str + "\" into a UUID");
		else if(status != RPC_S_OK)
			throw runtime_error("Fatal error parsing \"" + str + "\" as a UUID");
#else
		auto result = uuid_parse(str.data(), value.id);
		if(result == -1)
			throw invalid_argument("Cannot convert \"" + str + "\" into a UUID");
		else if(result != 0)
			throw runtime_error("Fatal error parsing \"" + str + "\" as a UUID");
#endif
		return value;
	}

#ifdef _WIN32
	bool IsNil(const UUID &id) noexcept
	{
		RPC_STATUS status;
		return UuidIsNil(const_cast<UUID *>(&id), &status);
	}
#else
	bool IsNil(const uuid_t &id) noexcept
	{
		return uuid_is_null(id) == 1;
	}
#endif

#ifdef _WIN32
	string Serialize(const UUID &id)
	{
		wchar_t *buf = nullptr;
		RPC_STATUS status = UuidToStringW(&id, reinterpret_cast<RPC_WSTR *>(&buf));

		string result = (status == RPC_S_OK) ? Utf8::ToUTF8(buf) : string{};
		if(result.empty())
			Logger::Log("Failed to serialize UUID!", Logger::Level::WARNING);
		else
			RpcStringFreeW(reinterpret_cast<RPC_WSTR *>(&buf));

		return result;
	}
#else
	string Serialize(const uuid_t &id)
	{
		char buf[UUID_BUFFER_LENGTH];
		uuid_unparse_lower(id, buf);
		return buf;
	}
#endif

	signed int Compare(const EsUuid::UuidType &a, const EsUuid::UuidType &b)
	{
#ifdef _WIN32
		RPC_STATUS status;
		auto result = UuidCompare(const_cast<UUID *>(&a.id), const_cast<UUID *>(&b.id), &status);
		if(status != RPC_S_OK)
			throw runtime_error("Fatal error comparing UUIDs \"" + Serialize(a.id) + "\" and \"" + Serialize(b.id) + "\"");
		return result;
#else
		return uuid_compare(a.id, b.id);
#endif
	}
}



// Get a version 4 (random) Universally Unique Identifier (see IETF RFC 4122).
EsUuid::UuidType EsUuid::MakeUuid()
{
	EsUuid::UuidType value;
#ifdef _WIN32
	RPC_STATUS status = UuidCreate(&value.id);
	if(status == RPC_S_UUID_LOCAL_ONLY)
		Logger::Log("Created locally unique UUID only", Logger::Level::WARNING);
	else if(status == RPC_S_UUID_NO_ADDRESS)
		throw runtime_error("Failed to create UUID");
#else
	uuid_generate_random(value.id);
#endif
	return value;
}



EsUuid EsUuid::FromString(const string &input)
{
	return EsUuid(input);
}



// Explicitly copy the value of the other UUID.
void EsUuid::Clone(const EsUuid &other)
{
	value = other.Value();
}



bool EsUuid::operator==(const EsUuid &other) const noexcept(false)
{
	return Compare(Value(), other.Value()) == 0;
}



bool EsUuid::operator!=(const EsUuid &other) const noexcept(false)
{
	return !(*this == other);
}



bool EsUuid::operator<(const EsUuid &other) const noexcept(false)
{
	return Compare(Value(), other.Value()) < 0;
}



string EsUuid::ToString() const noexcept(false)
{
	return Serialize(Value().id);
}



// Internal constructor. Note that the provided value may not be a valid v4 UUID,
// in which case an error is logged and we return a new UUID.
EsUuid::EsUuid(const string &input)
{
	try {
		value = ParseUuid(input);
	}
	catch(const invalid_argument &err)
	{
		Logger::Log(err.what(), Logger::Level::WARNING);
	}
}



const EsUuid::UuidType &EsUuid::Value() const
{
	if(IsNil(value.id))
		value = MakeUuid();

	return value;
}
