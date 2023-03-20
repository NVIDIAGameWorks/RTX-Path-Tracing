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

#include <functional>

namespace donut::log
{
    enum class Severity
    {
        None = 0,
        Debug,
        Info,
        Warning,
        Error,
        Fatal
    };

	typedef std::function<void(Severity, char const*)> Callback;

    void SetMinSeverity(Severity severity);
    void SetCallback(Callback func);
	Callback GetCallback();
    void ResetCallback();
    void SetErrorMessageCaption(const char* caption);

    void message(Severity severity, const char* fmt...);
    void debug(const char* fmt...);
    void info(const char* fmt...);
    void warning(const char* fmt...);
    void error(const char* fmt...);
    void fatal(const char* fmt...);
}
