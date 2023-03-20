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

#include <donut/engine/ConsoleObjects.h>
#include <donut/core/log.h>
#include <donut/tests/utils.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

using namespace donut;
using namespace donut::math;
using namespace donut::engine;

void test_variable_state()
{
	using namespace console;

	VariableState stateA;
	CHECK(stateA.IsInitalized() == false);

	stateA.read_only = true;
	stateA.cheat = false;
	stateA.type = VariableType::TYPE_STRING;
	stateA.setby = VariableState::CONSOLE;
	CHECK(stateA.IsInitalized() == true);
	CHECK(stateA.CanSetValue() == false);

	VariableState stateB(VariableType::TYPE_STRING, VariableState::CONSOLE);
	CHECK(stateB.IsInitalized() == true);
	CHECK(stateB.CanSetValue() == true);
	CHECK(stateB.setby == console::VariableState::CONSOLE);
	
	stateB.setby = VariableState::CODE;
	stateB.cheat = true;
	CHECK(stateB.CanSetValue(VariableState::CONSOLE) == false);

	VariableState stateC = stateB;
	CHECK(stateC != stateA);
	CHECK(stateC == stateB);
}

void test_console_variables()
{
	console::ResetAll();

	static char const* name = "float3cvar";
	static char const* description = "The description of float3cvar is very descriptive.";
	float3 value(.1f, .2f, .3f), 
		   value2(.4f, .5f, .6f),
		   value3(.7f, .8f, .9f);

	// auto-instantiation
	cvarFloat3 var = cvarFloat3(name, description, value);
	CHECK(all(var.GetValue() == value));
	CHECK(all(var.GetValue() != value2));
	CHECK(std::strcmp(var.GetName().c_str(), name)==0);
	CHECK(std::strcmp(var.GetDescription().c_str(), description)==0);

	console::VariableState state = var.GetState();
	CHECK(state.IsInitalized()==true);
	CHECK(state.setby == console::VariableState::CODE);
	CHECK(state.type == console::VariableType::TYPE_FLOAT3);
	
	// reference copy
	cvarFloat3 varcopy = var;
	CHECK(all(varcopy.GetValue() == value));
	CHECK(std::strcmp(varcopy.GetName().c_str(), name) == 0);
	CHECK(std::strcmp(varcopy.GetDescription().c_str(), description) == 0);
	CHECK(state == varcopy.GetState());

	// set value
	CHECK(var.GetState().CanSetValue());
	var.SetValue(value2);
	CHECK(all(var.GetValue() == value2));
	CHECK(all(varcopy.GetValue() == value2));

	// find
	CHECK(console::FindVariable("foo") == nullptr);
	if (cvar* pvar = console::FindVariable(name))
	{
		CHECK(pvar->IsBool() == false);
		CHECK(pvar->IsFloat3() == true);
		CHECK(pvar->IsString() == false);
		CHECK(all(pvar->GetFloat3() == value2));
		CHECK(strcmp(pvar->GetName().c_str(), name) == 0);
		CHECK(std::strcmp(pvar->GetDescription().c_str(), description) == 0);
		CHECK(pvar->GetState() == state);

		pvar->SetFloat3(value);
		CHECK(all(pvar->GetFloat3() == value));
		CHECK(pvar->GetValueAsString() == "0.1 0.2 0.3");

		// read-only
		std::pair<donut::log::Severity, std::string> err;
		donut::log::SetCallback([&](donut::log::Severity s, char const* msg) {
			err = { s, msg };
			});

		CHECK(pvar->GetState().read_only == false);
		pvar->SetReadOnly(true);
		CHECK(pvar->GetState().read_only == true);
		pvar->SetFloat3(value2, console::VariableState::CONSOLE);
		CHECK(all(pvar->GetFloat3() == value));
		CHECK((err.first == donut::log::Severity::Error) && (!err.second.empty()))
		pvar->SetReadOnly(false);
		CHECK(pvar->GetState().read_only == false);
	}
	else
		CHECK(false);

	// callbacks
	bool callbackHasRun = false;
	var.SetOnChangeCallback([&](cvar& v) -> void {
		CHECK(v.GetName() == name);
		CHECK(v.GetDescription() == description);
		CHECK(all(v.GetFloat3()==value3));
		CHECK(v.GetState() == state);
		callbackHasRun = true;
		});

	var.SetValue(value3);
	CHECK(callbackHasRun == true);
}

static std::string readIniFile(std::filesystem::path const& fpath)
{
	std::ifstream infile(fpath);
	std::stringstream str;
	str << infile.rdbuf();
	return str.str();
}

