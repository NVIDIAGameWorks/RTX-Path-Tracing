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

#include <donut/core/log.h>
#include <stdio.h>
#include <stdarg.h>
#include <iterator>
#include <mutex>
#if _WIN32
#include <Windows.h>
#endif

namespace donut::log
{
    static constexpr size_t g_MessageBufferSize = 4096;

    static std::string g_ErrorMessageCaption = "Error";

    static std::mutex g_LogMutex;
    
    void DefaultCallback(Severity severity, const char* message)
    {
        const char* severityText = "";
        switch (severity)
        {
        case Severity::Debug: severityText = "DEBUG";  break;
        case Severity::Info: severityText = "INFO";  break;
        case Severity::Warning: severityText = "WARNING"; break;
        case Severity::Error: severityText = "ERROR"; break;
        case Severity::Fatal: severityText = "FATAL ERROR"; break;
		default:
			break;
        }

        char buf[g_MessageBufferSize];
        snprintf(buf, std::size(buf), "%s: %s", severityText, message);

        {
            std::lock_guard<std::mutex> lockGuard(g_LogMutex);

#if _WIN32
            OutputDebugStringA(buf);
            OutputDebugStringA("\n");

            if (severity == Severity::Error || severity == Severity::Fatal)
            {
                MessageBoxA(0, buf, g_ErrorMessageCaption.c_str(), MB_ICONERROR);
            }
#else
            fprintf(stderr, "%s\n", buf);
#endif
        }

        if (severity == Severity::Fatal)
            abort();
    }

    void SetErrorMessageCaption(const char* caption)
    {
        g_ErrorMessageCaption = (caption) ? caption : "";
    }

    static Callback g_Callback = &DefaultCallback;
    static Severity g_MinSeverity = Severity::Info;

    void SetMinSeverity(Severity severity)
    {
        g_MinSeverity = severity;
    }

    void SetCallback(Callback func)
    {
        g_Callback = func;
    }

	Callback GetCallback()
	{
		return g_Callback;
	}

    void ResetCallback()
    {
        g_Callback = &DefaultCallback;
    }

    void message(Severity severity, const char* fmt...)
    {
        if (static_cast<int>(g_MinSeverity) > static_cast<int>(severity))
            return;

        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(severity, buffer);

        va_end(args);
    }

    void debug(const char* fmt...)
    {
        if (static_cast<int>(g_MinSeverity) > static_cast<int>(Severity::Debug))
            return;

        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(Severity::Debug, buffer);

        va_end(args);
    }

    void info(const char* fmt...)
    {
        if (static_cast<int>(g_MinSeverity) > static_cast<int>(Severity::Info))
            return;

        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(Severity::Info, buffer);

        va_end(args);
    }

    void warning(const char* fmt...)
    {
        if (static_cast<int>(g_MinSeverity) > static_cast<int>(Severity::Warning))
            return;

        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(Severity::Warning, buffer);

        va_end(args);
    }

    void error(const char* fmt...)
    {
        if (static_cast<int>(g_MinSeverity) > static_cast<int>(Severity::Error))
            return;

        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(Severity::Error, buffer);

        va_end(args);
    }

    void fatal(const char* fmt...)
    {
        char buffer[g_MessageBufferSize];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, std::size(buffer), fmt, args);

        g_Callback(Severity::Fatal, buffer);

        va_end(args);
    }
}
