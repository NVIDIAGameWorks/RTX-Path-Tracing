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

#include <donut/engine/ConsoleInterpreter.h>
#include <donut/engine/COnsoleObjects.h>
#include <donut/core/string_utils.h>
#include <donut/core/log.h>
#include <donut/tests/utils.h>

using namespace donut;
using namespace donut::math;
using namespace donut::engine;

std::vector<std::pair<donut::log::Severity, std::string>> logs;

typedef console::Command::Args Args;
typedef console::Command::Result Result;

Result hello_cmd(Args args)
{
	return { true, "hello world" };
}

Result add_cmd(Args args)
{
	if (args.size() != 3)
		return Result();

	int a = 0, b = 0;
	if (auto v = ds::parse<int>(args[1]))
		a = *v;
	if (auto v = ds::parse<int>(args[2]))
		b = *v;

	return { true, std::to_string(a + b) };
}

void test_commands()
{
	logs.clear();

	console::Interpreter interpreter;

	{
		auto [s, o] = interpreter.Execute("help");
		CHECK(logs.empty() && (s == true) && (!o.empty()));
	}
	{
		auto [s, o] = interpreter.Execute("help me");
		CHECK(logs.empty() && (s == false) && (o == "no console object with name 'me' found"));
	}

	{
		bool b = console::RegisterCommand({ "add", "it adds numbers", add_cmd });
		CHECK(logs.empty() && (b==true));
	}

	{
		auto [s, o] = interpreter.Execute("add 2 3");
		CHECK(logs.empty() && (s == true) && (o == "5"));
	}
	{
		auto [s, o] = interpreter.Execute("help add");
		CHECK(logs.empty() && (s == true) && (o == "it adds numbers"));
	}

	{
		bool b = console::RegisterCommand({ "hello", "returns \"hello world\" string", hello_cmd });
		CHECK(logs.empty() && (b == true));
	}

	// suggestions
	{
		auto sugs = interpreter.Suggest("", 0);
		CHECK((logs.size() == 0) && sugs.empty());
	}
	{
		auto sugs = interpreter.Suggest("he", 1);
		CHECK((logs.size() == 0) && (sugs.size() == 2) && (sugs[0] == "hello") && (sugs[1] == "help"));
	}
}

void test_variables()
{
	logs.clear();

	console::Interpreter interpreter;

	cvarInt myint("myint", "just a random int", 55);

	{
		auto [s, o] = interpreter.Execute("myint");
		CHECK(logs.empty() && (s == true) && (o == "55"));
	}
	{
		auto [s, o] = interpreter.Execute("myint 99");
		CHECK(logs.empty() && (s == true) && o.empty() &&(myint.GetValue()==99));
	}
	{
		auto [s, o] = interpreter.Execute("myfloat 0.5");
		CHECK((logs.size() == 1) && (logs[0].second == "no console object with name 'myfloat' found") && (s == false) && o.empty());
		logs.clear();
	}

	cvarFloat myfloat("myfloat", "just a random float", -.555f, /*read only*/true);
	{
		auto [s, o] = interpreter.Execute("myfloat");
		CHECK(logs.empty() && (s == true) && (o == "-0.555"));
	}
	{
		auto [s, o] = interpreter.Execute("help myfloat");
		CHECK(logs.empty() && (s == true) && (o == "just a random float"));
	}
	{
		auto [s, o] = interpreter.Execute("myfloat 1.25"); // attempt to write read-only variable
		CHECK((logs.size() == 1) && (s == false) && o.empty());
		logs.clear();
	}
}

int main(int, char** argv)
{	
	// catch donut::logs 
	donut::log::SetCallback([](donut::log::Severity s, char const* msg) -> void {
			logs.push_back({ s, std::string(msg) });
		});

	try
	{
		test_commands();
		test_variables();
	}
	catch (const std::runtime_error & err)
	{
		fprintf(stderr, "%s", err.what());
		return 1;
	}
	return 0;
}
