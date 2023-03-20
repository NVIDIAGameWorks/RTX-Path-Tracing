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

#pragma once

#include <donut/core/math/math.h>

#include <array>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <regex>

// A set of thread-safe string manipulation functions

namespace donut::string_utils {

	// case-insensitive string comparison

	// note: strcasecmp is a POSIX function and there is no standardized
	// equivalent as of C++17
	template <typename T> bool strcasecmp(T const& a, T const& b)
	{
		return a.size() == b.size() &&
			std::equal(a.begin(), a.end(), b.begin(), b.end(), 
				[](char a, char b) { return std::tolower(a) == std::tolower(b); });
	}

	template <typename T> bool strcasencmp(T const& a, T const& b, size_t n)
	{
		return a.size()>=n && b.size()>=n &&
			std::equal(a.begin(), a.begin()+n, b.begin(), b.begin()+n,
				[](char a, char b) { return std::tolower(a) == std::tolower(b); });
	}

	inline bool starts_with(std::string_view const& value, std::string_view const& beginning)
	{
		if (beginning.size() > value.size())
			return false;

		return std::equal(beginning.begin(), beginning.end(), value.begin());
	}
	
	inline bool ends_with(std::string_view const& value, std::string_view const& ending)
	{
		if (ending.size() > value.size())
			return false;

		return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
	}

	// Trim white space

	// for "C" locale characters trimmed are ' ', '\f', '\n', '\r', '\t', '\v'
	// see: https://en.cppreference.com/w/cpp/string/byte/isspace

