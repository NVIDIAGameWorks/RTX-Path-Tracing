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
#include <donut/core/string_utils.h>

#include <cassert>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <cstring>

using namespace donut::math;

namespace donut::engine::console
{

	static std::string const emptyString;

	// helper : compile regex safely & catch user errors
	inline std::optional<std::regex> regex_from_char(char const* s)
	{
		std::regex rx;
		if (s)
		{
			try { rx = s; }
			catch (std::regex_error const& err)
			{
				donut::log::error(err.what());
				return std::nullopt;
			}
		}
		return rx;
	}

	// Variable types conversions
	template <> inline VariableType::Type VariableType::IsA<bool>() { return TYPE_BOOL; }
	template <> inline VariableType::Type VariableType::IsA<int>() { return TYPE_INT; }
	template <> inline VariableType::Type VariableType::IsA<float>() { return TYPE_FLOAT; }
	template <> inline VariableType::Type VariableType::IsA<dm::int2>() { return TYPE_INT2; }
	template <> inline VariableType::Type VariableType::IsA<dm::int3>() { return TYPE_INT3; }
	template <> inline VariableType::Type VariableType::IsA<dm::float2>() { return TYPE_FLOAT2; }
	template <> inline VariableType::Type VariableType::IsA<dm::float3>() { return TYPE_FLOAT3; }
	template <> inline VariableType::Type VariableType::IsA<dm::float4>() { return TYPE_FLOAT4; }
	template <> inline VariableType::Type VariableType::IsA<std::string>() { return TYPE_STRING; }

	static char const* AsString(VariableType::Type type)
	{
		switch (type)
		{
		case VariableType::TYPE_BOOL: return "bool";
		case VariableType::TYPE_INT: return "int";
		case VariableType::TYPE_INT2: return "int2";
		case VariableType::TYPE_INT3: return "int3";
		case VariableType::TYPE_FLOAT: return "float";
		case VariableType::TYPE_FLOAT2: return "float2";
		case VariableType::TYPE_FLOAT3: return "float3";
		case VariableType::TYPE_FLOAT4: return "float4";
		case VariableType::TYPE_STRING: return "string";
		default: 
			return "unknown";
		}
	}

	static char const* AsString(VariableState::SetBy setby)
	{
		switch (setby)
		{
		case VariableState::CODE: return "CODE";
		case VariableState::CONSOLE: return "CONSOLE";
		case VariableState::INI: return "INI";
		default:
			return "UNSET";
		}
	}

	bool VariableState::IsInitalized() const
	{
		return type != VariableType::TYPE_UNKNOWN && setby != UNSET;
	}

	bool VariableState::CanSetValue(SetBy origin) const
	{
		if (!IsInitalized() || read_only)
			return false;
		if (cheat && (setby <= CODE) && (origin > CODE))
			return false;
		return true;
	}
	
	//
	// Console Object Dictionary
	//

	class ObjectDictionary
	{
	public:
		typedef VariableState::SetBy SetBy;

		static inline bool IsValidName(char const* name)
		{
			return (name && (std::strlen(name) > 0)) ? true : false;
		}

		Object* RegisterCommand(CommandDesc const& desc)
		{
			if (IsValidName(desc.name))
			{
				if (!desc.on_execute)
				{
					donut::log::fatal("attempting to register console command '%s' with no execution function", desc.name);
				}

				std::lock_guard<std::mutex> lock(m_Mutex);
				if (auto it = m_Dictionary.find(desc.name); it == m_Dictionary.end())
				{
					auto* cmd = new Command(desc.description, desc.on_execute, desc.on_suggest);
					m_Dictionary[desc.name] = cmd;
					return cmd;
				}
				else
					donut::log::error("console command with name '%s' already exists", desc.name);
			}
			else
				donut::log::error("attempting to register a console command with invalid name '%s'", desc.name);
			return nullptr;
		}

		bool UnregisterCommand(std::string_view name)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (auto it = m_Dictionary.find(name); it != m_Dictionary.end())
			{
				if (it->second->AsCommand())
				{
					m_Dictionary.erase(it);
					return true;
				}
				else
					donut::log::error("unregister command '%s'; object is not a console command");
			}
			else
				donut::log::error("unregister command '%s'; command does not exist", name);
			return false;
		}

