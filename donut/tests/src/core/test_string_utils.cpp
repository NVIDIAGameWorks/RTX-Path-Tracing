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

#include <donut/tests/utils.h>

#include <limits>

using namespace donut;
using namespace donut::math;

void test_strcasecmp()
{
	typedef std::string S;
	typedef std::string_view SV;

	CHECK(ds::strcasecmp(S("hello world"), S("hello world")) == true);
	CHECK(ds::strcasecmp(SV("hello world"), SV("hello world")) == true);

	CHECK(ds::strcasecmp(S("hello world "), S("hello world")) == false);
	CHECK(ds::strcasecmp(SV("hello world "), SV("hello world")) == false);

	CHECK(ds::strcasecmp(S("HeLlo World"), S("hello world")) == true);
	CHECK(ds::strcasecmp(SV("HeLlo World"), SV("hello world")) == true);

	CHECK(ds::strcasecmp(S("HeLl0 World"), S("hell0 world")) == true);
	CHECK(ds::strcasecmp(SV("HeLl0 World"), SV("hell0 world")) == true);

	CHECK(ds::strcasecmp(S("YES"), S("NO")) == false);
	CHECK(ds::strcasecmp(SV("YES"), SV("NO")) == false);

	CHECK(ds::strcasecmp(S("0"), S("1")) == false);
	CHECK(ds::strcasecmp(SV("0"), SV("1")) == false);

	CHECK(ds::strcasecmp(S("10"), S("1")) == false);
	CHECK(ds::strcasecmp(SV("10"), SV("1")) == false);

	CHECK(ds::strcasencmp(S("hello"), S("hello world"), 5) == true);
	CHECK(ds::strcasencmp(SV("hello"), SV("hello world"), 5) == true);

	CHECK(ds::strcasencmp(S("foo"), S("foobar"), 3) == true);
	CHECK(ds::strcasencmp(SV("foo"), SV("foobar"), 3) == true);

	CHECK(ds::strcasencmp(S("foo"), S("foobar"), 4) == false);
	CHECK(ds::strcasencmp(SV("foo"), SV("foobar"), 4) == false);


}

void test_trim()
{
	std::string s;
	std::string_view sv;

	// ltrim
	ds::ltrim(s = "  foo"); CHECK(s == "foo");
	ds::ltrim(s = " \f\n\r\t\vfoo"); CHECK(s == "foo");

	ds::ltrim(sv = "  foo"); CHECK(sv == "foo");
	ds::ltrim(sv = " \f\n\r\t\vfoo"); CHECK(sv == "foo");

	// rtrim
	ds::rtrim(s = "foo  "); CHECK(s == "foo");
	ds::rtrim(s = "foo \f\n\r\t\v"); CHECK(s == "foo");

	ds::rtrim(sv = "foo  "); CHECK(sv == "foo");
	ds::rtrim(sv = "foo \f\n\r\t\v"); CHECK(sv == "foo");

	// trim
	ds::trim(s = "foo"); CHECK(s == "foo");
	ds::trim(s = " \f\n\r\t\vfoo \f\n\r\t\v"); CHECK(s == "foo");

	ds::trim(sv = "foo"); CHECK(sv == "foo");
	ds::trim(sv = " \f\n\r\t\vfoo \f\n\r\t\v"); CHECK(sv == "foo");

	ds::trim(s = "\"hello\" \"world\"", '"'); CHECK(s == "hello\" \"world");
	ds::trim(sv = "\"hello\" \"world\"", '"'); CHECK(sv == "hello\" \"world");

	ds::trim(s = "  \t \n "); CHECK(s.empty());
	ds::trim(sv = "  \t \n "); CHECK(sv.empty());
}

void test_tolower()
{
	std::string s;
	ds::tolower(s = "FLOOF"); 
	CHECK(s == "floof");
	ds::tolower(s = "FlooF"); 
	CHECK(s == "floof");
	ds::tolower(s = "Floof"); 
	CHECK(s == "floof");
	ds::tolower(s = "+-123"); 
	CHECK(s == "+-123");
}

