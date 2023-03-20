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
	// Generic affine transform struct, with a matrix and translation vector

	template <typename T, int n>
	struct affine
	{
		cassert(n > 1);

		matrix<T, n, n>	m_linear;
		vector<T, n>	m_translation;

		template<typename U>
		explicit affine(const affine<U, n>& a)
			: m_linear(a.m_linear)
			, m_translation(a.m_translation) { }

		static affine<T, n> identity()
		{
			affine<T, n> result = { matrix<T, n, n>::identity(), vector<T, n>(static_cast<T>(0)) };
			return result;
		}

		affine() { }

		vector<T, n> transformPoint(const vector<T, n>& v) const
		{
		    return v * m_linear + m_translation;
		}

		vector<T, n> transformVector(const vector<T, n>& v) const
		{
		    return v * m_linear;
		}
	};

	template<typename T>
	struct affine<T, 2>
	{
		matrix<T, 2, 2>	m_linear;
		vector<T, 2>	m_translation;

		template<typename U>
		explicit constexpr affine(const affine<U, 2>& a)
			: m_linear(a.m_linear)
			, m_translation(a.m_translation) { }

		constexpr affine(T m00, T m01, T m10, T m11, T t0, T t1)
			: m_linear(m00, m01, m10, m11)
			, m_translation(t0, t1) { }

		constexpr affine(const vector<T, 2>& row0, const vector<T, 2>& row1, const vector<T, 2>& translation)
			: m_linear(row0, row1)
			, m_translation(translation) { }

		constexpr affine(const matrix<T, 2, 2>& linear, const vector<T, 2>& translation)
			: m_linear(linear)
			, m_translation(translation) { }

		static constexpr affine from_cols(const vector<T, 2>& col0, const vector<T, 2>& col1, const vector<T, 2>& translation)
		{
			return affine(matrix<T, 2, 2>::from_cols(col0, col1), translation);
		}

		static constexpr affine identity()
		{
			return affine(matrix<T, 2, 2>::identity(), vector<T, 2>::zero());
		}

	    affine() { }

		[[nodiscard]] vector<T, 2> transformVector(const vector<T, 2>& v) const
		{
			vector<T, 2> result;
			result.x = v.x * m_linear.row0.x + v.y * m_linear.row1.x;
			result.y = v.x * m_linear.row0.y + v.y * m_linear.row1.y;
			return result;
		}

		[[nodiscard]] vector<T, 2> transformPoint(const vector<T, 2>& v) const
		{
			vector<T, 2> result;
			result.x = v.x * m_linear.row0.x + v.y * m_linear.row1.x + m_translation.x;
			result.y = v.x * m_linear.row0.y + v.y * m_linear.row1.y + m_translation.y;
			return result;
		}
	};

	template<typename T>
	struct affine<T, 3>
	{
		matrix<T, 3, 3>	m_linear;
		vector<T, 3>	m_translation;

		template<typename U>
		explicit constexpr affine(const affine<U, 3>& a)
			: m_linear(a.m_linear)
			, m_translation(a.m_translation) { }

		constexpr affine(T m00, T m01, T m02, T m10, T m11, T m12, T m20, T m21, T m22, T t0, T t1, T t2)
			: m_linear(m00, m01, m02, m10, m11, m12, m20, m21, m22)
			, m_translation(t0, t1, t2) { }

		constexpr affine(const vector<T, 3>& row0, const vector<T, 3>& row1, const vector<T, 3>& row2, const vector<T, 3>& translation)
			: m_linear(row0, row1, row2)
			, m_translation(translation) { }

		constexpr affine(const matrix<T, 3, 3>& linear, const vector<T, 3>& translation)
			: m_linear(linear)
			, m_translation(translation) { }

		static constexpr affine from_cols(const vector<T, 3>& col0, const vector<T, 3>& col1, const vector<T, 3>& col2, const vector<T, 3>& translation)
		{
			return affine(matrix<T, 3, 3>::from_cols(col0, col1, col2), translation);
		}

		static constexpr affine identity()
		{
			return affine(matrix<T, 3, 3>::identity(), vector<T, 3>::zero());
		}

	    affine() { }

		[[nodiscard]] vector<T, 3> transformVector(const vector<T, 3>& v) const
		{
			vector<T, 3> result;
			result.x = v.x * m_linear.row0.x + v.y * m_linear.row1.x + v.z * m_linear.row2.x;
			result.y = v.x * m_linear.row0.y + v.y * m_linear.row1.y + v.z * m_linear.row2.y;
			result.z = v.x * m_linear.row0.z + v.y * m_linear.row1.z + v.z * m_linear.row2.z;
			return result;
		}

		[[nodiscard]] vector<T, 3> transformPoint(const vector<T, 3>& v) const
		{
			vector<T, 3> result;
			result.x = v.x * m_linear.row0.x + v.y * m_linear.row1.x + v.z * m_linear.row2.x + m_translation.x;
			result.y = v.x * m_linear.row0.y + v.y * m_linear.row1.y + v.z * m_linear.row2.y + m_translation.y;
			result.z = v.x * m_linear.row0.z + v.y * m_linear.row1.z + v.z * m_linear.row2.z + m_translation.z;
			return result;
		}
	};


	// Concrete affines for the most common types and dimensions