void test_ini()
{
	//std::filesystem::path rpath(DONUT_TEST_SOURCE_DIR);
	//std::string ini = readInitFile(rpath / "src/engine/foo.ini")

	std::string ini =
		"# this is a comment\n"
		"	# this is another comment\n"
		"\n"
		"	fooBool1 = true\n"
		"	fooBool2 = false\n"
		"	fooBool3 = 0\n"
		"	fooBool4 = ON\n"
		"  \n"
		"	fooInt1 = 42\n"
		"	fooInt1 = 43\n"
		"	fooInt2 = blarg\n"
		"	fooInt3 = 23basd!@df22\n"
		"\n"
		"	fooInt31 = 1, 2, 3\n"
		"	fooInt32 = 1, 2, 3\n"
		"\n"
		"	fooFloat1 = 0.5f\n"
		"	fooFloat2 = 23.0f\n"
		"	fooFloat3 = 45\n"
		""
		"	fooFloat31 = 0.f, 0.5f, 0.8f\n"
		"\n"
		"	fooString1 = hello world\n"
		"	fooString2 = \"hello world\"\n";

	// catch donut::logs 
	std::vector<std::pair<donut::log::Severity, std::string>> errors;
	donut::log::SetCallback([&](donut::log::Severity s, char const* msg) -> void {
		errors.push_back({s, std::string(msg)});
		});

	console::ResetAll();

	cvarBool fooBool1("fooBool1", "foo bool var 1", false);
	cvarBool fooBool2("fooBool2", "foo bool var 2", false);
	cvarBool fooBool3("fooBool3", "foo bool var 3", false);
	cvarBool fooBool4("fooBool4", "foo bool var 3", false);

	cvarInt fooInt1("fooInt1", "foo int var 1", 0);
	cvarInt fooInt2("fooInt2", "foo int var 1", 666);
	cvarInt fooInt3("fooInt3", "foo int var 1", 0);

	cvarFloat fooFloat1("fooFloat1", "foo float var 1", 0.f);
	cvarFloat fooFloat2("fooFloat2", "foo float var 2", 0.f);
	cvarFloat fooFloat3("fooFloat3", "foo float var 3", 0.f);

	cvarInt3 fooInt31("fooInt31", "foo int var 1", int3());
	cvarInt3 fooInt32("fooInt32", "foo int var 2", int3());
	cvarInt3 fooInt33("fooInt33", "foo int var 3", int3());

	cvarFloat3 fooFloat31("fooFloat31", "foo float3 var 1", int3(0, 0, 0));

	cvarString fooString1("fooString1", "foo string var 1", "");
	cvarString fooString2("fooString2", "foo string var 2", "nom nom nom");

	console::ParseIniFile(ini.c_str(), "foo.ini");

	CHECK(fooBool1.GetValue() == true);
	CHECK((&fooBool1)->GetValueAsString() == "true");
	CHECK(fooBool1.GetState().IsInitalized() == true);
	CHECK(fooBool1.GetState().CanSetValue(console::VariableState::CODE) == true);
	CHECK(fooBool1.GetState().CanSetValue(console::VariableState::INI) == true);
	CHECK(fooBool1.GetState().CanSetValue(console::VariableState::CONSOLE) == true);
	CHECK(strcmp(fooBool1.GetName().c_str(), "fooBool1") == 0);
	CHECK(strcmp(fooBool1.GetDescription().c_str(), "foo bool var 1") == 0);

	CHECK(fooBool2.GetValue() == false);
	CHECK((&fooBool2)->GetValueAsString() == "false");
	CHECK(fooBool3.GetValue() == false);
	CHECK((&fooBool3)->GetValueAsString() == "false");
	CHECK(fooBool4.GetValue() == true);
	CHECK((&fooBool4)->GetValueAsString() == "true");

	CHECK(fooInt1.GetValue() == 43);
	CHECK((&fooInt1)->GetValueAsString() == "43");
	CHECK(fooInt2.GetValue() == 666);
	CHECK((&fooInt2)->GetValueAsString() == "666");
	CHECK(fooInt3.GetValue() == 23);
	CHECK((&fooInt3)->GetValueAsString() == "23");

	CHECK(fooFloat1.GetValue() == 0.5f);
	CHECK((&fooFloat1)->GetValueAsString() == "0.5");
	CHECK(fooFloat2.GetValue() == 23.f);
	CHECK((&fooFloat2)->GetValueAsString() == "23");
	CHECK(fooFloat3.GetValue() == 45);
	CHECK((&fooFloat3)->GetValueAsString() == "45");

	CHECK(all(fooInt31.GetValue() == int3(1, 2, 3)));
	CHECK((&fooInt31)->GetValueAsString() == "1 2 3");
	CHECK(all(fooInt32.GetValue() == int3(1, 2, 3)));

	CHECK(all(fooFloat31.GetValue() == float3(0.f, .5f, .8f)));
	CHECK((&fooFloat31)->GetValueAsString() == "0 0.5 0.8");

	CHECK(fooString1.GetValue() == "hello world");
	CHECK((&fooString1)->GetValueAsString() == "hello world");
	CHECK(fooString2.GetValue() == "hello world");

	CHECK(errors.size()==2);
	CHECK(errors[0].first == donut::log::Severity::Error);
	CHECK(errors[1].first == donut::log::Severity::Error);
	CHECK(errors[1].second == "foo.ini:10 parse error : cannot set value for variable 'fooInt2'");
}

int main(int, char** argv)
{
	try
	{
		test_variable_state();
		test_console_variables();
		test_ini();
	}
	catch (const std::runtime_error & err)
	{
		fprintf(stderr, "%s", err.what());
		return 1;
	}
	return 0;
}
