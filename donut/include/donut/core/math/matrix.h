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
#include <algorithm>

namespace donut::math
{

#define MATRIX_MEMBERS(T, rows, cols) \
        /* Conversions to C arrays of fixed size */ \
        typedef T(&array_t)[rows*cols]; \
        operator array_t () { return m_data; } \
        typedef const T(&const_array_t)[rows*cols]; \
        operator const_array_t () const { return m_data; } \
        /* Subscript operators - built-in subscripts are ambiguous without these */ \
        vector<T, cols> & operator [] (int i) \
        { return reinterpret_cast<vector<T, cols> &>(m_data[i*cols]); } \
        const vector<T, cols> & operator [] (int i) const \
        { return reinterpret_cast<const vector<T, cols> &>(m_data[i*cols]); } \
        /* Generic constructors */ \
        matrix() { } \
        matrix(const T* v) \
            { for (int i = 0; i < rows*cols; ++i) m_data[i] = v[i]; } \
        /* Column accessor */ \
        vector<T, rows> col(int j) const { vector<T, rows> v; for (int i = 0; i < rows; i++) \
            v[i] = m_data[i * cols + j]; return v; } \
        /* Conversion to bool is not allowed (otherwise would \
           happen implicitly through array conversions) */ \
        private: operator bool();

    // Generic matrix struct, providing (row-major) storage,
    // conversion and subscript operators

    template <typename T, int rows, int cols>
    struct matrix
    {
        cassert(rows > 1);
        cassert(cols > 1);

        T m_data[rows*cols];

        matrix(T a)
        {
            for (int i = 0; i < rows * cols; ++i)
                m_data[i] = a;
        }

        static matrix diagonal(vector<T, (rows < cols) ? rows : cols> v)
        {
            matrix result;
            for (int i = 0; i < rows; ++i)
                for (int j = 0; j < cols; ++j)
                    result[i][j] = (i == j) ? v[i] : T(0);
            return result;
        }

        static matrix diagonal(T a)
        {
            matrix result;
            for (int i = 0; i < rows; ++i)
                for (int j = 0; j < cols; ++j)
                    result[i][j] = (i == j) ? a : T(0);
            return result;
        }

        static matrix identity()
        {
            return diagonal(T(1));
        }

        static matrix zero()
        {
            return matrix(T(0));
        }
		
        MATRIX_MEMBERS(T, rows, cols)
    };

#pragma warning(push)
#pragma warning(disable: 4201)	// Nameless struct/union

    template <typename T>
    struct matrix<T, 2, 2>
    {
        union
        {  
            T m_data[2 * 2];
            struct { T 
                m00, m01, 
                m10, m11; };
            struct { vector<T, 2> row0, row1; };
        };

        constexpr matrix(T a) 
            : m00(a), m01(a)
            , m10(a), m11(a) { }

        constexpr matrix(T _m00, T _m01, T _m10, T _m11) 
            : m00(_m00), m01(_m01)
            , m10(_m10), m11(_m11) { }

        constexpr matrix(const vector<T, 2>& _row0, const vector<T, 2>& _row1)
            : m00(_row0.x), m01(_row0.y)
            , m10(_row1.x), m11(_row1.y) { }

		template<typename U>
		explicit constexpr matrix(const matrix<U, 2, 2>& m)
		{
			for (int i = 0; i < 4; ++i)
				m_data[i] = T(m.m_data[i]);
		}

        constexpr static matrix from_cols(const vector<T, 2>& col0, const vector<T, 2>& col1)
        { 
            return matrix(
                col0.x, col1.x, 
                col0.y, col1.y ); 
        }

        constexpr static matrix diagonal(T diag)
        {
            return matrix(
                diag, T(0),
                T(0), diag);
        }

        constexpr static matrix diagonal(vector<T, 2> v)
        {
            return matrix(
                v.x, T(0),
                T(0), v.y);
        }

        constexpr static matrix identity()
        {
            return diagonal(T(1));
        }

        constexpr static matrix zero()
        {
            return matrix(T(0));
        }

        MATRIX_MEMBERS(T, 2, 2)
    };

    template <typename T>
    struct matrix<T, 3, 3>
    {
        union
        {
            T m_data[3 * 3];
            struct { T 
                m00, m01, m02, 
                m10, m11, m12, 
                m20, m21, m22; };
            struct { vector<T, 3> row0, row1, row2; };
        };

