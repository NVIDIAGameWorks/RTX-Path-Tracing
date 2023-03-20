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

namespace donut::math
{
	template<typename T>
	struct quaternion
	{
		T w, x, y, z;
		
		quaternion() : w(1.f), x(0.f), y(0.f), z(0.f) { }
		quaternion(T w, T x, T y, T z) : w(w), x(x), y(y), z(z) { }

		template<typename U>
		explicit quaternion(const quaternion<U>& q) : w(T(q.w)), x(T(q.x)), y(T(q.y)), z(T(q.z)) { }

		static quaternion identity()
		{
			return quaternion();
		}

		static quaternion fromWXYZ(T w, const vector<T, 3>& v)
		{
			return quaternion(w, v.x, v.y, v.z);
		}

		static quaternion fromWXYZ(const T* v)
		{
			return quaternion(v[0], v[1], v[2], v[3]);
		}

		static quaternion fromXYZW(const vector<T, 4>& v)
		{
			return quaternion(v.w, v.x, v.y, v.z);
		}

		static quaternion fromXYZW(const vector<T, 3>& v, T w)
		{
			return quaternion(w, v.x, v.y, v.z);
		}

		static quaternion fromXYZW(const T* v)
		{
			return quaternion(v[3], v[0], v[1], v[2]);
		}

		[[nodiscard]] vector<T, 4> toXYZW() const
		{
			return vector(x, y, z, w);
		}

		[[nodiscard]] vector<T, 4> toWXYZ() const
		{
			return vector(w, x, y, z);
		}

		// Conversions to C arrays of fixed size
		typedef T (&array_t)[4];
		operator array_t ()
			{ return (array_t)&w; }

		typedef const T (&const_array_t)[4];
		operator const_array_t () const
			{ return (const_array_t)&w; }

		// Subscript operators - built-in subscripts are ambiguous without these
		T & operator [] (int i)
			{ return &w[i]; }
		const T & operator [] (int i) const
			{ return &w[i]; }

		// Convert to a matrix
		[[nodiscard]] matrix<T, 3, 3> toMatrix() const
		{
			return matrix<T, 3, 3>(
							1 - 2*(y*y + z*z), 2*(x*y + z*w), 2*(x*z - y*w),
							2*(x*y - z*w), 1 - 2*(x*x + z*z), 2*(y*z + x*w),
							2*(x*z + y*w), 2*(y*z - x*w), 1 - 2*(x*x + y*y));
		}

		// Convert to an affine transform
		[[nodiscard]] affine<T, 3> toAffine() const
		{
			return affine<T, 3>(toMatrix(), T(0));
		}

		// Conversion to bool is not allowed (otherwise would
		// happen implicitly through array conversions)
		operator bool() = delete;
	};
	
	using quat = quaternion<float>;
	using dquat = quaternion<double>;
	
	// Overloaded math operators

#define DEFINE_UNARY_OPERATOR(op) \
			template<typename T> \
			quaternion<T> operator op (const quaternion<T>& a) \
			{ \
			    return quaternion<T>(op a.w, op a.x, op a.y, op a.z); \
			}

#define DEFINE_BINARY_SCALAR_OPERATORS(op) \
			/* Scalar-quat op */ \
			template<typename T> \
			quaternion<T> operator op (T a, const quaternion<T>& b) \
			{ \
				return quaternion<T>(a op b.w, a op b.x, a op b.y, a op b.z); \
			} \
			/* Quat-scalar op */ \
			template<typename T> \
			quaternion<T> operator op (const quaternion<T>& a, T b) \
			{ \
				return quaternion<T>(a.w op b, a.x op b, a.y op b, a.z op b); \
			}

#define DEFINE_BINARY_OPERATORS(op) \
			/* Quat-quat op */ \
			template<typename T> \
			quat operator op (const quaternion<T>& a, const quaternion<T>& b) \
			{ \
				return quaternion<T>(a.w op b.w, a.x op b.x, a.y op b.y, a.z op b.z); \
			} \
			DEFINE_BINARY_SCALAR_OPERATORS(op)

#define DEFINE_INPLACE_SCALAR_OPERATOR(op) \
			/* Quat-scalar op */ \
			template<typename T> \
			quaternion<T> & operator op (quaternion<T> & a, T b) \
			{ \
				a.w op b; \
				a.x op b; \
				a.y op b; \
				a.z op b; \
				return a; \
			}

#define DEFINE_INPLACE_OPERATORS(op) \
			/* Quat-quat op */ \
			template<typename T> \
			quaternion<T> & operator op (quaternion<T> & a, const quaternion<T>& b) \
			{ \
				a.w op b.w; \
				a.x op b.x; \
				a.y op b.y; \
				a.z op b.z; \
				return a; \
			} \
			DEFINE_INPLACE_SCALAR_OPERATOR(op)

#define DEFINE_RELATIONAL_OPERATORS(op) \
			/* Quat-quat op */ \
			template<typename T> \
			bool4 operator op (const quaternion<T>& a, const quaternion<T>& b) \
			{ \
				bool4 result; \
				result.x = a.w op b.w; \
				result.y = a.x op b.x; \
				result.z = a.y op b.y; \
				result.w = a.z op b.z; \
				return result; \
			} \
			/* Scalar-quat op */ \
			template<typename T> \
			bool4 operator op (T a, const quaternion<T>& b) \
			{ \
				bool4 result; \
				result.x = a op b.w; \
				result.y = a op b.x; \
				result.z = a op b.y; \
				result.w = a op b.z; \
				return result; \
			} \
			/* Quat-scalar op */ \
			template<typename T> \
			bool4 operator op (const quaternion<T>& a, T b) \
			{ \
				bool4 result; \
				result.x = a.w op b; \
				result.y = a.x op b; \
				result.z = a.y op b; \
				result.w = a.z op b; \
				return result; \
			}