		template <typename T> Object* RegisterVariable(char const* name, char const* description, T const& value, VariableState state)
		{
			// Static registration (from code) cannot return nullptr and therefore has to trigger a fatal errors.
			// Dynamic registration (from .ini or console) reports errors, but can return nullptr
			auto handleError = [&](char const* message) -> void {
				if (state.setby > SetBy::CODE)
					donut::log::error(message, name);
				else
					donut::log::fatal(message, name);
			};

			if (IsValidName(name))
			{
				std::lock_guard<std::mutex> lock(m_Mutex);				
				if (auto it = m_Dictionary.find(name); it != m_Dictionary.end())
				{
					if (VariableImpl<T>* cvar = (VariableImpl<T>*)it->second->AsVariable())
					{
						if (cvar->GetState().type == VariableType::IsA<T>())
						{
							// cvar may have been referenced elsewhere but not be initialized yet
							if (cvar->m_Description.empty() && IsValidName(description))
								cvar->m_Description = description;

							// override the value
							cvar->SetData(value, (SetBy)state.setby);

							return cvar;
						}
						else
							handleError("console variable '%s' already exists but is a different type");
					}
					else
						handleError("console variable with name '%s' already exists");
				}
				else
				{
					assert(state.type == VariableType::IsA<T>());
					state.type = VariableType::IsA<T>(); // force type to be correct

					VariableImpl<T>* cvar = new VariableImpl<T>(value, description, state);
					m_Dictionary[name] = cvar;					
					return cvar;
				}
			}
			else
				handleError("attempting to register a console variable with invalid name '%s'");
			return nullptr;
		}

