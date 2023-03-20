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

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace donut::engine
{
	class TextureCache;

	namespace console
	{
		// Commnad-line lexer (splits a command line into a vector of tokens)
		//
		// Valid tokens:
		//   - identifiers: none
		//   - keywords   : none
		//   - separator  : none
		//   - operator   : none
		//   - literals   : strings
		//
		// Lexical grammar:
		//
		//   - strings: valid strings are sequences of characters separated by
		//     white-space characters. Single quotes (') and double quotes (")
		//     can be used as delimiters. Backslash (\) can be used to escape
		//     quotes and space.
		//  

		enum class TokenType
		{
			INVALID = 0,
			STRING
		};

		struct Token
		{
			TokenType type = TokenType::INVALID;
			std::string value;
		};

		class Lexer;

		// Command-line interpreter

		class Interpreter
		{
		public:

			Interpreter();

			struct Result
			{
				bool status = false;
				std::string output;
			};

			Result Execute(std::string_view const cmdline);

			// parse incomplete command line & return auto-completion suggestions
			std::vector<std::string> Suggest(std::string_view const cmdline, size_t cursor_pos);

			bool RegisterCommands(std::shared_ptr<TextureCache> textureCache);

		private:

			std::shared_ptr<TextureCache> m_TextureCache;
		};

	} // end namespace console

} // end namespace donut::engine