        constexpr matrix(T a) 
            : m00(a), m01(a), m02(a)
            , m10(a), m11(a), m12(a)
            , m20(a), m21(a), m22(a) { }

        constexpr matrix(T _m00, T _m01, T _m02, T _m10, T _m11, T _m12, T _m20, T _m21, T _m22) 
            : m00(_m00), m01(_m01), m02(_m02)
            , m10(_m10), m11(_m11), m12(_m12)
            , m20(_m20), m21(_m21), m22(_m22) { }

        constexpr matrix(const vector<T, 3>& _row0, const vector<T, 3>& _row1, const vector<T, 3>& _row2)
            : m00(_row0.x), m01(_row0.y), m02(_row0.z)
            , m10(_row1.x), m11(_row1.y), m12(_row1.z)
            , m20(_row2.x), m21(_row2.y), m22(_row2.z) { }

        constexpr matrix(const matrix<T, 3, 4>& m)
            : m00(m.m00), m01(m.m01), m02(m.m02)
            , m10(m.m10), m11(m.m11), m12(m.m12)
            , m20(m.m20), m21(m.m21), m22(m.m22) { }

        constexpr matrix(const matrix<T, 4, 4>& m)
            : m00(m.m00), m01(m.m01), m02(m.m02)
            , m10(m.m10), m11(m.m11), m12(m.m12)
            , m20(m.m20), m21(m.m21), m22(m.m22) { }

		template<typename U>
		explicit constexpr matrix(const matrix<U, 3, 3>& m)
		{
			for (int i = 0; i < 9; ++i)
				m_data[i] = T(m.m_data[i]);
		}

        constexpr static matrix from_cols(const vector<T, 3>& col0, const vector<T, 3>& col1, const vector<T, 3>& col2)
        {
            return matrix(
                col0.x, col1.x, col2.x,
                col0.y, col1.y, col2.y,
                col0.z, col1.z, col2.z );
        }

        constexpr static matrix diagonal(T diag)
        {
            return matrix(
                diag, T(0), T(0),
                T(0), diag, T(0),
                T(0), T(0), diag);
        }

        constexpr static matrix diagonal(vector<T, 3> v)
        {
            return matrix(
                v.x, T(0), T(0),
                T(0), v.y, T(0),
                T(0), T(0), v.z);
        }

        constexpr static matrix identity()
        {
            return diagonal(T(1));
        }

        constexpr static matrix zero()
        {
            return matrix(static_cast<T>(0));
        }

        MATRIX_MEMBERS(T, 3, 3)
    };

    template <typename T>
    struct matrix<T, 3, 4>
    {
        union
        {
            T m_data[3 * 4];
            struct { T 
                m00, m01, m02, m03, 
                m10, m11, m12, m13, 
                m20, m21, m22, m23; };
            struct { vector<T, 4> row0, row1, row2; };
        };

        constexpr matrix(T a)
            : m00(a), m01(a), m02(a), m03(a)
            , m10(a), m11(a), m12(a), m13(a)
            , m20(a), m21(a), m22(a), m23(a) { }

        constexpr matrix(T _m00, T _m01, T _m02, T _m03, T _m10, T _m11, T _m12, T _m13, T _m20, T _m21, T _m22, T _m23)
            : m00(_m00), m01(_m01), m02(_m02), m03(_m03)
            , m10(_m10), m11(_m11), m12(_m12), m13(_m13)
            , m20(_m20), m21(_m21), m22(_m22), m23(_m23) { }

        constexpr matrix(const vector<T, 4>& _row0, const vector<T, 4>& _row1, const vector<T, 4>& _row2)
            : m00(_row0.x), m01(_row0.y), m02(_row0.z), m03(_row0.w)
            , m10(_row1.x), m11(_row1.y), m12(_row1.z), m13(_row1.w)
            , m20(_row2.x), m21(_row2.y), m22(_row2.z), m23(_row2.w) { }

        constexpr matrix(const matrix<T, 3, 3>& m, const vector<T, 3>& col3)
            : m00(m.m00), m01(m.m01), m02(m.m02), m03(col3.x)
            , m10(m.m10), m11(m.m11), m12(m.m12), m13(col3.y)
            , m20(m.m20), m21(m.m21), m22(m.m22), m23(col3.z) { }

