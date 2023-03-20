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

#pragma once

#include <nvrhi/nvrhi.h>
#include <unordered_map>
#include <shared_mutex>

namespace donut::engine
{
    /*
    BindingCache maintains a dictionary that maps binding set descriptors
    into actual binding set objects. The binding sets are created on demand when 
    GetOrCreateBindingSet(...) is called and the requested binding set does not exist.
    Created binding sets are stored for the lifetime of BindingCache, or until
    Clear() is called.
    
    All BindingCache methods are thread-safe.
    */
    class BindingCache
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::unordered_map<size_t, nvrhi::BindingSetHandle> m_BindingSets;
        std::shared_mutex m_Mutex;

    public:
        BindingCache(nvrhi::IDevice* device)
            : m_Device(device)
        { }

        nvrhi::BindingSetHandle GetCachedBindingSet(const nvrhi::BindingSetDesc& desc, nvrhi::IBindingLayout* layout);
        nvrhi::BindingSetHandle GetOrCreateBindingSet(const nvrhi::BindingSetDesc& desc, nvrhi::IBindingLayout* layout);
        void Clear();
    };

}
