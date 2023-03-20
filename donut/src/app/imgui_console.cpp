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

/*
License for Dear ImGui

Copyright (c) 2014-2019 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <donut/app/imgui_console.h>
#include <donut/app/imgui_renderer.h>

#include <donut/engine/ConsoleInterpreter.h>
#include <donut/engine/ConsoleObjects.h>
#include <donut/core/string_utils.h>

#include <cstdarg>
#include <cctype>

using namespace donut::app;
using namespace donut::engine;


static ImVec4 getSeverityColor(donut::log::Severity severity)
{
	using namespace donut::log;	
	switch (severity)
	{
	case Severity::Info: return ImVec4(.6f, .8f, 1.f, 1.f);
	case Severity::Warning: return ImVec4(1.f, .5f, 0.f, 1.f);
	case Severity::Error: return ImVec4(1.f, 0.f, 0.f, 1.f);
	default:
		break;
	}
	return ImVec4(1.f, 1.f, 1.f, 1.f);
}

ImGui_Console::ImGui_Console(std::shared_ptr<console::Interpreter> interpreter, Options const& options) 
	: m_Interpreter(interpreter)
	, m_Options(options)
{
	if (options.capture_log)
	{
		donut::log::SetCallback([&](donut::log::Severity severity, char const* msg) {			
				ImVec4 color = getSeverityColor(severity);
				this->m_ItemsLog.push_back({severity, color, msg});
			});
	}
}
ImGui_Console::~ImGui_Console()
{ }

void ImGui_Console::Print(char const* fmt, ...)
{
	InputBuffer buf;
	std::va_list args;

	va_start(args, fmt);
	vsnprintf(buf.data(), buf.size(), fmt, args);
	buf.back() = 0;
	va_end(args);

	LogItem item;
	item.text = buf.data();
	m_ItemsLog.push_back(item);
}

void ImGui_Console::Print(std::string_view line)
{
	LogItem item;
	item.text = line;
	m_ItemsLog.push_back(item);
}

void ImGui_Console::ClearLog()
{
	m_ItemsLog.clear();
}

void ImGui_Console::ClearHistory()
{
	m_History.clear();
	m_HistoryIterator = m_History.rend();
}

void ImGui_Console::Render(bool* open)
{
	ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Console", open, ImGuiWindowFlags_MenuBar))
	{
		ImGui::End();
		return;
	}

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Close Console"))
			*open = false;
		ImGui::EndPopup();
	}

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Edit"))
		{
			bool clearLog = ImGui::MenuItem("Clear Log");
			bool clearHistory = ImGui::MenuItem("Clear History");
			bool clearAll = ImGui::MenuItem("Clear All");

			if (clearLog || clearAll)
				this->ClearLog();
			if (clearHistory || clearAll)
				this->ClearHistory();
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();			
	}

	//ImGui::Separator();

	// Log area

	const float footer_height = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	ImGui::BeginChild("Log panel", ImVec2(0, -footer_height), false, ImGuiWindowFlags_HorizontalScrollbar);

	// right click popup on log panel
	if (ImGui::BeginPopupContextWindow()) 
	{
		if (ImGui::Selectable("Clear"))
			ClearLog();
		ImGui::EndPopup();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing

	if (m_Options.font)
		ImGui::PushFont(m_Options.font);
	for (auto const& item : m_ItemsLog)
	{
		using namespace donut::log;

		bool showItem = true;
		switch (item.severity)
		{
		case Severity::Info: showItem = m_Options.show_info; break;
		case Severity::Warning: showItem = m_Options.show_warnings; break;
		case Severity::Error: showItem = m_Options.show_errors; break;
		default: break;
		}

		if (showItem)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, item.textColor);
			ImGui::TextUnformatted(item.text.c_str());
			ImGui::PopStyleColor();
		}
	}

	if (m_Options.scroll_to_bottom || (m_Options.auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
	{
		ImGui::SetScrollHereY(1.f);
	}

	if (m_Options.font)
		ImGui::PopFont();

	m_Options.scroll_to_bottom = false;
	ImGui::PopStyleVar();
	ImGui::EndChild(); // end log scroll area

	ImGui::Separator();

	// Command line
	if (m_Options.font)
		ImGui::PushFont(m_Options.font);

	bool reclaim_focus = false;
	auto flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
	if (ImGui::InputText("", m_InputBuffer.data(), m_InputBuffer.size(), flags, 
		[](ImGuiInputTextCallbackData* data)
		{
			ImGui_Console* console = (ImGui_Console*)data->UserData;
			return console->TextEditCallback(data);
		}, (void*)this))
	{
		if (m_InputBuffer.front()!='0')
		{
			this->ExecCommand(m_InputBuffer.data());
			m_InputBuffer.front() = 0;
		}
		reclaim_focus = true;
	}

	if (m_Options.font)
		ImGui::PopFont();

	// Auto-focus on window apparition
	ImGui::SetItemDefaultFocus();
	if (reclaim_focus)
		ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::Text("Filters : "); ImGui::SameLine();
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
	auto filterButton = [](char const* label, bool* value, donut::log::Severity severity) {
		ImGui::PushStyleColor(ImGuiCol_Border, getSeverityColor(severity));
		ImGui::Checkbox(label, value);
		ImGui::PopStyleColor();
	};
	filterButton("Errors", &m_Options.show_errors, donut::log::Severity::Error); ImGui::SameLine();
	filterButton("Warnings", &m_Options.show_warnings, donut::log::Severity::Warning); ImGui::SameLine();
	filterButton("Info", &m_Options.show_info, donut::log::Severity::Info);
	ImGui::PopStyleVar(); // FrameBorder

	ImGui::End();
}

static void printLines(ImGui_Console& console, std::string const& output)
{
	if (output.empty())
		return;

	std::string line;
	for (int start = 0, curr = 0; curr < (int)output.size(); ++curr)
	{
		if ((output[curr] == '\r') || (output[curr] == '\n'))
		{
			console.Print(std::string_view(&output[start], curr - start));
			start = ++curr;
		}
	}		
}

void ImGui_Console::ExecCommand(char const* cmdline)
{
	std::string_view const cmd = cmdline;
	if (!cmd.empty())
	{
		this->Print("> %s", cmd.data());

		if (auto result = m_Interpreter->Execute(cmd); result.status)
		{
			if (!result.output.empty())
				printLines(*this, result.output.c_str());

			m_History.push_back(cmd.data());
			m_HistoryIterator = m_History.rend();
		}
	}
}


// XXXX mk: we should probably use the columns features instead ?
static void printColumns(ImGui_Console& console, std::vector<std::string> const& items)
{
	auto computeLineWidth = []() {
		// XXXX mk: this only works if the font is monospace !
		float width = ImGui::CalcItemWidth();
		ImVec2 charWidth = ImGui::CalcTextSize("A");
		return (size_t)(width / charWidth.x);
	};

	size_t max_len = 0;
	for (auto const candidate : items)
		max_len = std::max(max_len, candidate.size());

	size_t line_width = computeLineWidth();
	size_t ncolumns = line_width / max_len;

	std::string line; int col = 1;
	for (auto const candidate : items)
	{
		if ((col % ncolumns) != 0)
		{
			line += candidate;
			line += ' ';
			++col;
		}
		else
		{
			console.Print(line.c_str());
			line.clear();
			col = 1;
		}
	}
	if (!line.empty())
		console.Print(line.c_str());
}

static std::string extendKeyword(std::string_view keyword, std::vector<std::string> const& candidates)
{
	std::string match(keyword.data(), keyword.length());
	while (true)
	{
		int c = -1, cpos = (int)match.size();
		for (std::string_view const candidate : candidates)
		{
			if (cpos < candidate.size())
			{
				if (c == -1)
					c = candidate[cpos];
				else
					if (c != candidate[cpos])
						return match;
			}
			else
				return match;
		}
		match.push_back(c);
	}
}

inline std::string_view isolateKeyword(std::string_view line)
{
	ds::ltrim(line);
	if (auto it = std::find_if(line.rbegin(), line.rend(), [](int ch) { return std::isspace(ch); }); it != line.rend())
	{
		line.remove_prefix(std::distance(line.begin(), it.base()));
	}	
	return line;
}

int ImGui_Console::AutoCompletionCallback(ImGuiInputTextCallbackData* data)
{

	std::string_view cmdline(data->Buf, data->CursorPos);
    std::string_view keyword = isolateKeyword(cmdline);

	if (auto candidates = m_Interpreter->Suggest(data->Buf, data->CursorPos); !candidates.empty())
	{
		if (candidates.size() == 1)
		{
			std::string_view candidate = candidates.front();
			candidate.remove_prefix(keyword.size());
			data->InsertChars(data->CursorPos, candidate.data(), candidate.data() + candidate.size());
			data->InsertChars(data->CursorPos, " ");
		}
		else
		{
			// multiple candidates : append as many characters as possible to input (auto-complete)
			if (std::string match = extendKeyword(keyword, candidates); match.size() > keyword.size())
			{
				data->InsertChars(data->CursorPos, match.data() + keyword.size(), match.data() + match.size());
			}

			// print all candidates in columns
			if (candidates.size() < 64)
				printColumns(*this, candidates);
					else
				Print("Too many matches (%d)", candidates.size());
			}
		}
	return 0;
}

int ImGui_Console::HistoryKeyCallback(ImGuiInputTextCallbackData* data)
{
	HistoryBuffer::reverse_iterator currentPos = m_HistoryIterator;
	switch (data->EventKey)
	{
	case ImGuiKey_UpArrow: ++m_HistoryIterator; break;
	case ImGuiKey_DownArrow: --m_HistoryIterator; break;
	}
	if (currentPos != m_HistoryIterator)
	{
		std::string const& cmd = *m_HistoryIterator;
		snprintf(data->Buf, data->BufSize, "%s", cmd.c_str());
		data->BufTextLen = (int)cmd.length();
		data->BufDirty = true;
	}
	return 0;
}

int ImGui_Console::TextEditCallback(ImGuiInputTextCallbackData* data)
{
	switch (data->EventFlag)
	{
	case ImGuiInputTextFlags_CallbackCompletion:
		return AutoCompletionCallback(data);

	case ImGuiInputTextFlags_CallbackHistory:
		return HistoryKeyCallback(data);
	}
	return 0;
}
