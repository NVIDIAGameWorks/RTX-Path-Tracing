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

    template<typename T, int n>
    struct hypersphere
    {
        vector<T, n> center;
        T radius;

        constexpr hypersphere() { }

        constexpr hypersphere(const vector<T, n>& _center, T _radius)
            : center(_center)
            , radius(_radius) { }

        static constexpr hypersphere fromBox(const box<T, n>& _box)
        {
            return hypersphere(_box.center(), length(_box.diagonal()) / T(2));
        }

        static constexpr hypersphere empty()
        {
            return hypersphere(vector<T, n>::zero(), T(0));
        }

        constexpr bool intersects(const hypersphere& other)
        {
            auto vec = center - other.center;
            auto r1sq = radius * radius;
            auto r2sq = other.radius + other.radius;
            return dot(vec, vec) < r1sq + r2sq;
        }

        constexpr bool intersects(const box<T, n>& b) const
        {
            return b.grow(radius).contains(center);
        }

        constexpr bool isEmpty()
        {
            return radius == T(0);
        }

        constexpr hypersphere translate(const vector<T, n>& v) const
        {
            return hypersphere(center + v, radius);
        }

        constexpr hypersphere grow(T d) const
        {
            return hypersphere(center, max(T(0), radius + d));
        }

        constexpr bool operator == (hypersphere<T, n> const & other) const
        {
            return all(center == other.center) && (radius == other.radius);
        }

        constexpr bool operator != (hypersphere<T, n> const & other) const
        {
            return any(center != other.center) || (radius != other.radius);
        }
    };

    typedef hypersphere<float, 2> circle;
    typedef hypersphere<float, 3> sphere;

}
