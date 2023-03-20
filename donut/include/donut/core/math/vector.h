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
#include <cmath>

namespace donut::math
{
	// Macro to define conversion and subscript operators
#define VECTOR_MEMBERS(T, n) \
			/* Conversions to C arrays of fixed size */ \
			typedef T (&array_t)[n]; \
			operator array_t () \
				{ return reinterpret_cast<array_t>(*data()); } \
			typedef const T (&const_array_t)[n]; \
			operator const_array_t () const \
				{ return reinterpret_cast<const_array_t>(*data()); } \
			/* Subscript operators - built-in subscripts are ambiguous without these */ \
			T & operator [] (int i) \
				{ return data()[i]; } \
			const T & operator [] (int i) const \
				{ return data()[i]; } \
            static constexpr uint const DIM = n; \
            vector() { } \
            vector(const T* v) \
                { for(int i = 0; i < n; i++) data()[i] = v[i]; } \
			/* Conversion to bool is not allowed (otherwise would \
			   happen implicitly through array conversions) */ \
			private: operator bool();

	// Generic vector struct, providing storage, using partial
	// specialization to get names (xyzw) for n <= 4

	template <typename T, int n>
	struct vector
	{
		cassert(n > 4);
		T m_data[n];

        vector(T a) 
        { for (int i = 0; i < n; i++) m_data[i] = a; } 

        T* data() { return m_data; }
        const T* data() const { return m_data; }

        template<typename U> 
        explicit vector(const vector<U, n>& v)
        { for (int i = 0; i < n; i++) m_data[i] = static_cast<T>(v.m_data[i]); }

		VECTOR_MEMBERS(T, n)
	};

	template <typename T>
	struct vector<T, 2>
	{
        T x, y;

        T* data() { return &x; }
        const T* data() const { return &x; }

        constexpr vector(T a) : x(a), y(a) { }
        constexpr vector(T _x, T _y) : x(_x), y(_y) { }
        template<typename U>
		explicit constexpr vector(const vector<U, 2>& v) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)) { }
        constexpr vector(const vector<T, 3>& v) : x(v.x), y(v.y) { }
        constexpr vector(const vector<T, 4>& v) : x(v.x), y(v.y) { }
        constexpr static vector zero() { return vector(static_cast<T>(0)); }

		VECTOR_MEMBERS(T, 2)
	};

	template <typename T>
	struct vector<T, 3>
	{
        T x, y, z;

        T* data() { return &x; }
        const T* data() const { return &x; }

        vector<T, 2>& xy() { return *reinterpret_cast<vector<T, 2>*>(&x); }
        const vector<T, 2>& xy() const { return *reinterpret_cast<const vector<T, 2>*>(&x); }

        constexpr vector(T a) : x(a), y(a), z(a) { }
        constexpr vector(T _x, T _y, T _z) : x(_x), y(_y), z(_z) { }
        constexpr vector(const vector<T, 2>& xy, T _z) : x(xy.x), y(xy.y), z(_z) { }
        constexpr vector(const vector<T, 4>& v) : x(v.x), y(v.y), z(v.z) { }
        template<typename U>
		explicit constexpr vector(const vector<U, 3>& v) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(v.z)) { }
        constexpr static vector zero() { return vector(static_cast<T>(0)); }

		VECTOR_MEMBERS(T, 3)
	};

	template <typename T>
	struct vector<T, 4>
    {
        T x, y, z, w;

        T* data() { return &x; }
        const T* data() const { return &x; }

        vector<T, 2>& xy() { return *reinterpret_cast<vector<T, 2>*>(&x); }
        const vector<T, 2>& xy() const { return *reinterpret_cast<const vector<T, 2>*>(&x); }
        vector<T, 2>& zw() { return *reinterpret_cast<vector<T, 2>*>(&z); }
        const vector<T, 2>& zw() const { return *reinterpret_cast<const vector<T, 2>*>(&z); }
        vector<T, 3>& xyz() { return *reinterpret_cast<vector<T, 3>*>(&x); }
        const vector<T, 3>& xyz() const { return *reinterpret_cast<const vector<T, 3>*>(&x); }

        constexpr vector(T a) : x(a), y(a), z(a), w(a) { }
        constexpr vector(T _x, T _y, T _z, T _w) : x(_x), y(_y), z(_z), w(_w) { }
        constexpr vector(const vector<T, 2>& xy, T _z, T _w) : x(xy.x), y(xy.y), z(_z), w(_w) { }
        constexpr vector(const vector<T, 2>& xy, const vector<T, 2>& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) { }
        constexpr vector(const vector<T, 3>& xyz, T _w) : x(xyz.x), y(xyz.y), z(xyz.z), w(_w) { }
        template<typename U>
		explicit constexpr vector(const vector<U, 4>& v) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(v.z)), w(static_cast<T>(v.w)) { }
        constexpr static vector zero() { return vector(static_cast<T>(0)); }

        VECTOR_MEMBERS(T, 4)
	};