        constexpr matrix(const matrix<T, 4, 4>& m)
            : m00(m.m00), m01(m.m01), m02(m.m02), m03(m.m03)
            , m10(m.m10), m11(m.m11), m12(m.m12), m13(m.m13)
            , m20(m.m20), m21(m.m21), m22(m.m22), m23(m.m23) { }

		template<typename U>
		explicit constexpr matrix(const matrix<U, 3, 4>& m)
		{
			for (int i = 0; i < 12; ++i)
				m_data[i] = T(m.m_data[i]);
		}

        constexpr static matrix from_cols(const vector<T, 3>& col0, const vector<T, 3>& col1, const vector<T, 3>& col2, const vector<T, 3>& col3)
        {
            return matrix(
                col0.x, col1.x, col2.x, col3.x,
                col0.y, col1.y, col2.y, col3.y,
                col0.z, col1.z, col2.z, col3.z);
        }

        constexpr static matrix diagonal(T diag)
        {
            return matrix(
                diag, T(0), T(0), T(0),
                T(0), diag, T(0), T(0),
                T(0), T(0), diag, T(0));
        }

        constexpr static matrix diagonal(vector<T, 3> v)
        {
            return matrix(
                v.x, T(0), T(0), T(0),
                T(0), v.y, T(0), T(0),
                T(0), T(0), v.z, T(0));
        }

        constexpr static matrix identity()
        {
            return diagonal(T(1));
        }

        constexpr static matrix zero()
        {
            return matrix(static_cast<T>(0));
        }

        MATRIX_MEMBERS(T, 3, 4)
    };

    template <typename T>
    struct matrix<T, 4, 4>
    {
        union
        {
            T m_data[4 * 4];
            struct { T 
                m00, m01, m02, m03, 
                m10, m11, m12, m13, 
                m20, m21, m22, m23, 
                m30, m31, m32, m33; };
            struct { vector<T, 4> row0, row1, row2, row3; };
        };

        constexpr matrix(T a)
            : m00(a), m01(a), m02(a), m03(a)
            , m10(a), m11(a), m12(a), m13(a)
            , m20(a), m21(a), m22(a), m23(a)
            , m30(a), m31(a), m32(a), m33(a) { }

        constexpr matrix(T _m00, T _m01, T _m02, T _m03, T _m10, T _m11, T _m12, T _m13, T _m20, T _m21, T _m22, T _m23, T _m30, T _m31, T _m32, T _m33)
            : m00(_m00), m01(_m01), m02(_m02), m03(_m03)
            , m10(_m10), m11(_m11), m12(_m12), m13(_m13)
            , m20(_m20), m21(_m21), m22(_m22), m23(_m23)
            , m30(_m30), m31(_m31), m32(_m32), m33(_m33) { }

        constexpr matrix(const vector<T, 4>& _row0, const vector<T, 4>& _row1, const vector<T, 4>& _row2, const vector<T, 4>& _row3)
            : m00(_row0.x), m01(_row0.y), m02(_row0.z), m03(_row0.w)
            , m10(_row1.x), m11(_row1.y), m12(_row1.z), m13(_row1.w)
            , m20(_row2.x), m21(_row2.y), m22(_row2.z), m23(_row2.w)
            , m30(_row3.x), m31(_row3.y), m32(_row3.z), m33(_row3.w) { }

        constexpr matrix(const matrix<T, 3, 4>& m, const vector<T, 4>& _row3)
            : m00(m.m00), m01(m.m01), m02(m.m02), m03(m.m03)
            , m10(m.m10), m11(m.m11), m12(m.m12), m13(m.m13)
            , m20(m.m20), m21(m.m21), m22(m.m22), m23(m.m23)
            , m30(_row3.x), m31(_row3.y), m32(_row3.z), m33(_row3.w) { }

		template<typename U>
		explicit constexpr matrix(const matrix<U, 4, 4>& m)
		{
			for (int i = 0; i < 16; ++i)
				m_data[i] = T(m.m_data[i]);
		}

        constexpr static matrix from_cols(const vector<T, 4>& col0, const vector<T, 4>& col1, const vector<T, 4>& col2, const vector<T, 4>& col3)
        {
            return matrix(
                col0.x, col1.x, col2.x, col3.x,
                col0.y, col1.y, col2.y, col3.y,
                col0.z, col1.z, col2.z, col3.z,
                col0.w, col1.w, col2.w, col3.w );
        }