void test_split()
{
	std::string s;
	std::vector<std::string> tokens;

	std::string sv;
	std::vector<std::string> tokensv;

	tokens = ds::split(s = "1 2 3");
	CHECK(tokens.size() == 3 && tokens[0] == "1" && tokens[1] == "2" && tokens[2] == "3");

	tokensv = ds::split(sv = s);
	CHECK(tokensv.size() == 3 && tokensv[0] == "1" && tokensv[1] == "2" && tokensv[2] == "3");


	tokens = ds::split(s = "1.0 2.0 3.0");
	CHECK(tokens.size() == 3 && tokens[0] == "1.0" && tokens[1] == "2.0" && tokens[2] == "3.0");

	tokensv = ds::split(sv = s);
	CHECK(tokensv.size() == 3 && tokensv[0] == "1.0" && tokensv[1] == "2.0" && tokensv[2] == "3.0");


	tokens = ds::split(s = "1,2,3");
	CHECK(tokens.size() == 3 && tokens[0] == "1" && tokens[1] == "2" && tokens[2] == "3");

	tokensv = ds::split(sv = s);
	CHECK(tokensv.size() == 3 && tokensv[0] == "1" && tokensv[1] == "2" && tokensv[2] == "3");


	tokens = ds::split(s = "1, 2, 3");
	CHECK(tokens.size() == 3 && tokens[0] == "1" && tokens[1] == "2" && tokens[2] == "3");

	tokensv = ds::split(sv = s);
	CHECK(tokensv.size() == 3 && tokensv[0] == "1" && tokensv[1] == "2" && tokensv[2] == "3");


	tokens = ds::split(s = "1|2|3");
	CHECK(tokens.size() == 3 && tokens[0] == "1" && tokens[1] == "2" && tokens[2] == "3");

	tokensv = ds::split(sv = s);
	CHECK(tokensv.size() == 3 && tokensv[0] == "1" && tokensv[1] == "2" && tokensv[2] == "3");


	tokens = ds::split(s = "1:2:3");
	CHECK(tokens.size() == 3 && tokens[0] == "1" && tokens[1] == "2" && tokens[2] == "3");

	tokensv = ds::split(sv = s);
	CHECK(tokensv.size() == 3 && tokensv[0] == "1" && tokensv[1] == "2" && tokensv[2] == "3");


	tokens = ds::split(s = "1;2 3");
	CHECK(tokens.size() == 2 && tokens[0] == "1;2" && tokens[1] == "3");

	tokensv = ds::split(sv = "1;2   3");
	CHECK(tokensv.size() == 2 && tokensv[0] == "1;2" && tokensv[1] == "3");
}