#undef VECTOR_MEMBERS
    
	// Concrete vectors for the most common types and dimensions

#define DEFINE_CONCRETE_VECTORS(type) \
			typedef vector<type, 2> type##2; \
			typedef vector<type, 3> type##3; \
			typedef vector<type, 4> type##4;

	DEFINE_CONCRETE_VECTORS(float);
	DEFINE_CONCRETE_VECTORS(double);
	DEFINE_CONCRETE_VECTORS(int);
	DEFINE_CONCRETE_VECTORS(uint);
	DEFINE_CONCRETE_VECTORS(bool);

#undef DEFINE_CONCRETE_VECTORS



	// Overloaded math operators

#define DEFINE_UNARY_OPERATOR(op) \
        template<typename T> constexpr vector<T, 2> operator op (const vector<T, 2>& a) \
        { return vector<T, 2>(op a.x, op a.y); } \
        template<typename T> constexpr vector<T, 3> operator op (const vector<T, 3>& a) \
        { return vector<T, 3>(op a.x, op a.y, op a.z); } \
        template<typename T> constexpr vector<T, 4> operator op (const vector<T, 4>& a) \
        { return vector<T, 4>(op a.x, op a.y, op a.z, op a.w); }

#define DEFINE_BINARY_OPERATORS(op) \
        /* Vector-vector op */ \
        template<typename T> constexpr vector<T, 2> operator op (const vector<T, 2>& a, const vector<T, 2>& b) \
        { return vector<T, 2>(a.x op b.x, a.y op b.y); } \
        template<typename T> constexpr vector<T, 3> operator op (const vector<T, 3>& a, const vector<T, 3>& b) \
        { return vector<T, 3>(a.x op b.x, a.y op b.y, a.z op b.z); } \
        template<typename T> constexpr vector<T, 4> operator op (const vector<T, 4>& a, const vector<T, 4>& b) \
        { return vector<T, 4>(a.x op b.x, a.y op b.y, a.z op b.z, a.w op b.w); } \
        /* Scalar-vector op */ \
        template<typename T> constexpr vector<T, 2> operator op (T a, const vector<T, 2>& b) \
        { return vector<T, 2>(a op b.x, a op b.y); } \
        template<typename T> constexpr vector<T, 3> operator op (T a, const vector<T, 3>& b) \
        { return vector<T, 3>(a op b.x, a op b.y, a op b.z); } \
        template<typename T> constexpr vector<T, 4> operator op (T a, const vector<T, 4>& b) \
        { return vector<T, 4>(a op b.x, a op b.y, a op b.z, a op b.w); } \
        /* Vector-scalar op */ \
        template<typename T> constexpr vector<T, 2> operator op (const vector<T, 2>& a, T b) \
        { return vector<T, 2>(a.x op b, a.y op b); } \
        template<typename T> constexpr vector<T, 3> operator op (const vector<T, 3>& a, T b) \
        { return vector<T, 3>(a.x op b, a.y op b, a.z op b); } \
        template<typename T> constexpr vector<T, 4> operator op (const vector<T, 4>& a, T b) \
        { return vector<T, 4>(a.x op b, a.y op b, a.z op b, a.w op b); } 