        constexpr static matrix diagonal(T diag)
        {
            return matrix(
                diag, T(0), T(0), T(0),
                T(0), diag, T(0), T(0),
                T(0), T(0), diag, T(0),
                T(0), T(0), T(0), diag);
        }

        constexpr static matrix diagonal(vector<T, 4> v)
        {
            return matrix(
                v.x, T(0), T(0), T(0),
                T(0), v.y, T(0), T(0),
                T(0), T(0), v.z, T(0),
                T(0), T(0), T(0), v.w);
        }

        constexpr static matrix identity()
        {
            return diagonal(T(1));
        }

        constexpr static matrix zero()
        {
            return matrix(static_cast<T>(0));
        }

        MATRIX_MEMBERS(T, 3, 4)
    };
#pragma warning(pop)
#undef MATRIX_MEMBERS
    

	// Concrete matrices for the most common types and dimensions

#define DEFINE_CONCRETE_MATRICES(type) \
			typedef matrix<type, 2, 2> type##2x2; \
			typedef matrix<type, 3, 3> type##3x3; \
			typedef matrix<type, 3, 4> type##3x4; \
			typedef matrix<type, 4, 4> type##4x4;

	DEFINE_CONCRETE_MATRICES(float);
	DEFINE_CONCRETE_MATRICES(double);
	DEFINE_CONCRETE_MATRICES(int);
	DEFINE_CONCRETE_MATRICES(uint);
	DEFINE_CONCRETE_MATRICES(bool);

#undef DEFINE_CONCRETE_MATRICES



	// Overloaded math operators

#define DEFINE_UNARY_OPERATOR(op) \
			template <typename T, int rows, int cols> \
			matrix<T, rows, cols> operator op (matrix<T, rows, cols> const & a) \
			{ \
				matrix<T, rows, cols> result; \
				for (int i = 0; i < rows*cols; ++i) \
					result.m_data[i] = op a.m_data[i]; \
				return result; \
			}

#define DEFINE_BINARY_SCALAR_OPERATORS(op) \
			/* Scalar-matrix op */ \
			template <typename T, int rows, int cols> \
			matrix<T, rows, cols> operator op (T a, matrix<T, rows, cols> const & b) \
			{ \
				matrix<T, rows, cols> result; \
				for (int i = 0; i < rows*cols; ++i) \
					result.m_data[i] = a op b.m_data[i]; \
				return result; \
			} \
			/* Matrix-scalar op */ \
			template <typename T, int rows, int cols> \
			matrix<T, rows, cols> operator op (matrix<T, rows, cols> const & a, T b) \
			{ \
				matrix<T, rows, cols> result; \
				for (int i = 0; i < rows*cols; ++i) \
					result.m_data[i] = a.m_data[i] op b; \
				return result; \
			}

#define DEFINE_BINARY_OPERATORS(op) \
			/* Matrix-matrix op */ \
			template <typename T, int rows, int cols> \
			matrix<T, rows, cols> operator op (matrix<T, rows, cols> const & a, matrix<T, rows, cols> const & b) \
			{ \
				matrix<T, rows, cols> result; \
				for (int i = 0; i < rows*cols; ++i) \
					result.m_data[i] = a.m_data[i] op b.m_data[i]; \
				return result; \
			} \
			DEFINE_BINARY_SCALAR_OPERATORS(op)

#define DEFINE_INPLACE_SCALAR_OPERATOR(op) \
			/* Matrix-scalar op */ \
			template <typename T, int rows, int cols> \
			matrix<T, rows, cols> & operator op (matrix<T, rows, cols> & a, T b) \
			{ \
				for (int i = 0; i < rows*cols; ++i) \
					a.m_data[i] op b; \
				return a; \
			}

#define DEFINE_INPLACE_OPERATORS(op) \
			/* Matrix-matrix op */ \
			template <typename T, int rows, int cols> \
			matrix<T, rows, cols> & operator op (matrix<T, rows, cols> & a, matrix<T, rows, cols> const & b) \
			{ \
				for (int i = 0; i < rows*cols; ++i) \
					a.m_data[i] op b.m_data[i]; \
				return a; \
			} \
			DEFINE_INPLACE_SCALAR_OPERATOR(op)

#define DEFINE_RELATIONAL_OPERATORS(op) \
			/* Matrix-matrix op */ \
			template <typename T, int rows, int cols> \
			matrix<bool, rows, cols> operator op (matrix<T, rows, cols> const & a, matrix<T, rows, cols> const & b) \
			{ \
				matrix<bool, rows, cols> result; \
				for (int i = 0; i < rows*cols; ++i) \
					result.m_data[i] = a.m_data[i] op b.m_data[i]; \
				return result; \
			} \
			/* Scalar-matrix op */ \
			template <typename T, int rows, int cols> \
			matrix<bool, rows, cols> operator op (T a, matrix<T, rows, cols> const & b) \
			{ \
				matrix<bool, rows, cols> result; \
				for (int i = 0; i < rows*cols; ++i) \
					result.m_data[i] = a op b.m_data[i]; \
				return result; \
			} \
			/* Matrix-scalar op */ \
			template <typename T, int rows, int cols> \
			matrix<bool, rows, cols> operator op (matrix<T, rows, cols> const & a, T b) \
			{ \
				matrix<bool, rows, cols> result; \
				for (int i = 0; i < rows*cols; ++i) \
					result.m_data[i] = a.m_data[i] op b; \
				return result; \
			}

