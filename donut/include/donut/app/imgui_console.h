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

#pragma once

#include <donut/core/circular_buffer.h>
#include <donut/core/log.h>

#include <imgui.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace donut::engine::console
{
	class Interpreter;
}

namespace donut::app
{

	class ImGui_Console 
	{
	public:

		struct Options
		{
			
			ImFont* font = nullptr;        // it is recommended to specify a monospace font

			bool auto_scroll = true;       // automatically keep log output scrolled to the most recent item
			bool scroll_to_bottom = false; // scoll to botom on console creation, if the log is not empty

			bool capture_log = true;       // captures donut event logs & redirects to the console
			bool show_info = false;        // default state of log events filters
			bool show_warnings = true;
			bool show_errors = true;
		};

		ImGui_Console(std::shared_ptr<donut::engine::console::Interpreter> interpreter, Options const& opts);

		~ImGui_Console();

		void Print(char const* fmt, ...);

		void Print(std::string_view line);

		void ClearLog();

		void ClearHistory();

		void Render(bool * open=nullptr);

	private:

		int HistoryKeyCallback(ImGuiInputTextCallbackData* data);

		int AutoCompletionCallback(ImGuiInputTextCallbackData* data);

		int TextEditCallback(ImGuiInputTextCallbackData* data);

		void ExecCommand(char const* cmd);

	private:

		typedef std::array<char, 256> InputBuffer;
		InputBuffer m_InputBuffer = { 0 };

		typedef donut::core::circular_buffer<std::string, 1024> HistoryBuffer;
		HistoryBuffer m_History;
		HistoryBuffer::reverse_iterator m_HistoryIterator = m_History.rend();

		struct LogItem
		{
			donut::log::Severity severity = donut::log::Severity::None;
			ImVec4 textColor = ImVec4(1.f, 1.f, 1.f, 1.f);
			std::string text;
		};

		typedef donut::core::circular_buffer<LogItem, 5000> ItemsLog;
		ItemsLog m_ItemsLog;

	private:

		Options m_Options;

		std::shared_ptr<donut::engine::console::Interpreter> m_Interpreter;
	};

} // namespace donut::app
