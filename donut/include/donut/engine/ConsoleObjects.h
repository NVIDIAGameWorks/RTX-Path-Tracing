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

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <string.h>

namespace donut::engine
{

	namespace console {

		class Command;
		class Variable;
		template <typename T> class AutoVariable;

		//
		// Base Console Object
		//

		class Object
		{
		public:

			virtual ~Object() {}

			std::string const& GetName() const;

			std::string const& GetDescription() const { return m_Description; }
			void SetDescription(std::string const& description) { m_Description = description; }

			virtual Variable* AsVariable() { return nullptr; }
			virtual Command* AsCommand() { return nullptr; }

		protected:
			friend class ObjectDictionary;
			
			Object(char const* description) : m_Description(description) { }

			std::string m_Description;
		};

		//
		// Console Commands
		//

		class Command : public Object
		{
		public:

			virtual Command* AsCommand() override { return this; }

			// execution callback

			struct Result
			{
				bool status = false;
				std::string output;
			};

			typedef std::vector<std::string> Args;
			typedef std::function<Result(Args const& args)> OnExecuteFunction;

			Result Execute(Args const& args);

			// optional callback to suggest argument values

			typedef std::function<std::vector<std::string>(std::string_view const cmdline, size_t cursor_pos)> OnSuggestFunction;

			std::vector<std::string> Suggest(std::string_view const cmdline, size_t cursor_pos);

		private:

			friend class ObjectDictionary;

			Command(char const* description, OnExecuteFunction on_exec, OnSuggestFunction on_suggest);

			OnExecuteFunction m_OnExecute;
			OnSuggestFunction m_OnSuggest;
		};

		//
		// Console Variables
		// 
		// Console variables are unique typed data elements associated to a given name.
		// Two common usage patterns:
		//
		//   - Static mode: the type of the variable is known and template traits
		//     specialization can be used to automatically access the data. This mode
		//     is implemented with the "AutoVariable" class. AutoVariables can be instanciated
		//     directly in code, typically as global/static variables. They are strong-typed,
		//     lightweight, incur negligible performance penalty, and can be freely copied.
		//
		//     ex:
		//        cvarInt var = AutoVariable("myvar", "my variable description", 123);
		//        int i = var.GetValue();
		//     
		//     Convenience conversion operators are defined so that variables can be 
		//     used as if they were values:
		//        cvarInt var = ...;
		//        int i = var;
		//        var = 5;
		//
		//   - Dynamic mode: the type of the variable is not know to the code, so
		//     type-casting is implemented as an interface of virtual functions. The
		//     typical use case is the implementation of a console interpreter and any
		//     other run-time or user-driven access. This mode is implemented with the
		//     "Variable" class.
		//
		//     ex:
		//         if (cvar* var = FindVariable("myvar"))
		//             if (var->IsInt())
		//                 int i = var->GetInt();
		//     note: some accessors have specializations to cast between types
		//     (ex. GetString() on a bool typed variable returns "true" or "false" strings)
		//
		
		struct VariableType
		{
			enum Type : uint8_t {
				TYPE_UNKNOWN = 0,
				TYPE_BOOL,
				TYPE_INT,
				TYPE_INT2,
				TYPE_INT3,
				TYPE_FLOAT,
				TYPE_FLOAT2,
				TYPE_FLOAT3,
				TYPE_FLOAT4,
				TYPE_STRING
			};
			template <typename T> static Type IsA() { return TYPE_UNKNOWN; }
		};

		struct VariableState
		{
			// Tracks where the origin of the most recent change to the
			// value of a console variable.
			enum SetBy : uint8_t
			{
				UNSET = 0,
				CODE,
				INI,
				CONSOLE
			};

			// XXXX mk: C++ 20 has in-line initialization for bit-fields...
			VariableState() { memset(this, 0, sizeof(VariableState)); } 
			VariableState(VariableType::Type type, SetBy setby) : read_only(false), cheat(false), type(type), setby(setby) {}
			VariableState(bool ronly, bool cheat, VariableType::Type type, SetBy setby) : read_only(ronly), cheat(cheat), type(type), setby(setby) {}

			bool operator == (VariableState const& other) const { return *((uint32_t const*)this) == *((uint32_t const*)(&other)); }
			bool operator != (VariableState const& other) const { return !(*this==other); }

			bool IsInitalized() const;

			// Returns true if the setter is allowed to modify the value.
			// note: if the 'cheat' state is true, the variable can be initialized from 'CODE',
			// but it cannot be modified from either the 'CONSOLE' or 'INI' sources
			bool CanSetValue(SetBy origin = CONSOLE) const;

			uint32_t read_only : 1;
			uint32_t cheat     : 1;
			uint32_t type      : 5;
			uint32_t setby     : 2;
		};