#define DEFINE_CONCRETE_AFFINES(type, name) \
			typedef affine<type, 2> name##2; \
			typedef affine<type, 3> name##3;

	DEFINE_CONCRETE_AFFINES(float, affine);
	DEFINE_CONCRETE_AFFINES(double, daffine);
	DEFINE_CONCRETE_AFFINES(int, iaffine);

#undef DEFINE_CONCRETE_AFFINES

	// Overloaded math operators

	// Relational operators
	// !!! these don't match the behavior of relational ops for vectors and matrices -
	// return single results rather than componentwise results

	template <typename T, int n>
	bool operator == (affine<T, n> const & a, affine<T, n> const & b)
	{
		return all(a.m_linear == b.m_linear) && all(a.m_translation == b.m_translation);
	}

	template <typename T, int n>
	bool operator != (affine<T, n> const & a, affine<T, n> const & b)
	{
		return any(a.m_linear != b.m_linear) || any(a.m_translation != b.m_translation);
	}

	// Affine composition (row-vector math)

	template <typename T, int n>
	affine<T, n> operator * (affine<T, n> const & a, affine<T, n> const & b)
	{
		affine<T, n> result =
		{
			a.m_linear * b.m_linear,
			a.m_translation * b.m_linear + b.m_translation
		};
		return result;
	}

	template <typename T, int n>
	affine<T, n> & operator *= (affine<T, n> & a, affine<T, n> const & b)
	{
		a = a*b;
		return a;
	}
    

	// Other math functions

	template <typename T, int n>
	affine<T, n> transpose(affine<T, n> const & a)
	{
		auto mTransposed = transpose(a.m_linear);
		affine<T, n> result =
		{
			mTransposed,
			-a.m_translation * mTransposed
		};
		return result;
	}

	template <typename T, int n>
	affine<T, n> pow(affine<T, n> const & a, int b)
	{
		if (b <= 0)
			return affine<T, n>::identity();
		if (b == 1)
			return a;
		auto oddpart = affine<T, n>::identity(), evenpart = a;
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
	affine<T, n> inverse(affine<T, n> const & a)
	{
		auto mInverted = inverse(a.m_linear);
		affine<T, n> result =
		{
			mInverted,
			-a.m_translation * mInverted
		};
		return result;
	}

	template <typename T, int n>
	matrix<T, n+1, n+1> affineToHomogeneous(affine<T, n> const & a)
	{
		matrix<T, n+1, n+1> result;
		for (int i = 0; i < n; ++i)
		{
			for (int j = 0; j < n; ++j)
				result[i][j] = a.m_linear[i][j];
			result[i][n] = T(0);
		}
		for (int j = 0; j < n; ++j)
			result[n][j] = a.m_translation[j];
		result[n][n] = T(1);
		return result;
	}

	template <typename T, int n>
	affine<T, n-1> homogeneousToAffine(matrix<T, n, n> const & a)
	{
		// Extract the relevant components of the matrix; note, NO checking
		// that the matrix actually represents an affine transform!
		affine<T, n-1> result;
		for (int i = 0; i < n-1; ++i)
			for (int j = 0; j < n-1; ++j)
				result.m_linear[i][j] = T(a[i][j]);
		for (int j = 0; j < n-1; ++j)
			result.m_translation[j] = T(a[n-1][j]);
		return result;
	}

	// Fast shortcut for float3x4(transpose(affineToHomogenous(a)))
	// Useful for storing transformations in buffers and passing them to ray tracing APIs
	inline void affineToColumnMajor(affine3 const & a, float m[12])
	{
		m[0] = a.m_linear.m00;
		m[1] = a.m_linear.m10;
		m[2] = a.m_linear.m20;
		m[3] = a.m_translation.x;
		m[4] = a.m_linear.m01;
		m[5] = a.m_linear.m11;
		m[6] = a.m_linear.m21;
		m[7] = a.m_translation.y;
		m[8] = a.m_linear.m02;
		m[9] = a.m_linear.m12;
		m[10] = a.m_linear.m22;
		m[11] = a.m_translation.z;
	}

	// !!! this doesn't match the behavior of isnear() for vectors and matrices -
	// returns a single result rather than a componentwise result
	template <typename T, int n>
	bool isnear(affine<T, n> const & a, affine<T, n> const & b, T epsilon = dm::epsilon)
	{
		return all(isnear(a.m_linear, b.m_linear, epsilon)) &&
			   all(isnear(a.m_translation, b.m_translation, epsilon));
	}

	// !!! this doesn't match the behavior of isfinite() for vectors and matrices -
	// returns a single result rather than a componentwise result
	template <typename T, int n>
	bool isfinite(affine<T, n> const & a)
	{
		return all(isfinite(a.m_linear)) && all(isfinite(a.m_translation));
	}

	template <typename T, int n>
	affine<int, n> round(affine<T, n> const & a)
	{
		return affine<int, n>(round(a.m_linear), round(a.m_translation));
	}



	// Generate various types of transformations (row-vector math)

	template <typename T, int n>
	affine<T, n> translation(vector<T, n> const & a)
	{
		affine<T, n> result = { matrix<T, n, n>::identity(), a };
		return result;
	}

	template <typename T, int n>
	affine<T, n> scaling(T a)
	{
		affine<T, n> result = { diagonal<T, n>(a), vector<T, n>(static_cast<T>(0)) };
		return result;
	}

	template <typename T, int n>
	affine<T, n> scaling(vector<T, n> const & a)
	{
		affine<T, n> result = { diagonal(a), vector<T, n>(static_cast<T>(0)) };
		return result;
	}

	template<typename T>
	affine<T, 2> rotation(T radians)
	{
		T sinTheta = std::sin(radians);
		T cosTheta = std::cos(radians);
		return affine<T, 2>(
			cosTheta, sinTheta,
			-sinTheta, cosTheta,
			T(0), T(0));
	}

	template<typename T>
	affine<T, 3> rotation(const vector<T, 3>& axis, T radians)
	{
		// Note: assumes axis is normalized
		T sinTheta = std::sin(radians);
		T cosTheta = std::cos(radians);

		// Build matrix that does cross product by axis (on the right)
		matrix<T, 3, 3> crossProductMat = matrix<T, 3, 3>(
			T(0), axis.z, -axis.y,
			-axis.z, T(0), axis.x,
			axis.y, -axis.x, T(0));

		// Matrix form of Rodrigues' rotation formula
		matrix<T, 3, 3> mat = diagonal<T, 3>(cosTheta) +
			crossProductMat * sinTheta +
			outerProduct(axis, axis) * (T(1) - cosTheta);

		return affine<T, 3>(mat, vector<T, 3>::zero());
	}

	template<typename T>
	affine<T, 3> rotation(const vector<T, 3>& euler)
	{
		T sinX = std::sin(euler.x);
		T cosX = std::cos(euler.x);
		T sinY = std::sin(euler.y);
		T cosY = std::cos(euler.y);
		T sinZ = std::sin(euler.z);
		T cosZ = std::cos(euler.z);

		matrix<T, 3, 3> matX = matrix<T, 3, 3>(
			1, 0, 0,
			0, cosX, sinX,
			0, -sinX, cosX);
		matrix<T, 3, 3> matY = matrix<T, 3, 3>(
			cosY, 0, -sinY,
			0, 1, 0,
			sinY, 0, cosY);
		matrix<T, 3, 3> matZ = matrix<T, 3, 3>(
			cosZ, sinZ, 0,
			-sinZ, cosZ, 0,
			0, 0, 1);

		return affine<T, 3>(matX * matY * matZ, vector<T, 3>::zero());
	}

	template<typename T>
	affine<T, 3> yawPitchRoll(T yaw, T pitch, T roll)
	{
		// adapted from glm

		T tmp_sh = std::sin(yaw);
		T tmp_ch = std::cos(yaw);
		T tmp_sp = std::sin(pitch);
		T tmp_cp = std::cos(pitch);
		T tmp_sb = std::sin(roll);
		T tmp_cb = std::cos(roll);
		
		affine<T, 3> result;
		result.m_linear[0][0] = tmp_ch * tmp_cb + tmp_sh * tmp_sp * tmp_sb;
		result.m_linear[0][1] = tmp_sb * tmp_cp;
		result.m_linear[0][2] = -tmp_sh * tmp_cb + tmp_ch * tmp_sp * tmp_sb;
		result.m_linear[0][3] = T(0);
		result.m_linear[1][0] = -tmp_ch * tmp_sb + tmp_sh * tmp_sp * tmp_cb;
		result.m_linear[1][1] = tmp_cb * tmp_cp;
		result.m_linear[1][2] = tmp_sb * tmp_sh + tmp_ch * tmp_sp * tmp_cb;
		result.m_linear[1][3] = T(0);
		result.m_linear[2][0] = tmp_sh * tmp_cp;
		result.m_linear[2][1] = -tmp_sp;
		result.m_linear[2][2] = tmp_ch * tmp_cp;
		result.m_linear[2][3] = T(0);
		result.m_translation = vector<T, 3>::zero();
		return result;
	}

	template<typename T>
	affine<T, 2> lookat(const vector<T, 2>& look)
	{
		vector<T, 2> lookNormalized = normalize(look);
		return affine<T, 2>::from_cols(lookNormalized, orthogonal(lookNormalized), vector<T, 2>(T(0)));
	}

	// lookatX: rotate so X axis faces 'look' and Z axis faces 'up', if specified.
	// lookatZ: rotate so -Z axis faces 'look' and Y axis faces 'up', if specified.

	template<typename T>
	affine<T, 3> lookatX(const vector<T, 3>& look)
	{
		vector<T, 3> lookNormalized = normalize(look);
		vector<T, 3> left = normalize(orthogonal(lookNormalized));
		vector<T, 3> up = cross(lookNormalized, left);
		return affine<T, 3>::from_cols(lookNormalized, left, up, vector<T, 3>::zero());
	}

	template<typename T>
	affine<T, 3> lookatX(const vector<T, 3>& look, const vector<T, 3>& up)
	{
		vector<T, 3> lookNormalized = normalize(look);
		vector<T, 3> left = normalize(cross(up, lookNormalized));
		vector<T, 3> trueUp = cross(lookNormalized, left);
		return affine<T, 3>::from_cols(lookNormalized, left, trueUp, vector<T, 3>::zero());
	}

	template<typename T>
	affine<T, 3> lookatZ(const vector<T, 3>& look)
	{
		vector<T, 3> lookNormalized = normalize(look);
		vector<T, 3> left = normalize(orthogonal(lookNormalized));
		vector<T, 3> up = cross(lookNormalized, left);
		return affine<T, 3>::from_cols(-left, up, -lookNormalized, vector<T, 3>::zero());
	}

	template<typename T>
	affine<T, 3> lookatZ(const vector<T, 3>& look, const vector<T, 3>& up)
	{
		vector<T, 3> lookNormalized = normalize(look);
		vector<T, 3> left = normalize(cross(up, lookNormalized));
		vector<T, 3> trueUp = cross(lookNormalized, left);
		return affine<T, 3>::from_cols(-left, trueUp, -lookNormalized, vector<T, 3>::zero());
	}

	template<typename T>
	void constructOrthonormalBasis(const vector<T, 3>& normal, vector<T, 3>& tangent, vector<T, 3>& bitangent)
	{
		// https://graphics.pixar.com/library/OrthonormalB/paper.pdf
		T sign = std::copysign(T(1), normal.z);
		T a = T(-1) / (sign + normal.z);
		T b = normal.x * normal.y * a;
		tangent = vector<T, 3>(T(1) + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
		bitangent = vector<T, 3>(b, sign + normal.y * normal.y * a, -normal.y);
	}
}