		Object* FindObject(std::string_view name)
		{
			if (!name.empty())
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				auto it = m_Dictionary.find(name);
				if (it != m_Dictionary.end())
					return it->second;
			}
			return nullptr;
		}

		std::vector<std::string_view> FindObjectNames(char const* regex)
		{
			std::vector<std::string_view> matches;
			if (auto rx = regex_from_char(regex))
			{
				for (auto& it : m_Dictionary)
					if (std::regex_match(it.first, *rx))
						matches.push_back(std::string_view(it.first));
			}
			return matches;
		}

		std::vector<Object*> FindObjects(char const* regex)
		{
			std::vector<Object*> matches;
			if (auto rx = regex_from_char(regex))
			{
				for (auto& it : m_Dictionary)
					if (std::regex_match(it.first, *rx))
						matches.push_back(it.second);
			}
			return matches;
		}

		std::string const& GetObjectName(Object const* cobj)
		{
			// slow linear search under the assumption that this is only called very rarely
			if (cobj)
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				for (auto const& it : m_Dictionary)
				{
					if (it.second == cobj)
						return it.first;
				}
				donut::log::error("unregistered object");
			}
			return emptyString;
		}

		void Reset()
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Dictionary.clear();
		}

	private:

		// note: the dictionary deliberately leaks its ConsoleObjects* in order to
		// guarantee that any reference to the memory will still be valid when the
		// application shuts down and implicit destructors are invoked. The "correct"
		// implementation would to own lifespan with shared/weak_ptr, but this adds
		// a lot of atomic & error checking burdens which were not deemed to be worth it.
		std::mutex m_Mutex;
		std::map<std::string, Object*, std::less<>> m_Dictionary;

	} objectsDictionary;

	//
	// Implementation
	//


	//
	// Console Object
	//

	std::string const& Object::GetName() const
	{
		return objectsDictionary.GetObjectName(this);
	}

	//
	// Console Command
	//

	Command::Command(char const* description, OnExecuteFunction onexec, OnSuggestFunction onsuggest)
		: Object(description), m_OnExecute(onexec), m_OnSuggest(onsuggest)
	{ }

	Command::Result Command::Execute(Args const& args)
	{
		if (m_OnExecute)
			return m_OnExecute(args);
		else
			donut::log::error("console command '%s' has no function", this->GetName().c_str());
		return Result();
	}

	std::vector<std::string> Command::Suggest(std::string_view cmdline, size_t cursor_pos)
	{
		if (m_OnSuggest)
			return m_OnSuggest(cmdline, cursor_pos);
		else
			return {};
	}

	//
	// Console Variable
	//

	void Variable::SetOnChangeCallback(Callback onChange)
	{
		m_OnChange = onChange;
	}

	void Variable::ExecuteOnChangeCallback()
	{
		if (m_OnChange)
			m_OnChange(*this);
		else
			donut::log::error("no callback set for CVar '%s'", this->GetName().c_str());
	}

	//
	// Console Variable Implementation
	//

    template <typename T> class AutoVariable;

	template <typename T> class VariableImpl : public Variable
	{
	public:

		VariableImpl(T const& data, char const* description, VariableState state) 
			: Variable(description, state), m_Data(data) { }

		virtual Variable* AsVariable() override { return this; }

		inline T GetData() const { return m_Data; }

		inline T const& GetDataRef() const { return m_Data; }

		inline bool SetData(T const& value, SetBy setby)
		{
			VariableState flags = this->GetState();
			if (flags.CanSetValue(setby))
			{
				m_Data = value;
				this->m_State.setby = setby;

				if (m_OnChange)
					m_OnChange(*this);
				return true;
			}
			else
			{
				VariableState state = this->GetState();
				if (state.read_only)
					donut::log::error("cvar '%s' is read-only - value not set", this->GetName().c_str());
				else
					donut::log::error("cvar '%s' not enough privilege with '%s' to override '%s' - value not set",
						this->GetName().c_str(), AsString(setby), AsString((SetBy)state.setby));
			}
			return false;
		}

		virtual bool SetValueFromString(std::string_view s, SetBy setby) override
		{
			if (!s.empty())
				if (auto value = ds::parse<T>(s))
					return this->SetData(*value, setby);

			donut::log::error("cvar '%s' failed parsing value string '%s' (expected a %s) - value not set",
				this->GetName().c_str(), std::string(s).c_str(), AsString((VariableType::Type)m_State.type));
			return false;
		}

		virtual bool SetValueFromString(std::string const& s, SetBy setby) override
		{
			return SetValueFromString(std::string_view(s), setby);
		}

		virtual std::string GetValueAsString() const override
		{
			char buff[16] = { 0 };
			if (auto [p, ec] = std::to_chars(buff, buff+16, m_Data); ec == std::errc())
				return std::string(buff);
			return std::string();
		}

		// default accessors

		#define DEFINE_TYPED_ACCESSORS_IMPLEMENTATION(name, type) \
			virtual bool Is##name() const override { return false; } \
			virtual type Get##name() const override { \
				donut::log::error("cvar '%s' is not a "#type" (cannot get)", this->GetName().c_str()); \
				return type(); \
			} \
			virtual void Set##name(type value, SetBy) override { \
				donut::log::error("cvar '%s' is not a "#type" (cannot set)", this->GetName().c_str()); \
			}

		DEFINE_TYPED_ACCESSORS_IMPLEMENTATION(Bool, bool);
		DEFINE_TYPED_ACCESSORS_IMPLEMENTATION(Int, int);
		DEFINE_TYPED_ACCESSORS_IMPLEMENTATION(Int2, int2);
		DEFINE_TYPED_ACCESSORS_IMPLEMENTATION(Int3, int3);
		DEFINE_TYPED_ACCESSORS_IMPLEMENTATION(Float, float);
		DEFINE_TYPED_ACCESSORS_IMPLEMENTATION(Float2, float2);
		DEFINE_TYPED_ACCESSORS_IMPLEMENTATION(Float3, float3);
		DEFINE_TYPED_ACCESSORS_IMPLEMENTATION(Float4, float4);


		#define DEFINE_TYPED_REF_ACCESSORS_IMPLEMENTATION(name, type) \
			virtual bool Is##name() const override { return false; } \
			virtual type const& Get##name() const override { \
				donut::log::error("cvar '%s' is not a "#type" (cannot get)", this->GetName().c_str()); \
				return emptyString; \
			} \
			virtual void Set##name(type const& value, SetBy) override { \
				donut::log::error("cvar '%s' is not a "#type" (cannot set)", this->GetName().c_str()); \
			}

		DEFINE_TYPED_REF_ACCESSORS_IMPLEMENTATION(String, std::string);

	private:
		friend class AutoVariable<T>;

		T m_Data;
	};

	// specialisations

	template<> bool VariableImpl<bool>::IsBool() const { return true; }
	template<> bool VariableImpl<int>::IsInt() const { return true; }
	template<> bool VariableImpl<int2>::IsInt() const { return true; }
	template<> bool VariableImpl<int3>::IsInt() const { return true; }
	template<> bool VariableImpl<float>::IsFloat() const { return true; }
	template<> bool VariableImpl<float2>::IsFloat2() const { return true; }
	template<> bool VariableImpl<float3>::IsFloat3() const { return true; }
	template<> bool VariableImpl<float4>::IsFloat4() const { return true; }
	template<> bool VariableImpl<std::string>::IsString() const { return true; }

	template<> bool VariableImpl<bool>::GetBool() const { return GetData(); }
	template<> int VariableImpl<bool>::GetInt() const { return GetData()==true ? 1 : 0; }
	template<> float VariableImpl<bool>::GetFloat() const { return GetData()==true ? 1.f : 0.f; }
	template<> std::string const& VariableImpl<bool>::GetString() const
	{
		static const std::string _true = "true", _false = "false";
		return GetData()==true ? _true : _false;
	}

	template<> bool VariableImpl<int>::GetBool() const { return GetData() != 0; }
	template<> int VariableImpl<int>::GetInt() const { return GetData(); }
	template<> float VariableImpl<int>::GetFloat() const { return (float)GetData(); }

	template<> int2 VariableImpl<int2>::GetInt2() const { return GetData(); }
	template<> int3 VariableImpl<int3>::GetInt3() const { return GetData(); }

	template<> bool VariableImpl<float>::GetBool() const { return GetData() != 0.f; }
	template<> int VariableImpl<float> ::GetInt() const { return (int)GetData(); }
	template<> float VariableImpl<float>::GetFloat() const { return GetData(); }

	template<> float2 VariableImpl<float2>::GetFloat2() const { return GetData(); }
	template<> float3 VariableImpl<float3>::GetFloat3() const { return GetData(); }
	template<> float4 VariableImpl<float4>::GetFloat4() const { return GetData(); }

	template<> int VariableImpl<std::string>::GetInt() const { return std::atoi(GetDataRef().c_str()); }
	template<> float VariableImpl<std::string>::GetFloat() const { return (float)std::atof(GetDataRef().c_str()); }
	template<> std::string const& VariableImpl<std::string>::GetString() const { return GetDataRef(); }

	template<> void VariableImpl<bool>::SetBool(bool value, SetBy setby) { SetData(value, setby); }
	template<> void VariableImpl<int>::SetInt(int value, SetBy setby) { SetData(value, setby); }
	template<> void VariableImpl<int2>::SetInt2(int2 value, SetBy setby) { SetData(value, setby); }
	template<> void VariableImpl<int3>::SetInt3(int3 value, SetBy setby) { SetData(value, setby); }
	template<> void VariableImpl<float>::SetFloat(float value, SetBy setby) { SetData(value, setby); }
	template<> void VariableImpl<float2>::SetFloat2(float2 value, SetBy setby) { SetData(value, setby); }
	template<> void VariableImpl<float3>::SetFloat3(float3 value, SetBy setby) { SetData(value, setby); }
	template<> void VariableImpl<float4>::SetFloat4(float4 value, SetBy setby) { SetData(value, setby); }
	template<> void VariableImpl<std::string>::SetString(std::string const& value, SetBy setby) { SetData(value, setby); }

	template <typename T> std::string vector_to_string(T v)
	{
		std::string result;
		for (int i = 0; i < T::DIM; ++i)
		{
			char buff[16] = { 0 };
			if (auto [p, ec] = std::to_chars(buff, buff + 16, v[i]); ec == std::errc())
			{
				if (i < (T::DIM - 1))
					*p = ' ';

				result += buff;
			}
		}
		return result;
	}

	// A specialization of vector_to_string to work around std::to_chars being unavailable for float on clang
	template <int N> std::string float_vector_to_string(dm::vector<float, N> v)
	{
		std::string result;
		for (int i = 0; i < N; ++i)
		{
			char buff[16] = { 0 };
			int c = snprintf(buff, sizeof(buff), "%f", v[i]);

			if (c < sizeof(buff))
			{
				if (i < (N - 1))
					buff[strlen(buff)] = ' ';

				result += buff;
			}
		}
		return result;
	}

	std::string float_to_string(float v)
	{
		char buff[16] = { 0 };
		snprintf(buff, sizeof(buff), "%f", v);
		return buff;
	}

	template <> std::string VariableImpl<bool>::GetValueAsString() const { return m_Data ? "true" : "false"; }
	template <> std::string VariableImpl<int2>::GetValueAsString() const { return vector_to_string(m_Data); }
	template <> std::string VariableImpl<int3>::GetValueAsString() const { return vector_to_string(m_Data); }
    template <> std::string VariableImpl<float>::GetValueAsString() const { return float_to_string(m_Data); }
    template <> std::string VariableImpl<float2>::GetValueAsString() const { return float_vector_to_string(m_Data); }
	template <> std::string VariableImpl<float3>::GetValueAsString() const { return float_vector_to_string(m_Data); }
	template <> std::string VariableImpl<float4>::GetValueAsString() const { return float_vector_to_string(m_Data); }
	template <> std::string VariableImpl<std::string>::GetValueAsString() const { return m_Data; }

	//
	// Console Variable Reference 
	//