	DEFINE_BINARY_OPERATORS(+);
	DEFINE_BINARY_OPERATORS(-);
	DEFINE_UNARY_OPERATOR(-);
	DEFINE_BINARY_SCALAR_OPERATORS(*);
	DEFINE_BINARY_SCALAR_OPERATORS(/);

	DEFINE_INPLACE_OPERATORS(+=);
	DEFINE_INPLACE_OPERATORS(-=);
	DEFINE_INPLACE_SCALAR_OPERATOR(*=);
	DEFINE_INPLACE_SCALAR_OPERATOR(/=);

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

	// Quaternion multiplication

	template<typename T>
	quaternion<T> operator * (const quaternion<T>& a, const quaternion<T>& b)
	{
		return quaternion<T>(
				a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
				a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
				a.w*b.y + a.y*b.w + a.z*b.x - a.x*b.z,
				a.w*b.z + a.z*b.w + a.x*b.y - a.y*b.x);
	}

	template<typename T>
	quaternion<T> & operator *= (quaternion<T>& a, const quaternion<T>& b)
	{
		a = a*b;
		return a;
	}



	// Other math functions

	template<typename T>
	T dot(const quaternion<T>& a, const quaternion<T>& b)
		{ return a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z; }

	template<typename T>
	T lengthSquared(const quaternion<T>& a)
		{ return dot(a, a); }

	template<typename T>
	T length(const quaternion<T>& a)
		{ return std::sqrt(lengthSquared(a)); }

	template<typename T>
	quaternion<T> normalize(const quaternion<T>& a)
		{ return a / length(a); }

	template<typename T>
	quaternion<T> conjugate(const quaternion<T>& a)
		{ return quaternion<T>(a.w, -a.x, -a.y, -a.z); }

	template<typename T>
	quaternion<T> pow(const quaternion<T>& a, int b)
	{
		if (b <= 0)
			return quaternion<T>();
		if (b == 1)
			return a;
		quaternion<T> oddpart = quaternion<T>();
		quaternion<T> evenpart = a;
		while (b > 1)
		{
			if (b % 2 == 1)
				oddpart *= evenpart;

			evenpart *= evenpart;
			b /= 2;
		}
		return oddpart * evenpart;
	}

	template<typename T>
	quaternion<T> inverse(const quaternion<T>& a)
		{ return conjugate(a) / lengthSquared(a); }

	// Apply a normalized quat as a rotation to a vector or point

	template <typename T>
	vector<T, 3> applyQuat(const quaternion<T>& a, vector<T, 3> const & b)
	{
		quaternion<T> v = { 0, b.x, b.y, b.z };
		quaternion<T> resultQ = a * v * conjugate(a);
		vector<T, 3> result = { resultQ.x, resultQ.y, resultQ.z };
		return result;
	}

	template<typename T>
	bool4 isnear(const quaternion<T>& a, const quaternion<T>& b, T eps = dm::epsilon)
	{
		bool4 result;
		for (int i = 0; i < 4; ++i)
			result[i] = isnear(a[i], b[i], eps);
		return result;
	}

	template<typename T>
	bool4 isnear(const quaternion<T>& a, T b, T eps = dm::epsilon)
	{
		bool4 result;
		for (int i = 0; i < 4; ++i)
			result[i] = isnear(a[i], b, eps);
		return result;
	}

	template<typename T>
	bool4 isnear(T a, const quaternion<T>& b, T eps = dm::epsilon)
	{
		bool4 result;
		for (int i = 0; i < 4; ++i)
			result[i] = isnear(a, b[i], eps);
		return result;
	}

	template<typename T>
	bool4 isfinite(const quaternion<T>& a)
	{
		bool4 result;
		for (int i = 0; i < 4; ++i)
			result[i] = isfinite(a[i]);
		return result;
	}

	template<typename T>
	quaternion<T> select(const bool4& cond, const quaternion<T>& a, const quaternion<T>& b)
	{
		quaternion<T> result;
		for (int i = 0; i < 4; ++i)
			result[i] = cond[i] ? a[i] : b[i];
		return result;
	}

	template<typename T>
	quaternion<T> min(const quaternion<T>& a, const quaternion<T>& b)
		{ return select(a < b, a, b); }