#define DEFINE_INPLACE_OPERATORS(op) \
        /* Vector-vector op */ \
        template<typename T> vector<T, 2> operator op (vector<T, 2>& a, const vector<T, 2>& b) \
        { a.x op b.x; a.y op b.y; return a; } \
        template<typename T> vector<T, 3> operator op (vector<T, 3>& a, const vector<T, 3>& b) \
        { a.x op b.x; a.y op b.y; a.z op b.z; return a; } \
        template<typename T> vector<T, 4> operator op (vector<T, 4>& a, const vector<T, 4>& b) \
        { a.x op b.x; a.y op b.y; a.z op b.z; a.w op b.w; return a; } \
        /* Vector-scalar op */ \
        template<typename T> vector<T, 2> operator op (vector<T, 2>& a, T b) \
        { a.x op b; a.y op b; return a; } \
        template<typename T> vector<T, 3> operator op (vector<T, 3>& a, T b) \
        { a.x op b; a.y op b; a.z op b; return a; } \
        template<typename T> vector<T, 4> operator op (vector<T, 4>& a, T b) \
        { a.x op b; a.y op b; a.z op b; a.w op b; return a; }

#define DEFINE_RELATIONAL_OPERATORS(op) \
        /* Vector-vector op */ \
        template<typename T> constexpr vector<bool, 2> operator op (const vector<T, 2>& a, const vector<T, 2>& b) \
        { return vector<bool, 2>(a.x op b.x, a.y op b.y); } \
        template<typename T> constexpr vector<bool, 3> operator op (const vector<T, 3>& a, const vector<T, 3>& b) \
        { return vector<bool, 3>(a.x op b.x, a.y op b.y, a.z op b.z); } \
        template<typename T> constexpr vector<bool, 4> operator op (const vector<T, 4>& a, const vector<T, 4>& b) \
        { return vector<bool, 4>(a.x op b.x, a.y op b.y, a.z op b.z, a.w op b.w); } \
        /* Scalar-vector op */ \
        template<typename T> constexpr vector<bool, 2> operator op (T a, const vector<T, 2>& b) \
        { return vector<bool, 2>(a op b.x, a op b.y); } \
        template<typename T> constexpr vector<bool, 3> operator op (T a, const vector<T, 3>& b) \
        { return vector<bool, 3>(a op b.x, a op b.y, a op b.z); } \
        template<typename T> constexpr vector<bool, 4> operator op (T a, const vector<T, 4>& b) \
        { return vector<bool, 4>(a op b.x, a op b.y, a op b.z, a op b.w); } \
        /* Vector-scalar op */ \
        template<typename T> constexpr vector<bool, 2> operator op (const vector<T, 2>& a, T b) \
        { return vector<bool, 2>(a.x op b, a.y op b); } \
        template<typename T> constexpr vector<bool, 3> operator op (const vector<T, 3>& a, T b) \
        { return vector<bool, 3>(a.x op b, a.y op b, a.z op b); } \
        template<typename T> constexpr vector<bool, 4> operator op (const vector<T, 4>& a, T b) \
        { return vector<bool, 4>(a.x op b, a.y op b, a.z op b, a.w op b); } 

	DEFINE_BINARY_OPERATORS(+);
	DEFINE_BINARY_OPERATORS(-);
	DEFINE_UNARY_OPERATOR(-);
	DEFINE_BINARY_OPERATORS(*);
	DEFINE_BINARY_OPERATORS(/);
	DEFINE_BINARY_OPERATORS(&);
	DEFINE_BINARY_OPERATORS(|);
	DEFINE_BINARY_OPERATORS(^);
	DEFINE_UNARY_OPERATOR(!);
	DEFINE_UNARY_OPERATOR(~);

	DEFINE_INPLACE_OPERATORS(+=);
	DEFINE_INPLACE_OPERATORS(-=);
	DEFINE_INPLACE_OPERATORS(*=);
	DEFINE_INPLACE_OPERATORS(/=);
	DEFINE_INPLACE_OPERATORS(&=);
	DEFINE_INPLACE_OPERATORS(|=);
	DEFINE_INPLACE_OPERATORS(^=);

	DEFINE_RELATIONAL_OPERATORS(==);
	DEFINE_RELATIONAL_OPERATORS(!=);
	DEFINE_RELATIONAL_OPERATORS(<);
	DEFINE_RELATIONAL_OPERATORS(>);
	DEFINE_RELATIONAL_OPERATORS(<=);
	DEFINE_RELATIONAL_OPERATORS(>=);

