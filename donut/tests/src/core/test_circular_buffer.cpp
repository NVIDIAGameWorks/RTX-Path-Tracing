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

#include <donut/core/circular_buffer.h>


#include <donut/tests/utils.h>
#include <filesystem>

using namespace donut;

void test_circular_buffer()
{
	core::circular_buffer<int, 5> cbuf;

	CHECK(cbuf.empty() && (cbuf.size() == 0) && (cbuf.capacity() == 5));

	cbuf.push_back(1);
	cbuf.push_back(2);
	cbuf.push_back(3);
	CHECK((!cbuf.empty()) && (!cbuf.full()) && (cbuf.size() == 3) && (cbuf.capacity() == 5));
	CHECK((cbuf[0] == 1) && (cbuf[1] == 2) && (cbuf[2] == 3));
	CHECK((cbuf.front() == 1) && (cbuf.back() == 3));
	
	cbuf.pop_front();
	CHECK((!cbuf.empty()) && (!cbuf.full()) && (cbuf.size() == 2) && (cbuf.capacity() == 5));
	CHECK((cbuf[0] == 2) && (cbuf[1] == 3));
	CHECK((cbuf.front() == 2) && (cbuf.back() == 3));

	cbuf.pop_back();
	CHECK((!cbuf.empty()) && (!cbuf.full()) && (cbuf.size() == 1) && (cbuf.capacity() == 5));
	CHECK((cbuf[0] == 2));
	CHECK((cbuf.front() == 2) && (cbuf.back() == 2));

	cbuf.clear();
	CHECK(cbuf.empty() && (!cbuf.full()) && (cbuf.size() == 0) && (cbuf.capacity() == 5));

	cbuf.push_back(1);
	cbuf.push_back(2);
	cbuf.push_back(3);
	cbuf.push_back(4);
	cbuf.push_back(5);
	CHECK((!cbuf.empty()) && (cbuf.full()) && (cbuf.size() == 5) && (cbuf.capacity() == 5));

	cbuf.push_back(6);
	CHECK((!cbuf.empty()) && (cbuf.full()) && (cbuf.size() == 5) && (cbuf.capacity() == 5));
	CHECK((cbuf[0] == 2) && (cbuf[1] == 3) && (cbuf[2] == 4) && (cbuf[3] == 5) && (cbuf[4] == 6));

	cbuf.push_back(7);
	CHECK((!cbuf.empty()) && (cbuf.full()) && (cbuf.size() == 5) && (cbuf.capacity() == 5));
	CHECK((cbuf[0] == 3) && (cbuf[1] == 4) && (cbuf[2] == 5) && (cbuf[3] == 6) && (cbuf[4] == 7));

	cbuf.push_back(8);
	CHECK((!cbuf.empty()) && (cbuf.full()) && (cbuf.size() == 5) && (cbuf.capacity() == 5));
	CHECK((cbuf[0] == 4) && (cbuf[1] == 5) && (cbuf[2] == 6) && (cbuf[3] == 7) && (cbuf[4] == 8));

	cbuf.push_back(9);
	CHECK((!cbuf.empty()) && (cbuf.full()) && (cbuf.size() == 5) && (cbuf.capacity() == 5));
	CHECK((cbuf[0] == 5) && (cbuf[1] == 6) && (cbuf[2] == 7) && (cbuf[3] == 8) && (cbuf[4] == 9));

	cbuf.push_back(10);
	CHECK((!cbuf.empty()) && (cbuf.full()) && (cbuf.size() == 5) && (cbuf.capacity() == 5));
	CHECK((cbuf[0] == 6) && (cbuf[1] == 7) && (cbuf[2] == 8) && (cbuf[3] == 9) && (cbuf[4] == 10));

	cbuf.push_back(11);
	CHECK((!cbuf.empty()) && (cbuf.full()) && (cbuf.size() == 5) && (cbuf.capacity() == 5));
	CHECK((cbuf[0] == 7) && (cbuf[1] == 8) && (cbuf[2] == 9) && (cbuf[3] == 10) && (cbuf[4] == 11));
}

void test_circular_buffer_iterators()
{
	core::circular_buffer<int, 5> cbuf;
	cbuf = {1, 2, 3, 4, 5};

	CHECK(*(cbuf.begin()) == 1);

	{
		int i = 1;
		for (auto it : cbuf)
		{
			CHECK(it == i);
			++i;
		}		
		
		auto it = cbuf.begin();
		for (int i = 1; i < 6; ++i, ++it)
			CHECK(*it == i);
		CHECK(it == cbuf.end());

		++it;
		CHECK(it == cbuf.end());
		--it;
		CHECK(*it == 5);
		--it;
		CHECK(*it == 4);
	}

	{
		auto it = cbuf.rbegin();

		for (int i = 5; i > 0; --i, ++it)
			CHECK(*it == i);
		CHECK(it == cbuf.rend());

		--it;
		CHECK(it == cbuf.rend());

		int i = 5;
		for (it = cbuf.rbegin(); it != cbuf.rend(); ++it, --i)
			CHECK(*it == i);
		CHECK(it==cbuf.rend());
	}
}

int main(int, char** argv)
{
	try
	{
		test_circular_buffer();
		test_circular_buffer_iterators();
	}
	catch (const std::runtime_error & err)
	{
		fprintf(stderr, "%s", err.what());
		return 1;
	}
	return 0;
}