		class Variable : public Object
		{
		public:

			// state

			typedef VariableState::SetBy SetBy;

			VariableState GetState() const { return m_State; }

			void SetReadOnly(bool ronly) { m_State.read_only = ronly; }

			void SetCheat() { m_State.cheat = true; }	

		public:

			// Value accessors for each type. Ex.
			//     bool IsInt() const;
			//     int GetInt() const;
			//     void SetInt(int value, SetBy setby=SETBY_CODE);

#define DEFINE_TYPED_ACCESSORS(name, type) \
	virtual bool Is##name() const = 0;   \
	virtual type Get##name() const = 0;  \
	virtual void Set##name(type value, SetBy setby=SetBy::CODE) = 0;

#define DEFINE_TYPED_REF_ACCESSORS(name, type)   \
	virtual bool Is##name() const = 0;         \
	virtual type const& Get##name() const = 0; \
	virtual void Set##name(type const& value, SetBy setby=SetBy::CODE) = 0;

			DEFINE_TYPED_ACCESSORS(Bool, bool);

			DEFINE_TYPED_ACCESSORS(Int, int);
			DEFINE_TYPED_ACCESSORS(Int2, dm::int2);
			DEFINE_TYPED_ACCESSORS(Int3, dm::int3);
			
			DEFINE_TYPED_ACCESSORS(Float, float);
			DEFINE_TYPED_ACCESSORS(Float2, dm::float2);
			DEFINE_TYPED_ACCESSORS(Float3, dm::float3);
			DEFINE_TYPED_ACCESSORS(Float4, dm::float4);

			DEFINE_TYPED_REF_ACCESSORS(String, std::string);

			// attenmpt to parse value from string
			virtual bool SetValueFromString(std::string_view s, SetBy setby = SetBy::CODE) = 0;
			virtual bool SetValueFromString(std::string const& s, SetBy setby = SetBy::CODE) = 0;

			virtual std::string GetValueAsString() const = 0;

		public:

			// callback

			typedef std::function<void(Variable & cvar)> Callback;
			
			void SetOnChangeCallback(Callback onChange);

			void ExecuteOnChangeCallback();

		protected:

			friend class ObjectDictionary;

			Variable(char const* description, VariableState state) : Object(description), m_State(state) {}

			Callback m_OnChange;
			VariableState m_State;
		};

		template <typename TVar> class VariableImpl;

		template <typename T> class AutoVariable
		{
		public:
			
			// note: registering a console object with null or empty name string will result in fatal error
			AutoVariable(char const* name, char const* description, T const& defaultValue, bool read_only=false, bool cheat=false);

			std::string const& GetName() const;

			void SetDescription(std::string const& description);

			std::string const& GetDescription() const;

			VariableState GetState() const;

			T GetValue() const;

			void SetValue(T const& value);

			void SetOnChangeCallback(Variable::Callback onChange);

			void ExecuteOnChangeCallback();

			Variable* operator & ();

			operator T() const;

			AutoVariable<T>& operator =(const T& value);

		private:
			friend class VariableImpl<T>;
			
			VariableImpl<T>& m_Variable;
		};

		//
		// Object functions
		//

		struct CommandDesc
		{
			char const* name = nullptr;
			char const* description = nullptr;
			Command::OnExecuteFunction on_execute;
			Command::OnSuggestFunction on_suggest;
		};

		bool RegisterCommand(CommandDesc const& desc);

		Object* FindObject(std::string_view name);

		std::vector<std::string_view> MatchObjectNames(char const* regex = ".*");

		std::vector<Object*> MatchObjects(char const* regex = ".*");

		Command* FindCommand(std::string_view name);

		Variable* FindVariable(std::string_view name);
		
		// note: ini files can only modify values of existing consolve variables
		void ParseIniFile(char const* inidata, char const* filename);

		// nuclear option: removes all console objects from dictionary
		void ResetAll(); 

	} // end namespace console

	typedef console::Variable cvar;

	typedef console::AutoVariable<bool> cvarBool;
	typedef console::AutoVariable<int> cvarInt;
	typedef console::AutoVariable<float> cvarFloat;
	typedef console::AutoVariable<dm::int2> cvarInt2;
	typedef console::AutoVariable<dm::int3> cvarInt3;
	typedef console::AutoVariable<dm::uint2> cvarUint2;
	typedef console::AutoVariable<dm::uint3> cvarUint3;
	typedef console::AutoVariable<dm::float2> cvarFloat2;
	typedef console::AutoVariable<dm::float3> cvarFloat3;
	typedef console::AutoVariable<dm::float4> cvarFloat4;
	typedef console::AutoVariable<std::string> cvarString;

} // end namespace donut::engine