	DEFINE_BINARY_OPERATORS(+);
	DEFINE_BINARY_OPERATORS(-);
	DEFINE_UNARY_OPERATOR(-);
	DEFINE_BINARY_SCALAR_OPERATORS(*);
	DEFINE_BINARY_SCALAR_OPERATORS(/);
	DEFINE_BINARY_OPERATORS(&);
	DEFINE_BINARY_OPERATORS(|);
	DEFINE_BINARY_OPERATORS(^);
	DEFINE_UNARY_OPERATOR(!);
	DEFINE_UNARY_OPERATOR(~);

	DEFINE_INPLACE_OPERATORS(+=);
	DEFINE_INPLACE_OPERATORS(-=);
	DEFINE_INPLACE_SCALAR_OPERATOR(*=);
	DEFINE_INPLACE_SCALAR_OPERATOR(/=);
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
#undef DEFINE_BINARY_SCALAR_OPERATORS
#undef DEFINE_BINARY_OPERATORS
#undef DEFINE_INPLACE_SCALAR_OPERATOR
#undef DEFINE_INPLACE_OPERATORS
#undef DEFINE_RELATIONAL_OPERATORS

	// Matrix multiplication

	template <typename T, int rows, int inner, int cols>
	matrix<T, rows, cols> operator * (matrix<T, rows, inner> const & a, matrix<T, inner, cols> const & b)
	{
		auto result = matrix<T, rows, cols>::zero();
		for (int i = 0; i < rows; ++i)
			for (int j = 0; j < cols; ++j)
				for (int k = 0; k < inner; ++k)
					result[i][j] += a[i][k] * b[k][j];
		return result;
	}

	template <typename T, int rows, int cols>
	matrix<T, rows, cols> & operator *= (matrix<T, rows, cols> & a, matrix<T, cols, cols> const & b)
	{
		a = a*b;
		return a;
	}

	// Matrix-vector multiplication

	template <typename T, int rows, int cols>
	vector<T, rows> operator * (matrix<T, rows, cols> const & a, vector<T, cols> const & b)
	{
        auto result = vector<T, rows>::zero();
		for (int i = 0; i < rows; ++i)
			for (int j = 0; j < cols; ++j)
					result[i] += a[i][j] * b[j];
		return result;
	}

	template <typename T, int rows, int cols>
	vector<T, cols> operator * (vector<T, rows> const & a, matrix<T, rows, cols> const & b)
	{
		auto result = vector<T, cols>::zero();
		for (int i = 0; i < rows; ++i)
			for (int j = 0; j < cols; ++j)
					result[j] += a[i] * b[i][j];
		return result;
	}

	template <typename T>
	vector<T, 3> operator * (matrix<T, 3, 3> const& a, vector<T, 3> const& b)
	{
		vector<T, 3> result;
		result.x = a.row0.x * b.x + a.row0.y * b.y + a.row0.z * b.z;
		result.y = a.row1.x * b.x + a.row1.y * b.y + a.row1.z * b.z;
		result.z = a.row2.x * b.x + a.row2.y * b.y + a.row2.z * b.z;
		return result;
	}

