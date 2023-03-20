/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <donut/core/string_utils.h>
#include <cstring>

namespace donut::string_utils
{
	template <> long sto_number(std::string const& s) 
	{ 
		return std::stol(s, nullptr, 0); 
	}
	
	template <> float sto_number(std::string const& s) 
	{ 
		return std::stof(s); 
	
	}

	template <> double sto_number(std::string const& s)
	{ 
		return std::stod(s); 
	}

	template <> std::optional<bool> from_string(std::string const& s) 
	{ 
		return stob(s); 
	}

	template <> std::optional<float> parse(std::string_view s)
	{
		trim(s);
		trim(s, '+');

		char buf[32];
		buf[sizeof(buf) - 1] = 0;
		strncpy(buf, s.data(), std::min(s.size(), sizeof(buf) - 1));
		char* endptr = buf;
		float value = strtof(buf, &endptr);

		if (endptr == buf)
			return std::optional<float>();

		return value;
	}

	template <> std::optional<double> parse(std::string_view s)
	{
		trim(s);
		trim(s, '+');

		char buf[32];
		buf[sizeof(buf) - 1] = 0;
		strncpy(buf, s.data(), std::min(s.size(), sizeof(buf) - 1));
		char* endptr = buf;
		double value = strtod(buf, &endptr);

		if (endptr == buf)
			return std::optional<double>();

		return value;
	}

	template <> std::optional<bool> parse<bool>(std::string_view s) 
	{
		return stob(s); 
	}

	template <> std::optional<std::string_view> parse<std::string_view>(std::string_view s)
	{
		trim(s);
		trim(s, '"');
		return s;
	}

	template <> std::optional<std::string> parse<std::string>(std::string_view s)
	{
		if (auto r = parse<std::string_view>(s))
			return std::string(*r);
		return std::nullopt;
	}

	template <> std::optional<dm::bool2> parse(std::string_view s) { return parse_vector<dm::bool2>(s); }
	template <> std::optional<dm::bool3> parse(std::string_view s) { return parse_vector<dm::bool3>(s); }
	template <> std::optional<dm::bool4> parse(std::string_view s) { return parse_vector<dm::bool4>(s); }

	template <> std::optional<dm::int2> parse(std::string_view s) { return parse_vector<dm::int2>(s); }
	template <> std::optional<dm::int3> parse(std::string_view s) { return parse_vector<dm::int3>(s); }
	template <> std::optional<dm::int4> parse(std::string_view s) { return parse_vector<dm::int4>(s); }

	template <> std::optional<dm::uint2> parse(std::string_view s) { return parse_vector<dm::uint2>(s); }
	template <> std::optional<dm::uint3> parse(std::string_view s) { return parse_vector<dm::uint3>(s); }
	template <> std::optional<dm::uint4> parse(std::string_view s) { return parse_vector<dm::uint4>(s); }

	template <> std::optional<dm::float2> parse(std::string_view s) { return parse_vector<dm::float2>(s); }
	template <> std::optional<dm::float3> parse(std::string_view s) { return parse_vector<dm::float3>(s); }
	template <> std::optional<dm::float4> parse(std::string_view s) { return parse_vector<dm::float4>(s); }

} // end namespace donut::string_utils