#undef DEFINE_UNARY_OPERATOR
#undef DEFINE_BINARY_OPERATORS
#undef DEFINE_INPLACE_OPERATORS
#undef DEFINE_RELATIONAL_OPERATORS



	// Other math functions

	template <typename T, int n>
	T dot(vector<T, n> const & a, vector<T, n> const & b)
	{
		T result(0);
		for (int i = 0; i < n; ++i)
			result += a[i] * b[i];
		return result;
	}

    template <typename T>
    constexpr T dot(vector<T, 2> const & a, vector<T, 2> const & b)
    {
        return a.x * b.x + a.y * b.y;
    }

    template <typename T>
    constexpr T dot(vector<T, 3> const & a, vector<T, 3> const & b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    template <typename T>
    constexpr T dot(vector<T, 4> const & a, vector<T, 4> const & b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

	template <typename T, int n>
	T lengthSquared(vector<T, n> const & a)
		{ return dot(a, a); }

	template <typename T, int n>
	T length(vector<T, n> const & a)
		{ return sqrt(lengthSquared(a)); }

	template <typename T, int n>
	vector<T, n> normalize(vector<T, n> const & a)
		{ return a / length(a); }

	template <typename T, int n>
	vector<T, n> pow(vector<T, n> const & a, float p)
	{
		vector<T, n> result;
		for (int i = 0; i < n; ++i)
			result[i] = ::pow(a[i], p);
		return result;
	}

	template <typename T, int n>
	vector<bool, n> isnear(vector<T, n> const & a, vector<T, n> const & b, float epsilon = dm::epsilon)
	{
		vector<bool, n> result;
		for (int i = 0; i < n; ++i)
			result[i] = isnear(a[i], b[i], epsilon);
		return result;
	}

	template <typename T, int n>
	vector<bool, n> isnear(vector<T, n> const & a, T b, float epsilon = dm::epsilon)
	{
		vector<bool, n> result;
		for (int i = 0; i < n; ++i)
			result[i] = isnear(a[i], b, epsilon);
		return result;
	}

	template <typename T, int n>
	vector<bool, n> isnear(T a, vector<T, n> const & b, float epsilon = dm::epsilon)
	{
		vector<bool, n> result;
		for (int i = 0; i < n; ++i)
			result[i] = isnear(a, b[i], epsilon);
		return result;
	}

	template <typename T, int n>
	vector<bool, n> isfinite(vector<T, n> const & a)
	{
		vector<bool, n> result;
		for (int i = 0; i < n; ++i)
			result[i] = isfinite(a[i]);
		return result;
	}

	template <typename T, int n>
	vector<int, n> round(vector<T, n> const & a)
	{
		vector<int, n> result;
		for (int i = 0; i < n; ++i)
			result[i] = round(a[i]);
		return result;
	}

	template <typename T>
	constexpr vector<T, 3> cross(vector<T, 3> const & a, vector<T, 3> const & b)
	{
        return vector<T, 3>(
            a.y*b.z - a.z*b.y,
            a.z*b.x - a.x*b.z,
            a.x*b.y - a.y*b.x
        );
	}

	template <typename T>
	constexpr vector<T, 2> orthogonal(vector<T, 2> const & a)
	{
		return vector<T, 2>(-a.y, a.x);
	}

	template <typename T>
	constexpr vector<T, 3> orthogonal(vector<T, 3> const & a)
	{
		// Implementation due to Sam Hocevar - see blog post:
		// http://lolengine.net/blog/2013/09/21/picking-orthogonal-vector-combing-coconuts
        return (abs(a.x) > abs(a.z)) 
            ? vector<T, 3>(-a.y, a.x, T(0)) 
            : vector<T, 3>(T(0), -a.z, a.y);
	}



	// Utilities for bool vectors

	template<int n> bool any(vector<bool, n> const & a);
    template<> constexpr bool any(const vector<bool, 2>& a) { return a.x || a.y; }
    template<> constexpr bool any(const vector<bool, 3>& a) { return a.x || a.y || a.z; }
    template<> constexpr bool any(const vector<bool, 4>& a) { return a.x || a.y || a.z || a.w; }

    template<int n> bool all(vector<bool, n> const & a);
    template<> constexpr bool all(const vector<bool, 2>& a) { return a.x && a.y; }
    template<> constexpr bool all(const vector<bool, 3>& a) { return a.x && a.y && a.z; }
    template<> constexpr bool all(const vector<bool, 4>& a) { return a.x && a.y && a.z && a.w; }

    template<int n> vector<bool, n> bitvector(int bits);
    template<> constexpr bool2 bitvector(int bits) { return bool2((bits & 1) != 0, (bits & 2) != 0); }
    template<> constexpr bool3 bitvector(int bits) { return bool3((bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0); }
    template<> constexpr bool4 bitvector(int bits) { return bool4((bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0); }

	template<typename T, int n> vector<T, n> select(vector<bool, n> const & cond, vector<T, n> const & a, vector<T, n> const & b);
    template<typename T> constexpr vector<T, 2> select(const vector<bool, 2>& cond, const vector<T, 2>& a, const vector<T, 2>& b)
    {
        return vector<T, 2>(cond.x ? a.x : b.x, cond.y ? a.y : b.y);
    }
    template<typename T> constexpr vector<T, 3> select(const vector<bool, 3>& cond, const vector<T, 3>& a, const vector<T, 3>& b)
    {
        return vector<T, 3>(cond.x ? a.x : b.x, cond.y ? a.y : b.y, cond.z ? a.z : b.z);
    }
    template<typename T> constexpr vector<T, 4> select(const vector<bool, 4>& cond, const vector<T, 4>& a, const vector<T, 4>& b)
    {
        return vector<T, 4>(cond.x ? a.x : b.x, cond.y ? a.y : b.y, cond.z ? a.z : b.z, cond.w ? a.w : b.w);
    }

    template <typename T, int n>
    constexpr vector<T, n> min(vector<T, n> const & a, vector<T, n> const & b)
    {
        return select(a < b, a, b);
    }

    template <typename T, int n>
    constexpr vector<T, n> max(vector<T, n> const& a, vector<T, n> const& b)
    {
        return select(a < b, b, a);
    }

    template <typename T>
    constexpr vector<T, 2> min(vector<T, 2> const& a, vector<T, 2> const& b)
    {
        vector<T, 2> result;
        result.x = (a.x < b.x) ? a.x : b.x;
        result.y = (a.y < b.y) ? a.y : b.y;
        return result;
    }

    template <typename T>
    constexpr vector<T, 3> min(vector<T, 3> const& a, vector<T, 3> const& b)
    {
        vector<T, 3> result;
        result.x = (a.x < b.x) ? a.x : b.x;
        result.y = (a.y < b.y) ? a.y : b.y;
        result.z = (a.z < b.z) ? a.z : b.z;
        return result;
    }

    template <typename T>
    constexpr vector<T, 4> min(vector<T, 4> const& a, vector<T, 4> const& b)
    {
        vector<T, 4> result;
        result.x = (a.x < b.x) ? a.x : b.x;
        result.y = (a.y < b.y) ? a.y : b.y;
        result.z = (a.z < b.z) ? a.z : b.z;
        result.w = (a.w < b.w) ? a.w : b.w;
        return result;
    }

    template <typename T>
    constexpr vector<T, 2> max(vector<T, 2> const& a, vector<T, 2> const& b)
    {
        vector<T, 2> result;
        result.x = (a.x > b.x) ? a.x : b.x;
        result.y = (a.y > b.y) ? a.y : b.y;
        return result;
    }

    template <typename T>
    constexpr vector<T, 3> max(vector<T, 3> const& a, vector<T, 3> const& b)
    {
        vector<T, 3> result;
        result.x = (a.x > b.x) ? a.x : b.x;
        result.y = (a.y > b.y) ? a.y : b.y;
        result.z = (a.z > b.z) ? a.z : b.z;
        return result;
    }

    template <typename T>
    constexpr vector<T, 4> max(vector<T, 4> const& a, vector<T, 4> const& b)
    {
        vector<T, 4> result;
        result.x = (a.x > b.x) ? a.x : b.x;
        result.y = (a.y > b.y) ? a.y : b.y;
        result.z = (a.z > b.z) ? a.z : b.z;
        result.w = (a.w > b.w) ? a.w : b.w;
        return result;
    }

    template <typename T, int n> 
    constexpr vector<T, n> abs(vector<T, n> const & a)
    {
        return select(a < T(0), -a, a);
    }

    template <typename T>
    constexpr vector<T, 2> abs(vector<T, 2> const& a)
    {
        vector<T, 2> result;
        result.x = std::abs(a.x);
        result.y = std::abs(a.y);
        return result;
    }

    template <typename T>
    constexpr vector<T, 3> abs(vector<T, 3> const& a)
    {
        vector<T, 3> result;
        result.x = std::abs(a.x);
        result.y = std::abs(a.y);
        result.z = std::abs(a.z);
        return result;
    }

    template <typename T>
    constexpr vector<T, 4> abs(vector<T, 4> const& a)
    {
        vector<T, 4> result;
        result.x = std::abs(a.x);
        result.y = std::abs(a.y);
        result.z = std::abs(a.z);
        result.w = std::abs(a.w);
        return result;
    }

	template <typename T, int n>
	vector<T, n> saturate(vector<T, n> const & value)
		{ return clamp(value, vector<T, n>(T(0)), vector<T, n>(T(1))); }

	template <typename T, int n>
	T minComponent(vector<T, n> const & a)
	{
		T result = a[0];
		for (int i = 1; i < n; ++i)
			result = min(result, a[i]);
		return result;
	}

	template <typename T, int n>
	T maxComponent(vector<T, n> const & a)
	{
		T result = a[0];
		for (int i = 1; i < n; ++i)
			result = max(result, a[i]);
		return result;
	}

    template<int n>
    constexpr vector<float, n> degrees(vector<float, n> rad) 
    { 
        return rad * (180.f / PI_f);
    }

    template<int n>
    constexpr vector<float, n> radians(vector<float, n> deg)
    {
        return deg * (PI_f / 180.f);
    }

    float3 sphericalToCartesian(float azimuth, float elevation, float distance);
    float3 sphericalDegreesToCartesian(float azimuth, float elevation, float distance);

    void cartesianToSpherical(const float3& v, float& azimuth, float& elevation, float& distance);
    void cartesianToSphericalDegrees(const float3& v, float& azimuth, float& elevation, float& distance);

    template<int n> uint vectorToSnorm8(const vector<float, n>& v); // undefined
    template<> uint vectorToSnorm8<2>(const float2& v);
    template<> uint vectorToSnorm8<3>(const float3& v);
    template<> uint vectorToSnorm8<4>(const float4& v);

    template<int n> vector<float, n> snorm8ToVector(uint v); // undefined
    template<> float2 snorm8ToVector<2>(uint v);
    template<> float3 snorm8ToVector<3>(uint v);
    template<> float4 snorm8ToVector<4>(uint v);
}