	template <typename T>
	vector<T, 3> operator * (vector<T, 3> const& a, matrix<T, 3, 3> const& b)
	{
		vector<T, 3> result;
		result.x = a.x * b.row0.x + a.y * b.row1.x + a.z * b.row2.x;
		result.y = a.x * b.row0.y + a.y * b.row1.y + a.z * b.row2.y;
		result.z = a.x * b.row0.z + a.y * b.row1.z + a.z * b.row2.z;
		return result;
	}

	template <typename T>
	vector<T, 4> operator * (matrix<T, 4, 4> const& a, vector<T, 4> const& b)
	{
		vector<T, 4> result;
		result.x = a.row0.x * b.x + a.row0.y * b.y + a.row0.z * b.z + a.row0.w * b.w;
		result.y = a.row1.x * b.x + a.row1.y * b.y + a.row1.z * b.z + a.row1.w * b.w;
		result.z = a.row2.x * b.x + a.row2.y * b.y + a.row2.z * b.z + a.row2.w * b.w;
		result.w = a.row3.x * b.x + a.row3.y * b.y + a.row3.z * b.z + a.row3.w * b.w;
		return result;
	}

	template <typename T>
	vector<T, 4> operator * (vector<T, 4> const& a, matrix<T, 4, 4> const& b)
	{
		vector<T, 4> result;
		result.x = a.x * b.row0.x + a.y * b.row1.x + a.z * b.row2.x + a.w * b.row3.x;
		result.y = a.x * b.row0.y + a.y * b.row1.y + a.z * b.row2.y + a.w * b.row3.y;
		result.z = a.x * b.row0.z + a.y * b.row1.z + a.z * b.row2.z + a.w * b.row3.z;
		result.w = a.x * b.row0.w + a.y * b.row1.w + a.z * b.row2.w + a.w * b.row3.w;
		return result;
	}

	template <typename T, int n>
	vector<T, n> operator *= (vector<T, n> & a, matrix<T, n, n> const & b)
	{
		a = a*b;
		return a;
	}



	// Other math functions

	template <typename T, int rows, int cols>
	matrix<T, cols, rows> transpose(matrix<T, rows, cols> const & a)
	{
		matrix<T, cols, rows> result;
		for (int i = 0; i < rows; ++i)
			for (int j = 0; j < cols; ++j)
				result[j][i] = a[i][j];
		return result;
	}

	template <typename T, int n>
	matrix<T, n, n> pow(matrix<T, n, n> const & a, int b)
	{
		if (b <= 0)
			return matrix<T, n, n>::identity();
		if (b == 1)
			return a;
		auto oddpart = matrix<T, n, n>::identity(), evenpart = a;
		while (b > 1)
		{
			if (b % 2 == 1)
				oddpart *= evenpart;

			evenpart *= evenpart;
			b /= 2;
		}
		return oddpart * evenpart;
	}

	template <typename T, int n>
	matrix<T, n, n> inverse(matrix<T, n, n> const & m)
	{
		// Calculate inverse using Gaussian elimination

		matrix<T, n, n> a = m;
		auto b = matrix<T, n, n>::identity();

		// Loop through columns
		for (int j = 0; j < n; ++j)
		{
			// Select pivot element: maximum magnitude in this column at or below main diagonal
			int pivot = j;
			for (int i = j+1; i < n; ++i)
				if (abs(a[i][j]) > abs(a[pivot][j]))
					pivot = i;
			if (abs(a[pivot][j]) < epsilon)
				return matrix<T, n, n>(NaN);

			// Interchange rows to put pivot element on the diagonal,
			// if it is not already there
			if (pivot != j)
			{
				std::swap(a[j], a[pivot]);
                std::swap(b[j], b[pivot]);
			}

			// Divide the whole row by the pivot element
			if (a[j][j] != T(1))								// Skip if already equal to 1
			{
				T scale = a[j][j];
				a[j] /= scale;
				b[j] /= scale;
				// Now the pivot element has become 1
			}

			// Subtract this row from others to make the rest of column j zero
			for (int i = 0; i < n; ++i)
			{
				if ((i != j) && (abs(a[i][j]) > epsilon))		// skip rows already zero
				{
					T scale = -a[i][j];
					a[i] += a[j] * scale;
					b[i] += b[j] * scale;
				}
			}
		}
	
		// At this point, a should have been transformed to the identity matrix,
		// and b should have been transformed into the inverse of the original a.
		return b;
	}

