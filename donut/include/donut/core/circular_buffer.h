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

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>
#include <stdexcept>

namespace donut::core
{

	// Static-sized circular buffer container and iterators
	// 
	// Behavior : pushing back items when the buffer is full results
	// in the front item being automatically evicted from the buffer
	//
	// note : *DO NOT* store pointers as eviction means values are
	// overwritten and will result in memory leaks. Use smart pointers
	// instead.

	template<typename T, size_t N> class circular_buffer
	{
	public:

		void clear() { _front = _count = 0; }

		std::size_t capacity() const { return _data.size(); }

		std::size_t size() const { return _count; }

		bool empty() const { return _count == 0; }

		bool full() const { return size() == capacity(); }

		T& front() { return _data[_front]; }

		const T& front() const { return front(); }

		T& back() { return _data[wrap(_count - 1)]; }

		const T& back() const { return back(); }

		// returns false if the front item had to be removed to make space
		bool push_back(const T& t)
		{
			if (size() == capacity())
			{
				_data[_front] = t;
				_front = wrap(1);
				return false;
			}
			else
			{
				size_t index = wrap(_count);
				_data[index] = t;
				++_count;
				return true;
			}
		}

		void pop_front()
		{
			if (size() > 0)
			{
				_front = wrap(1);
				--_count;
			}
		}

		void pop_back()
		{
			if (size() > 0)
			{
				--_count;
			}
		}

		T& at(size_t n)
		{
			if (n >= size()) throw std::out_of_range("Parameter out of range");
			return (*this)[n];
		}

		T const& at(size_t n) const
		{
			if (n >= size()) throw std::out_of_range("Parameter out of range");
			return (*this)[n];
		}

		T& operator [](size_t n) { return _data[wrap(n)]; }
		T const& operator [](size_t n) const { return _data[wrap(n)]; }

		circular_buffer<T, N> & operator = (std::array<T, N> const& other) 
		{ 
			_data = other; 
			_front = 0; 
			_count = other.size();
			return *this;
		}

	public:

		class iterator;
		iterator begin();
		iterator end();

		class reverse_iterator;
		reverse_iterator rbegin();
		reverse_iterator rend();

	private:

		inline size_t wrap(size_t index) const
		{
			size_t ofs = _front + index;
			return ofs < capacity() ? ofs : ofs - capacity();
		}

		std::array<T, N> _data;
		size_t _front = 0;
		size_t _count = 0;
	};

	// iterators

	template<typename T, size_t S> class circular_buffer<T, S>::iterator
	{
	public:

		typedef circular_buffer<T, S> parent_type;
		typedef typename parent_type::iterator self_type;

		iterator(parent_type& parent, size_t index) : _parent(parent), _index(index) {}

		self_type const& operator++() { _index = std::min(_index + 1, _parent.size()); return *this; }
		self_type operator++(int) { iterator old(*this); operator++(); return old; }

		self_type const& operator--() { _index = _index == 0 ? 0 : _index - 1; return *this; }
		self_type operator--(int) { iterator old(*this); operator--(); return old; }

		T& operator*() { return _parent[_index]; }
		T* operator->() { return &(_parent[_index]); }

		inline bool operator==(const self_type& other) const { return (&_parent == &other._parent) && (_index == other._index); }
		inline bool operator!=(const self_type& other) const { return !(*this == other); }
		
		self_type& operator=(self_type& other) { _parent = other._parent; _index = other._index; return *this; }

	private:

		parent_type& _parent;
		size_t _index;
	};

	template<typename T, size_t S> class circular_buffer<T, S>::reverse_iterator
	{
	public:

		typedef circular_buffer<T, S> parent_type;
		typedef typename parent_type::reverse_iterator self_type;

		reverse_iterator(parent_type& parent, size_t index) : _parent(parent), _index(index) {}

		self_type const& operator--() { _index = std::min(_index+1, _parent.size()); return *this; }
		self_type operator--(int) { iterator old(*this); operator++(); return old; }

		self_type const& operator++() { _index = _index == 0 ? _parent.size() : _index - 1; return *this; }
		self_type operator++(int) { iterator old(*this); operator--(); return old; }

		T& operator*() { return _parent[_index]; }
		T* operator->() { return &(_parent[_index]); }

		inline bool operator==(const self_type& other) const { return (&_parent == &other._parent) && (_index == other._index); }
		inline bool operator!=(const self_type& other) const { return !(*this == other); }

		self_type& operator=(const self_type& other) { _parent = other._parent; _index = other._index; return *this; }

	private:

		parent_type& _parent;
		size_t _index;
	};

	template<typename T, size_t S> typename circular_buffer<T, S>::iterator circular_buffer<T, S>::begin()
	{
		return iterator(*this, 0);
	}

	template<typename T, size_t S> typename circular_buffer<T, S>::iterator circular_buffer<T, S>::end()
	{
		return iterator(*this, size());
	}

	template<typename T, size_t S> typename circular_buffer<T, S>::reverse_iterator circular_buffer<T, S>::rbegin()
	{
		return reverse_iterator(*this, size()-1);
	}

	template<typename T, size_t S> typename circular_buffer<T, S>::reverse_iterator circular_buffer<T, S>::rend()
	{
		return reverse_iterator(*this, size());
	}

} // end namespace donut::core