	template<typename T>
	quaternion<T> max(const quaternion<T>& a, const quaternion<T>& b)
		{ return select(a < b, b, a); }

	template<typename T>
	quaternion<T> abs(const quaternion<T>& a)
		{ return select(a < T(0), -a, a); }

	template<typename T>
	quaternion<T> saturate(const quaternion<T>& value)
		{ return clamp(value, quaternion<T>(T(0)), quaternion<T>(T(1))); }

	template<typename T>
	T minComponent(const quaternion<T>& a)
	{
		T result = a[0];
		for (int i = 1; i < 4; ++i)
			result = min(result, a[i]);
		return result;
	}

	template<typename T>
	T maxComponent(const quaternion<T>& a)
	{
		T result = a[0];
		for (int i = 1; i < 4; ++i)
			result = max(result, a[i]);
		return result;
	}

	template<typename T>
	quaternion<T> rotationQuat(const vector<T, 3>& axis, T radians)
	{
		// Note: assumes axis is normalized
		T sinHalfTheta = std::sin(T(0.5) * radians);
		T cosHalfTheta = std::cos(T(0.5) * radians);

		return quaternion<T>(cosHalfTheta, axis * sinHalfTheta);
	}

	template<typename T>
	quaternion<T> rotationQuat(const vector<T, 3>& euler)
	{
		T sinHalfX = std::sin(T(0.5) * euler.x);
		T cosHalfX = std::cos(T(0.5) * euler.x);
		T sinHalfY = std::sin(T(0.5) * euler.y);
		T cosHalfY = std::cos(T(0.5) * euler.y);
		T sinHalfZ = std::sin(T(0.5) * euler.z);
		T cosHalfZ = std::cos(T(0.5) * euler.z);

		quaternion<T> quatX = quaternion<T>(cosHalfX, sinHalfX, 0, 0);
		quaternion<T> quatY = quaternion<T>(cosHalfY, 0, sinHalfY, 0);
		quaternion<T> quatZ = quaternion<T>(cosHalfZ, 0, 0, sinHalfZ);

		// Note: multiplication order for quats is like column-vector convention
		return quatZ * quatY * quatX;
	}

	template<typename T>
	quaternion<T> slerp(const quaternion<T>& a, const quaternion<T>& b, T u)
	{
#if 0
		T theta = std::acos(dot(a, b));
		if (theta <= T(0))
			return a;
		return (a * std::sin((T(1) - u) * theta) + b * std::sin(u * theta)) / std::sin(theta);
#else // adapted from https://github.com/GameTechDev/XeGTAO/blob/master/Source/Core/vaGeometry.cpp#L1253, see https://github.com/GameTechDev/XeGTAO/blob/master/LICENSE
		T sign = T(1);
		T fa = T(1) - u;
		T fb = u;
		T dp = dot(a, b);
		if (dp < T(0))
		{
			sign = T(-1);
			dp = -dp;
		}
		if (T(1) - dp > T(0.001))
		{
			T theta = std::acos(dp);
			fa = std::sin(theta * fa) / std::sin(theta);
			fb = std::sin(theta * fb) / std::sin(theta);
		}
		return fa * a + sign * fb * b;
#endif
	}

	template<typename T>
	void decomposeAffine(const affine<T, 3>& transform, vector<T, 3>* pTranslation, quaternion<T>* pRotation, vector<T, 3>* pScaling)
	{
		if (pTranslation)
			*pTranslation = transform.m_translation;

		vector<T, 3> col0 = transform.m_linear.col(0);
		vector<T, 3> col1 = transform.m_linear.col(1);
		vector<T, 3> col2 = transform.m_linear.col(2);

		vector<T, 3> scaling;
		scaling.x = length(col0);
		scaling.y = length(col1);
		scaling.z = length(col2);
		if (scaling.x > 0.f) col0 /= scaling.x;
		if (scaling.y > 0.f) col1 /= scaling.y;
		if (scaling.z > 0.f) col2 /= scaling.z;

		vector<T, 3> zAxis = cross(col0, col1);
		if (dot(zAxis, col2) < T(0))
		{
			scaling.x = -scaling.x;
			col0 = -col0;
		}

		if (pScaling)
			*pScaling = scaling;

		if (pRotation)
		{
			// https://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
			quaternion<T> rotation;
			rotation.w = std::sqrt(std::max(T(0), T(1) + col0.x + col1.y + col2.z)) * T(0.5);
			rotation.x = std::sqrt(std::max(T(0), T(1) + col0.x - col1.y - col2.z)) * T(0.5);
			rotation.y = std::sqrt(std::max(T(0), T(1) - col0.x + col1.y - col2.z)) * T(0.5);
			rotation.z = std::sqrt(std::max(T(0), T(1) - col0.x - col1.y + col2.z)) * T(0.5);
			rotation.x = std::copysign(rotation.x, col2.y - col1.z);
			rotation.y = std::copysign(rotation.y, col0.z - col2.x);
			rotation.z = std::copysign(rotation.z, col1.x - col0.y);
			*pRotation = rotation;
		}
	}
}
