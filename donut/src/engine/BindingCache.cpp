/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

#include <donut/engine/BindingCache.h>

using namespace donut::engine;

nvrhi::BindingSetHandle BindingCache::GetCachedBindingSet(const nvrhi::BindingSetDesc& desc, nvrhi::IBindingLayout* layout)
{
    size_t hash = 0;
    nvrhi::hash_combine(hash, desc);
    nvrhi::hash_combine(hash, layout);

    m_Mutex.lock_shared();

    nvrhi::BindingSetHandle result = nullptr;
    auto it = m_BindingSets.find(hash);
    if (it != m_BindingSets.end())
        result = it->second;

    m_Mutex.unlock_shared();

    if (result)
    {
        assert(result->getDesc());
        assert(*result->getDesc() == desc);
    }

    return result;
}

nvrhi::BindingSetHandle BindingCache::GetOrCreateBindingSet(const nvrhi::BindingSetDesc& desc, nvrhi::IBindingLayout* layout)
{
    size_t hash = 0;
    nvrhi::hash_combine(hash, desc);
    nvrhi::hash_combine(hash, layout);

    m_Mutex.lock_shared();

    nvrhi::BindingSetHandle result;
    auto it = m_BindingSets.find(hash);
    if (it != m_BindingSets.end())
        result = it->second;
    
    m_Mutex.unlock_shared();

    if (!result)
    {
        m_Mutex.lock();

        nvrhi::BindingSetHandle& entry = m_BindingSets[hash];
        if (!entry)
        {
            result = m_Device->createBindingSet(desc, layout);
            entry = result;
        }
        else
            result = entry;

        m_Mutex.unlock();
    }

    if (result)
    {
        assert(result->getDesc());
        assert(*result->getDesc() == desc);
    }

    return result;
}

void BindingCache::Clear()
{
    m_Mutex.lock();
    m_BindingSets.clear();
    m_Mutex.unlock();
}
