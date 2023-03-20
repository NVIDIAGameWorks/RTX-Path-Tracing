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

#include <donut/core/math/math.h>

namespace donut::math
{
    plane plane::normalize() const
    {
        float lengthSq = dot(normal, normal);
        float scale = (lengthSq > 0.f ? (1.0f / sqrtf(lengthSq)) : 0);

        plane result;
        result.normal = normal * scale;
        result.distance = distance * scale;
        return result;
    }

    constexpr bool plane::isempty()
    {
        return all(normal == 0.f);
    }

    frustum::frustum(const frustum &f)
    {
        planes[0] = f.planes[0];
        planes[1] = f.planes[1];
        planes[2] = f.planes[2];
        planes[3] = f.planes[3];
        planes[4] = f.planes[4];
        planes[5] = f.planes[5];
    }

    frustum::frustum(const float4x4 &m, bool isReverseProjection)
    {
        planes[NEAR_PLANE] = plane(-m[0].z, -m[1].z, -m[2].z, m[3].z);
        planes[FAR_PLANE] = plane(-m[0].w + m[0].z, -m[1].w + m[1].z, -m[2].w + m[2].z, m[3].w - m[3].z);

        if (isReverseProjection)
            std::swap(planes[NEAR_PLANE], planes[FAR_PLANE]);

        planes[LEFT_PLANE] = plane(-m[0].w - m[0].x, -m[1].w - m[1].x, -m[2].w - m[2].x, m[3].w + m[3].x);
        planes[RIGHT_PLANE] = plane(-m[0].w + m[0].x, -m[1].w + m[1].x, -m[2].w + m[2].x, m[3].w - m[3].x);

        planes[TOP_PLANE] = plane(-m[0].w + m[0].y, -m[1].w + m[1].y, -m[2].w + m[2].y, m[3].w - m[3].y);
        planes[BOTTOM_PLANE] = plane(-m[0].w - m[0].y, -m[1].w - m[1].y, -m[2].w - m[2].y, m[3].w + m[3].y);

        *this = normalize();
    }

    bool frustum::intersectsWith(const float3 &point) const
    {
        for (int i = 0; i < PLANES_COUNT; ++i)
        {
            float distance = dot(planes[i].normal, point);
            if (distance > planes[i].distance) return false;
        }

        return true;
    }

    bool frustum::intersectsWith(const box3 &box) const
    {
        for (int i = 0; i < PLANES_COUNT; ++i)
        {
            float x = planes[i].normal.x > 0 ? box.m_mins.x : box.m_maxs.x;
            float y = planes[i].normal.y > 0 ? box.m_mins.y : box.m_maxs.y;
            float z = planes[i].normal.z > 0 ? box.m_mins.z : box.m_maxs.z;
            
            float distance = 
                planes[i].normal.x * x +
                planes[i].normal.y * y +
                planes[i].normal.z * z -
                planes[i].distance;

            if (distance > 0.f) return false;
        }

        return true;
    }

    dm::float3 frustum::getCorner(int index) const
    {
        const plane& a = (index & 1) ? planes[RIGHT_PLANE] : planes[LEFT_PLANE];
        const plane& b = (index & 2) ? planes[TOP_PLANE] : planes[BOTTOM_PLANE];
        const plane& c = (index & 4) ? planes[FAR_PLANE] : planes[NEAR_PLANE];

        float3x3 m = float3x3(a.normal, b.normal, c.normal);
        float3 d = float3(a.distance, b.distance, c.distance);
        return inverse(m) * d;
    }

    frustum frustum::normalize() const
    {
        frustum result;

        for (int i = 0; i < PLANES_COUNT; i++)
            result.planes[i] = planes[i].normalize();

        return result;
    }

    frustum frustum::grow(float distance) const
    {
        frustum result;

        for (int i = 0; i < PLANES_COUNT; i++)
        {
            result.planes[i] = planes[i].normalize();
            result.planes[i].distance += distance;
        }

        return result;
    }

    bool frustum::isempty() const
    {
        // empty if at least one plane equation rejects all points

        for (int i = 0; i < PLANES_COUNT; i++)
        {
            if (all(planes[i].normal == 0.f) && planes[i].distance < 0.f)
                return true;
        }

        return false;
    }

    bool frustum::isopen() const
    {
        // open if at least one plane equation accepts all points, unless empty

        if (isempty()) 
            return false;

        for (int i = 0; i < PLANES_COUNT; i++)
        {
            if (all(planes[i].normal == 0.f) && planes[i].distance >= 0.f)
                return true;
        }

        return false;
    }

    bool frustum::isinfinite() const
    {
        // infinite if all plane equations accept all points

        for (int i = 0; i < PLANES_COUNT; i++)
        {
            if (!(all(planes[i].normal == 0.f) && planes[i].distance >= 0.f))
                return false;
        }

        return true;
    }

    frustum frustum::empty()
    {
        frustum f;

        for (plane& p : f.planes)
        {
            // (dot(normal, v) - distance) positive for any v => any point is outside
            p.normal = 0.f;
            p.distance = -1.f;
        }

        return f;
    }

    frustum frustum::infinite()
    {
        frustum f;

        for (plane& p : f.planes)
        {
            // (dot(normal, v) - distance) negative for any v => any point is inside
            p.normal = 0.f;
            p.distance = 1.f;
        }

        return f;
    }

    frustum frustum::fromBox(const box3& b)
    {
        frustum f;

        f.leftPlane() = plane(float3(-1.f, 0.f, 0.f), -b.m_mins.x);
        f.rightPlane() = plane(float3(1.f, 0.f, 0.f), b.m_maxs.x);
        f.bottomPlane() = plane(float3(0.f, -1.f, 0.f), -b.m_mins.y);
        f.topPlane() = plane(float3(0.f, 1.f, 0.f), b.m_maxs.y);
        f.nearPlane() = plane(float3(0.f, 0.f, -1.f), -b.m_mins.z);
        f.farPlane() = plane(float3(0.f, 0.f, 1.f), b.m_maxs.z);

        return f;
    }

}