	// Inverse specialization for 2x2
	template <typename T>
	matrix<T, 2, 2> inverse(matrix<T, 2, 2> const & a)
	{
		matrix<T, 2, 2> result = { a[1][1], -a[0][1], -a[1][0], a[0][0] };
		return result / determinant(a);
	}
	
	template <typename T, int n>
	T determinant(matrix<T, n, n> const & m)
	{
		// Calculate determinant using Gaussian elimination

		matrix<T, n, n> a = m;
		T result(1);

		// Loop through columns
		for (int j = 0; j < n; ++j)
		{
			// Select pivot element: maximum magnitude in this column at or below main diagonal
			int pivot = j;
			for (int i = j+1; i < n; ++i)
				if (abs(a[i][j]) > abs(a[pivot][j]))
					pivot = i;
			if (abs(a[pivot][j]) < epsilon)
				return T(0);

			// Interchange rows to put pivot element on the diagonal,
			// if it is not already there
			if (pivot != j)
			{
				std::swap(a[j], a[pivot]);
				result *= T(-1);
			}

			// Divide the whole row by the pivot element
			if (a[j][j] != T(1))								// Skip if already equal to 1
			{
				T scale = a[j][j];
				a[j] /= scale;
				result *= scale;
				// Now the pivot element has become 1
			}

			// Subtract this row from others to make the rest of column j zero
			for (int i = 0; i < n; ++i)
			{
				if ((i != j) && (abs(a[i][j]) > epsilon))		// skip rows already zero
				{
					T scale = -a[i][j];
					a[i] += a[j] * scale;
				}
			}
		}
	
		// At this point, a should have been transformed to the identity matrix,
		// and we've accumulated the original a's determinant in result.
		return result;
	}

	// Determinant specialization for 2x2
	template <typename T>
	T determinant(matrix<T, 2, 2> const & a)
	{
		return (a[0][0]*a[1][1] - a[0][1]*a[1][0]);
	}

	// Determinant specialization for 3x3
	template <typename T>
	T determinant(matrix<T, 3, 3> const& a)
	{
		return (a[0][0]*a[1][1]*a[2][2] + a[0][1]*a[1][2]*a[2][0] + a[0][2]*a[1][0]*a[2][1])
		     - (a[2][0]*a[1][1]*a[0][2] + a[2][1]*a[1][2]*a[0][0] + a[2][2]*a[1][0]*a[0][1]);
	}

	// !!!UNDONE: specialization for 3x3? worth it?

	template <typename T, int n>
	T trace(matrix<T, n, n> const & a)
	{
		T result(0);
		for (int i = 0; i < n; ++i)
			result += a[i][i];
		return result;
	}

	// !!!UNDONE: diagonalization and decomposition?

	template <typename T, int n>
	matrix<T, n, n> diagonal(T a)
	{
        return matrix<T, n, n>::diagonal(a);
	}

	template <typename T, int n>
	matrix<T, n, n> diagonal(vector<T, n> const & a)
	{
        return matrix<T, n, n>::diagonal(a);
	}

	template <typename T, int rows, int cols>
	matrix<T, rows, cols> outerProduct(vector<T, rows> const & a, vector<T, cols> const & b)
	{
		matrix<T, rows, cols> result;
		for (int i = 0; i < rows; ++i)
			result[i] = a[i] * b;
		return result;
	}

	template <typename T, int rows, int cols>
	matrix<bool, rows, cols> isnear(matrix<T, rows, cols> const & a, matrix<T, rows, cols> const & b, float epsilon = dm::epsilon)
	{
		matrix<bool, rows, cols> result;
		for (int i = 0; i < rows*cols; ++i)
			result.m_data[i] = isnear(a.m_data[i], b.m_data[i], epsilon);
		return result;
	}

	template <typename T, int rows, int cols>
	matrix<bool, rows, cols> isnear(matrix<T, rows, cols> const & a, T b, float epsilon = dm::epsilon)
	{
		matrix<bool, rows, cols> result;
		for (int i = 0; i < rows*cols; ++i)
			result.m_data[i] = isnear(a.m_data[i], b, epsilon);
		return result;
	}