#define DEFINE_CVARREF_IMPLEMENTATION(type) \
	template <> AutoVariable<type>::AutoVariable(char const* name, char const* description, type const& value, bool ronly, bool cheat) \
	: m_Variable(*(VariableImpl<type>*)objectsDictionary.RegisterVariable<type>( \
	    name, description, value, VariableState(ronly, cheat, VariableType::IsA<type>(), VariableState::CODE))) { } \
    \
	template <> std::string const& AutoVariable<type>::GetName() const { return objectsDictionary.GetObjectName(&m_Variable); } \
	template <> std::string const& AutoVariable<type>::GetDescription() const { return m_Variable.GetDescription(); } \
	template <> void AutoVariable<type>::SetDescription(std::string const& description) { m_Variable.SetDescription(description); } \
	template <> VariableState AutoVariable<type>::GetState() const { return m_Variable.GetState(); } \
	template <> type AutoVariable<type>::GetValue() const { return m_Variable.GetData(); } \
	template <> void AutoVariable<type>::SetValue(type const& value) { m_Variable.SetData(value, VariableState::CODE); } \
    template <> void AutoVariable<type>::SetOnChangeCallback(Variable::Callback onChange) { m_Variable.SetOnChangeCallback(onChange); } \
	template <> void AutoVariable<type>::ExecuteOnChangeCallback() { m_Variable.ExecuteOnChangeCallback(); } \
	template <> Variable* AutoVariable<type>::operator &() { return &m_Variable; } \
	template <> AutoVariable<type>::operator type() const { return GetValue(); } \
	template <> AutoVariable<type>& AutoVariable<type>::operator=(const type& value) { SetValue(value); return *this; }


	DEFINE_CVARREF_IMPLEMENTATION(bool);
	DEFINE_CVARREF_IMPLEMENTATION(int);
	DEFINE_CVARREF_IMPLEMENTATION(int2);
	DEFINE_CVARREF_IMPLEMENTATION(int3);
	DEFINE_CVARREF_IMPLEMENTATION(float);
	DEFINE_CVARREF_IMPLEMENTATION(float2);
	DEFINE_CVARREF_IMPLEMENTATION(float3);
	DEFINE_CVARREF_IMPLEMENTATION(float4);
	DEFINE_CVARREF_IMPLEMENTATION(std::string);

	//
	// Dictionary implementation
	//

	bool RegisterCommand(CommandDesc const& desc)
	{
		return objectsDictionary.RegisterCommand(desc) != nullptr;
	}

	bool UnregisterCommand(std::string_view name)
	{
		return objectsDictionary.UnregisterCommand(name);
	}

	Object* FindObject(std::string_view name)
	{
		return objectsDictionary.FindObject(name);
	}

	std::vector<std::string_view> MatchObjectNames(char const* regex)
	{
		return objectsDictionary.FindObjectNames(regex);
	}

	std::vector<Object*> MatchObjects(char const* regex)
	{
		return objectsDictionary.FindObjects(regex);
	}

	Command* FindCommand(std::string_view name)
	{
		if (Object* cobj = FindObject(name))
			return cobj->AsCommand();
		return nullptr;
	}

	Variable* FindVariable(std::string_view name)
	{
		if (Object* cobj = FindObject(name))
			return cobj->AsVariable();
		return nullptr;
	}

	void ResetAll()
	{
		objectsDictionary.Reset();
	}

	void ParseIniFile(char const* inidata, char const* filename)
	{
		if (!filename)
			filename = "<nullptr name>";

		std::stringstream inifile(inidata);

		uint32_t lineno = 0;
		for (std::string linestr; std::getline(inifile, linestr); ++lineno)
		{		
			std::string_view line(linestr);

			// trim comments
			if (size_t pos = line.find('#'); pos != std::string::npos)
				line.remove_suffix(line.length() - pos);
			ds::trim(line);

			if (line.length() == 0)
				continue;

			auto tokens = ds::split(line, "[=]");
			if (tokens.size() == 0)
			{
				donut::log::error(" % s: % d parse error : cannot find '=' - skipped line", filename, lineno);
				continue;
			}
			if (tokens.size() != 2)
			{
				donut::log::error(" % s: % d parse error : invalid '<token> = <value>' format - skipped line", filename, lineno);
				continue;
			}

			std::string cvarname(tokens[0]);
			ds::trim(cvarname);
			
			std::string_view cvarvalue = tokens[1];
			ds::trim(cvarvalue);

			assert(cvarname.length() > 0 && cvarvalue.length() > 0);

			if (cvar* var = FindVariable(cvarname.data()))
			{
				if (!var->SetValueFromString(cvarvalue, VariableState::INI))
				{
					donut::log::error("%s:%d parse error : cannot set value for variable '%s'", filename, lineno, cvarname.data());
				}
			}
			else
			{
				donut::log::error("%s:%d parse error : unknown console variable name '%s'", filename, lineno, cvarname.data());
			}
		}
	}
} // end namespace donut::engine::console