void test_number_parsing()
{
	std::string s;
	std::string_view sv;
	
	// bool
	{ 
		{ auto r = ds::parse<bool>(sv = "true"); CHECK(r && (*r == true)); }
		{ auto r = ds::parse<bool>(sv = "True"); CHECK(r && (*r == true)); }
		{ auto r = ds::parse<bool>(sv = "TRUE"); CHECK(r && (*r == true)); }
		{ auto r = ds::parse<bool>(sv = "\t tRuE \n"); CHECK(r && (*r == true)); }
		{ auto r = ds::parse<bool>(sv = "on"); CHECK(r && (*r == true)); }
		{ auto r = ds::parse<bool>(sv = "On"); CHECK(r && (*r == true)); }
		{ auto r = ds::parse<bool>(sv = "ON"); CHECK(r && (*r == true)); }
		{ auto r = ds::parse<bool>(sv = "1"); CHECK(r && (*r == true)); }

		{ auto r = ds::parse<bool>(sv = "false"); CHECK(r && (*r == false)); }
		{ auto r = ds::parse<bool>(sv = "False"); CHECK(r && (*r == false)); }
		{ auto r = ds::parse<bool>(sv = "FALSE"); CHECK(r && (*r == false)); }
		{ auto r = ds::parse<bool>(sv = "\n FaLsE \t"); CHECK(r && (*r == false)); }
		{ auto r = ds::parse<bool>(sv = "off"); CHECK(r && (*r == false)); }
		{ auto r = ds::parse<bool>(sv = "Off"); CHECK(r && (*r == false)); }
		{ auto r = ds::parse<bool>(sv = "OFF"); CHECK(r && (*r == false)); }
		{ auto r = ds::parse<bool>(sv = "0"); CHECK(r && (*r == false)); }

		{ auto r = ds::parse<bool>(sv = "foo"); CHECK(!r); }

		{ auto r = ds::parse<bool>(s = "true"); CHECK(r && (*r == true) && s=="true"); }
		{ auto r = ds::parse<bool>(s = "FALSE"); CHECK(r && (*r == false) && s=="FALSE"); }
	}

	// int
	{
		{ auto r = ds::parse<int>(s = "123"); CHECK(r && (*r == 123)); }
		{ auto r = ds::parse<int>(s = "-123"); CHECK(r && (*r == -123)); }
		{ auto r = ds::parse<int>(s = "+123"); CHECK(r && (*r == 123)); }
		{ auto r = ds::parse<int>(s = " \t 234"); CHECK(r && (*r == 234)); }
		{ auto r = ds::parse<int>(s = "1.25"); CHECK(r && (*r == 1)); }
		{ auto r = ds::parse<int>(s = "-1.25"); CHECK(r && (*r == -1)); }
		{ auto r = ds::parse<int>(s = "0xFF"); CHECK(r && (*r == 0xff)); }
		{ auto r = ds::parse<int>(s = "0xff"); CHECK(r && (*r == 0xff)); }
		{ auto r = ds::parse<int>(s = "a123"); CHECK(!r); }
		{ auto r = ds::parse<int>(s = "123z"); CHECK(r && (*r == 123)); }
		{ auto r = ds::parse<int>(s = std::to_string(std::numeric_limits<int>::min())); CHECK(r && (*r == std::numeric_limits<int>::min())); }
		{ auto r = ds::parse<int>(s = std::to_string(std::numeric_limits<int>::max())); CHECK(r && (*r == std::numeric_limits<int>::max())); }
		{ auto r = ds::parse<int>(s = "2147483648"); CHECK(!r); }
		{ auto r = ds::parse<int>(s = "-2147483649"); CHECK(!r); }

		{ auto r = ds::parse<int>(sv = "123"); CHECK(r && (*r == 123)); }
		{ auto r = ds::parse<int>(sv = "-123"); CHECK(r && (*r == -123)); }
		{ auto r = ds::parse<int>(sv = "+123"); CHECK(r && (*r == 123)); }
		{ auto r = ds::parse<int>(sv = " \t 234"); CHECK(r && (*r == 234)); }
		{ auto r = ds::parse<int>(sv = "1.25"); CHECK(r && (*r == 1)); }
		{ auto r = ds::parse<int>(sv = "-1.25"); CHECK(r && (*r == -1)); }
		{ auto r = ds::parse<int>(sv = "0xFF"); CHECK(r && (*r == 0xff)); }
		{ auto r = ds::parse<int>(sv = "0xff"); CHECK(r && (*r == 0xff)); }
		{ auto r = ds::parse<int>(sv = "a123"); CHECK(!r); }
		{ auto r = ds::parse<int>(sv = "123z"); CHECK(r && (*r == 123)); }
		{ auto r = ds::parse<int>(sv = s = std::to_string(std::numeric_limits<int>::min())); CHECK(r && (*r == std::numeric_limits<int>::min())); }
		{ auto r = ds::parse<int>(sv = s = std::to_string(std::numeric_limits<int>::max())); CHECK(r && (*r == std::numeric_limits<int>::max())); }
		{ auto r = ds::parse<int>(sv = "2147483648"); CHECK(!r); }
		{ auto r = ds::parse<int>(sv = "-2147483649"); CHECK(!r); }
	}

	// float
	{
		{ auto r = ds::parse<float>(s = "foo"); CHECK(!r); }
		{ auto r = ds::parse<float>(s = "foo0.25"); CHECK(!r); }
		{ auto r = ds::parse<float>(s = "1.234foo"); CHECK(r && (*r == 1.234f)); }
		{ auto r = ds::parse<float>(s = "123"); CHECK(r && (*r == 123.f)); }
		{ auto r = ds::parse<float>(s = "-123"); CHECK(r && (*r == -123.f)); }
		{ auto r = ds::parse<float>(s = "+123"); CHECK(r && (*r == 123.f)); }
		{ auto r = ds::parse<float>(s = "+123"); CHECK(r && (*r == 123.f)); }
		{ auto r = ds::parse<float>(s = "0123.0f"); CHECK(r && (*r == 123.f)); }
		{ auto r = ds::parse<float>(s = "-0123.0f"); CHECK(r && (*r == -123.f)); }
		{ auto r = ds::parse<float>(s = "0.25"); CHECK(r && (*r == .25f)); }
		{ auto r = ds::parse<float>(s = "-0.25"); CHECK(r && (*r == -.25f)); }
		{ auto r = ds::parse<float>(s = "+0.25"); CHECK(r && (*r == .25f)); }
		{ auto r = ds::parse<float>(s = ".25"); CHECK(r && (*r == .25f)); }
		{ auto r = ds::parse<float>(s = "1.175494351e-38F"); CHECK(r && (*r == std::numeric_limits<float>::min())); }
		{ auto r = ds::parse<float>(s = "3.402823466e+38F"); CHECK(r && (*r == std::numeric_limits<float>::max())); }
		{ auto r = ds::parse<float>(s = "1.175494351e-40F"); CHECK(r && (std::isnormal(*r) == false)); }
		{ auto r = ds::parse<float>(s = "3.402823466e+40F"); CHECK(!r); }
		{ auto r = ds::parse<float>(s = "inf"); CHECK(r && (r == std::numeric_limits<float>::infinity())); }
		{ auto r = ds::parse<float>(s = "INF"); CHECK(r && (r == std::numeric_limits<float>::infinity())); }
		{ auto r = ds::parse<float>(s = "-inf"); CHECK(r && (r == -std::numeric_limits<float>::infinity())); }
		{ auto r = ds::parse<float>(s = "-INF"); CHECK(r && (r == -std::numeric_limits<float>::infinity())); }
		{ auto r = ds::parse<float>(s = "nan"); CHECK(r && std::isnan(*r)); }
		{ auto r = ds::parse<float>(s = "NAN"); CHECK(r && std::isnan(*r)); }

		{ auto r = ds::parse<float>(sv = "foo"); CHECK(!r); }
		{ auto r = ds::parse<float>(sv = "foo0.25"); CHECK(!r); }
		{ auto r = ds::parse<float>(sv = "1.234foo"); CHECK(r && (*r == 1.234f)); }
		{ auto r = ds::parse<float>(sv = "123"); CHECK(r && (*r == 123.f)); }
		{ auto r = ds::parse<float>(sv = "-123"); CHECK(r && (*r == -123.f)); }
		{ auto r = ds::parse<float>(sv = "+123"); CHECK(r && (*r == 123.f)); }
		{ auto r = ds::parse<float>(sv = "+123"); CHECK(r && (*r == 123.f)); }
		{ auto r = ds::parse<float>(sv = "0123.0f"); CHECK(r && (*r == 123.f)); }
		{ auto r = ds::parse<float>(sv = "-0123.0f"); CHECK(r && (*r == -123.f)); }
		{ auto r = ds::parse<float>(sv = "0.25"); CHECK(r && (*r == .25f)); }
		{ auto r = ds::parse<float>(sv = "-0.25"); CHECK(r && (*r == -.25f)); }
		{ auto r = ds::parse<float>(sv = "+0.25"); CHECK(r && (*r == .25f)); }
		{ auto r = ds::parse<float>(sv = ".25"); CHECK(r && (*r == .25f)); }
		{ auto r = ds::parse<float>(sv = "1.175494351e-38F"); CHECK(r && (*r == std::numeric_limits<float>::min())); }
		{ auto r = ds::parse<float>(sv = "3.402823466e+38F"); CHECK(r && (*r == std::numeric_limits<float>::max())); }
		{ auto r = ds::parse<float>(sv = "1.175494351e-40F"); CHECK(r && (std::isnormal(*r) == false)); }
		{ auto r = ds::parse<float>(sv = "3.402823466e+40F"); CHECK(!r); }
		{ auto r = ds::parse<float>(sv = "inf"); CHECK(r && (r == std::numeric_limits<float>::infinity())); }
		{ auto r = ds::parse<float>(sv = "INF"); CHECK(r && (r == std::numeric_limits<float>::infinity())); }
		{ auto r = ds::parse<float>(sv = "-inf"); CHECK(r && (r == -std::numeric_limits<float>::infinity())); }
		{ auto r = ds::parse<float>(sv = "-INF"); CHECK(r && (r == -std::numeric_limits<float>::infinity())); }
		{ auto r = ds::parse<float>(sv = "nan"); CHECK(r && std::isnan(*r)); }
		{ auto r = ds::parse<float>(sv = "NAN"); CHECK(r && std::isnan(*r)); }
	}

	// int3
	{	
		{ auto r = ds::parse<int3>(s = "1,2,3"); CHECK(r && all(*r==int3(1, 2, 3))); }
		{ auto r = ds::parse<int3>(s = "2 3 4"); CHECK(r && all(*r == int3(2, 3, 4))); }
		{ auto r = ds::parse<int3>(s = "4, 5, 6"); CHECK(r && all(*r == int3(4, 5, 6))); }
		{ auto r = ds::parse<int3>(s = "1, 2 3"); CHECK(r && all(*r == int3(1, 2, 3))); }
		{ auto r = ds::parse<int3>(s = "0.5, 1, 2"); CHECK(r && all(*r == int3(0, 1, 2))); }
		{ auto r = ds::parse<int3>(s = "0.6, 3, 4"); CHECK(r && all(*r == int3(0, 3, 4))); }
		{ auto r = ds::parse<int3>(s = "1, 2, a"); CHECK(!r); }
		{ auto r = ds::parse<int3>(s = "1, 2"); CHECK(!r); }
		{ auto r = ds::parse<int3>(s = "1,, 2"); CHECK(!r); }
		{ auto r = ds::parse<int3>(s = "1, , 2"); CHECK(!r); }
		{ auto r = ds::parse<int3>(s = "1, 2, 3, 4"); CHECK(!r); }
	}

	// float3
	{
		{ auto r = ds::parse<float3>(s = "1.5,2.5,3.5"); CHECK(r && all(*r == float3(1.5f, 2.5f, 3.5f))); }
		{ auto r = ds::parse<float3>(s = "2.2 3.3 4.4"); CHECK(r && all(*r == float3(2.2f, 3.3f, 4.4f))); }
		{ auto r = ds::parse<float3>(s = "4.4, 5.5, 6.6"); CHECK(r && all(*r == float3(4.4f, 5.5f, 6.6f))); }
		{ auto r = ds::parse<float3>(s = "5.1f 5.2f 5.3f"); CHECK(r && all(*r == float3(5.1f, 5.2f, 5.3f))); }
		{ auto r = ds::parse<float3>(s = "0.5, 1., 2"); CHECK(r && all(*r == float3(.5f, 1.f, 2.f))); }
		{ auto r = ds::parse<float3>(s = "1, 2 3"); CHECK(r && all(*r == float3(1.f, 2.f, 3.f))); }
		{ auto r = ds::parse<float3>(s = "1.0, 2, a"); CHECK(!r); }
		{ auto r = ds::parse<float3>(s = "1.0, 2.0"); CHECK(!r); }
		{ auto r = ds::parse<float3>(s = "1.0,,2.0"); CHECK(!r); }
		{ auto r = ds::parse<float3>(s = "1.0, ,2.0"); CHECK(!r); }
		{ auto r = ds::parse<float3>(s = "1, 2, 3, 4"); CHECK(!r); }
		{ auto r = ds::parse<float3>(s = "1.0, 2.0, 3.0, 4.0"); CHECK(!r); }
	}

	// string
	{
		{ auto r = ds::parse<std::string>(s = ""); CHECK(r && r->empty()); }
		{ auto r = ds::parse<std::string>(s = "hello world"); CHECK(r && (*r == "hello world")); }
		{ auto r = ds::parse<std::string>(s = "hello\tworld"); CHECK(r && (*r == "hello\tworld")); }
		{ auto r = ds::parse<std::string>(s = "\"hello world\""); CHECK(r && (*r == "hello world")); }
		{ auto r = ds::parse<std::string>(s = "\"hello\" \"world\""); CHECK(r && (*r == "hello\" \"world")); }

		{ auto r = ds::parse<std::string_view>(s = ""); CHECK(r && r->empty()); }
		{ auto r = ds::parse<std::string_view>(s = "hello world"); CHECK(r && (*r == "hello world")); }
		{ auto r = ds::parse<std::string_view>(s = "hello\tworld"); CHECK(r && (*r == "hello\tworld")); }
		{ auto r = ds::parse<std::string_view>(s = "\"hello world\""); CHECK(r && (*r == "hello world")); }
		{ auto r = ds::parse<std::string_view>(s = "\"hello\" \"world\""); CHECK(r && (*r == "hello\" \"world")); }
	}
}

int main(int, char** argv)
{
	try
	{
		test_strcasecmp();
		test_trim();
		test_tolower();
		test_split();
		test_number_parsing();
	}
	catch (const std::runtime_error & err)
	{
		fprintf(stderr, "%s", err.what());
		return 1;
	}
	return 0;
}