	template <typename T, int rows, int cols>
	matrix<bool, rows, cols> isnear(T a, matrix<T, rows, cols> const & b, float epsilon = dm::epsilon)
	{
		matrix<bool, rows, cols> result;
		for (int i = 0; i < rows*cols; ++i)
			result.m_data[i] = isnear(a, b.m_data[i], epsilon);
		return result;
	}

	template <typename T, int rows, int cols>
	matrix<bool, rows, cols> isfinite(matrix<T, rows, cols> const & a)
	{
		matrix<bool, rows, cols> result;
		for (int i = 0; i < rows*cols; ++i)
			result.m_data[i] = isfinite(a.m_data[i]);
		return result;
	}

	template <typename T, int rows, int cols>
	matrix<int, rows, cols> round(matrix<T, rows, cols> const & a)
	{
		matrix<int, rows, cols> result;
		for (int i = 0; i < rows*cols; ++i)
			result.m_data[i] = round(a.m_data[i]);
		return result;
	}



	// Utilities for bool matrices

	template <int rows, int cols>
	bool any(matrix<bool, rows, cols> const & a)
	{
		bool result = false;
		for (int i = 0; i < rows*cols; ++i)
			result = result || a.m_data[i];
		return result;
	}

	template <int rows, int cols>
	bool all(matrix<bool, rows, cols> const & a)
	{
		bool result = true;
		for (int i = 0; i < rows*cols; ++i)
			result = result && a.m_data[i];
		return result;
	}

	template <typename T, int rows, int cols>
	matrix<T, rows, cols> select(matrix<bool, rows, cols> const & cond, matrix<T, rows, cols> const & a, matrix<T, rows, cols> const & b)
	{
		matrix<T, rows, cols> result;
		for (int i = 0; i < rows*cols; ++i)
			result.m_data[i] = cond.m_data[i] ? a.m_data[i] : b.m_data[i];
		return result;
	}

	template <typename T, int rows, int cols>
	matrix<T, rows, cols> min(matrix<T, rows, cols> const & a, matrix<T, rows, cols> const & b)
		{ return select(a < b, a, b); }

	template <typename T, int rows, int cols>
	matrix<T, rows, cols> max(matrix<T, rows, cols> const & a, matrix<T, rows, cols> const & b)
		{ return select(a < b, b, a); }

	template <typename T, int rows, int cols>
	matrix<T, rows, cols> abs(matrix<T, rows, cols> const & a)
		{ return select(a < T(0), -a, a); }

	template <typename T, int rows, int cols>
	matrix<T, rows, cols> saturate(matrix<T, rows, cols> const & value)
		{ return clamp(value, matrix<T, rows, cols>::zero(), matrix<T, rows, cols>(static_cast<T>(1))); }

	template <typename T, int rows, int cols>
	T minComponent(matrix<T, rows, cols> const & a)
	{
		T result = a.m_data[0];
		for (int i = 1; i < rows*cols; ++i)
			result = min(result, a.m_data[i]);
		return result;
	}

	template <typename T, int rows, int cols>
	T maxComponent(matrix<T, rows, cols> const & a)
	{
		T result = a.m_data[0];
		for (int i = 1; i < rows*cols; ++i)
			result = max(result, a.m_data[i]);
		return result;
	}



	// Generate standard projection matrices (row-vector math).
	// "D3D style" means left-handed view space, right-handed z in [0, 1] after projection;
    // "OGL style" means right-handed view space, right-handed z in [-1, 1] after projection.

	float4x4 orthoProjD3DStyle(float left, float right, float bottom, float top, float zNear, float zFar);
	float4x4 orthoProjOGLStyle(float left, float right, float bottom, float top, float zNear, float zFar);

	float4x4 perspProjD3DStyle(float left, float right, float bottom, float top, float zNear, float zFar);
	float4x4 perspProjOGLStyle(float left, float right, float bottom, float top, float zNear, float zFar);
	float4x4 perspProjD3DStyleReverse(float left, float right, float bottom, float top, float zNear);

	float4x4 perspProjD3DStyle(float verticalFOV, float aspect, float zNear, float zFar);
	float4x4 perspProjOGLStyle(float verticalFOV, float aspect, float zNear, float zFar);
	float4x4 perspProjD3DStyleReverse(float verticalFOV, float aspect, float zNear);
}