	inline void ltrim(std::string_view& s)
	{
		s.remove_prefix(std::distance(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
			return !isspace(ch);
			})));
	}

	inline void ltrim(std::string& s)
	{
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
			return !std::isspace(ch);
			}));
	}

	inline void rtrim(std::string_view& s)
	{
		s.remove_suffix(std::distance(std::find_if(s.rbegin(), s.rend(), [](int ch) {
			return !isspace(ch); 
			}).base(), s.end()));
	}

	inline void rtrim(std::string& s)
	{
		s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
			return !std::isspace(ch);
			}).base(), s.end());
	}

	template <typename T> void trim(T& s)
	{
		ltrim(s);
		rtrim(s);
	}

	// trim specific characters

	inline void ltrim(std::string_view& s, int c)
	{
		s.remove_prefix(std::distance(s.begin(), std::find_if(s.begin(), s.end(), [&](int ch) {
			return ch!=c;
			})));
	}

	inline void ltrim(std::string& s, int c)
	{
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](int ch) {
			return ch!=c;
			}));
	}

	inline void rtrim(std::string_view& s, int c)
	{
		s.remove_suffix(std::distance(std::find_if(s.rbegin(), s.rend(), [&](int ch) {
			return ch != c;
			}).base(), s.end()));
	}

	inline void rtrim(std::string& s, int c)
	{
		s.erase(std::find_if(s.rbegin(), s.rend(), [&](int ch) {
			return ch!=c;
			}).base(), s.end());
	}

	template <typename T> void trim(T& s, int c)
	{
		ltrim(s, c);
		rtrim(s, c);
	}

	// upper/lower case conversions

	inline void tolower(std::string& s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
	}

	inline void toupper(std::string& s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
	}

	// regex tokens split (default delimiter are space, comma, pipe and semi-colon characters)
	// note : std::regex throws runt_time exceptions on invalid expressions
	inline std::vector<std::string> split(std::string const& s, char const* regex = "[\\s+,|:]")
	{
		std::regex rx(regex);
		std::sregex_token_iterator it{ s.begin(), s.end(), rx, -1 }, end;

		std::vector<std::string> tokens;
		for (;it != end; ++it)
			if (!it->str().empty())
				tokens.push_back(*it);
		return tokens;
	}

	inline std::vector<std::string_view> split(std::string_view const s, char const* regex = "[\\s+,|:]")
	{
		std::regex rx(regex);
		std::cregex_token_iterator it{ s.data(), s.data() + s.size(), rx, -1 }, end;

		std::vector<std::string_view> tokens;
		for (;it != end; ++it)
			if (it->length()>0)
				tokens.push_back({it->first, (size_t)it->length()});
		return tokens;
	}

	// Thread-safe & range-checked strings to number conversions
	
	template <typename T> bool istrue(T const& s)
	{
		namespace ds = donut::string_utils;
		return ds::strcasecmp(s, T("true")) || ds::strcasecmp(s, T("on")) || ds::strcasecmp(s, T("yes")) || ds::strcasecmp(s, T("1"));
	}

	template <typename T> bool isfalse(T const& s)
	{
		namespace ds = donut::string_utils;
		return ds::strcasecmp(s, T("false")) || ds::strcasecmp(s, T("off")) || ds::strcasecmp(s, T("no")) || ds::strcasecmp(s, T("0"));
	}

	inline std::optional<bool> stob(std::string_view s)
	{
		trim(s);
		if (istrue(s))  return true;
		if (isfalse(s)) return false;
		return std::nullopt;
	}
	
	template <typename T> T sto_number(std::string const& s) { return (T)std::stoi(s, nullptr, 0); }


	template <typename T> std::optional<T> from_string(std::string const& s)
	{
		T value;
		try { value = sto_number<T>(s); }
		catch (std::invalid_argument&) { return std::nullopt; }
		catch (std::out_of_range&) { return std::nullopt; }
		return value;
	}

	// generic number & vector parsing

	template <typename T> std::optional<T> parse(std::string_view s)
	{
		namespace ds = donut::string_utils;

		trim(s);		
		if (ds::strcasencmp(s, std::string_view("0x"), 2)) 
		{
			// as of C++17, std::from_chars does handle hex, so fall back on strings
			return from_string<T>(std::string(s));
		}
		else
		{
			// as of C++17, std::from_chars returns an error when parsing integers
			// with a '+' sign prefix
			ltrim(s, '+'); 
			T value;
			if (auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), value); ec == std::errc())
				return value;
		}
		return std::optional<T>();
	}

	template <typename T> std::optional<T> parse(std::string const& s)
	{
		// for results consistency, fall back on std::string_view parsing
		return parse<T>(std::string_view(s));
	}


	template <typename T> std::optional<T> parse_vector(std::string_view s)
	{
		std::regex rx("[\\s+,|:]");
		std::cregex_token_iterator it{ s.data(), s.data() + s.size(), rx, -1 }, last;

		T value; uint8_t dim = 0; 
		for (; it != last; ++it)
		{
			if (it->length() == 0)
				continue;

			if (dim >= T::DIM)
				return std::optional<T>();

			if (auto v = parse<decltype(value.x)>(std::string_view({ it->first, (size_t)it->length() })))
				value[dim++] = *v;
			else
				return std::optional<T>();
		}
		return dim == T::DIM ? value : std::optional<T>();
	}

	template <typename T> std::optional<T> parse_vector(std::string const& s)
	{
		// for results consistency, fall back on std::string_view parsing
		return parse_vector<T>(std::string_view(s));
	}

	// number  parsing specializations

	template <> long sto_number(std::string const& s);
	template <> float sto_number(std::string const& s);
	template <> double sto_number(std::string const& s);

	template <> std::optional<bool> from_string(std::string const& s);

	template <> std::optional<bool> parse<bool>(std::string_view s);
	template <> std::optional<float> parse(std::string_view s);
	template <> std::optional<double> parse(std::string_view s);

	template <> std::optional<dm::bool2> parse(std::string_view s);
	template <> std::optional<dm::bool3> parse(std::string_view s);
	template <> std::optional<dm::bool4> parse(std::string_view s);

	template <> std::optional<dm::int2> parse(std::string_view s);
	template <> std::optional<dm::int3> parse(std::string_view s);
	template <> std::optional<dm::int4> parse(std::string_view s);

	template <> std::optional<dm::uint2> parse(std::string_view s);
	template <> std::optional<dm::uint3> parse(std::string_view s);
	template <> std::optional<dm::uint4> parse(std::string_view s);

	template <> std::optional<dm::float2> parse(std::string_view s);
	template <> std::optional<dm::float3> parse(std::string_view s);
	template <> std::optional<dm::float4> parse(std::string_view s);

	template <> std::optional<std::string_view> parse<std::string_view>(std::string_view s);
	template <> std::optional<std::string> parse<std::string>(std::string_view s);



} // end namespace donut::string_utils

namespace ds = donut::string_utils;
