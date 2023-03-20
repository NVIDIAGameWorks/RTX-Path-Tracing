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

#include <donut/engine/DescriptorTableManager.h>

donut::engine::DescriptorHandle::DescriptorHandle()
    : m_DescriptorIndex(-1)
{
}

donut::engine::DescriptorHandle::DescriptorHandle(const std::shared_ptr<DescriptorTableManager>& managerPtr, DescriptorIndex index)
    : m_Manager(managerPtr)
    , m_DescriptorIndex(index)
{
}

donut::engine::DescriptorHandle::~DescriptorHandle()
{
    if (m_DescriptorIndex >= 0)
    {
        auto managerPtr = m_Manager.lock();
        if (managerPtr)
            managerPtr->ReleaseDescriptor(m_DescriptorIndex);
        m_DescriptorIndex = -1;
    }
}

donut::engine::DescriptorTableManager::DescriptorTableManager(nvrhi::IDevice* device, nvrhi::IBindingLayout* layout)
    : m_Device(device)
{
    m_DescriptorTable = m_Device->createDescriptorTable(layout);

    size_t capacity = m_DescriptorTable->getCapacity();
    m_AllocatedDescriptors.resize(capacity);
    m_Descriptors.resize(capacity);
    memset(m_Descriptors.data(), 0, sizeof(nvrhi::BindingSetItem) * capacity);
}

donut::engine::DescriptorIndex donut::engine::DescriptorTableManager::CreateDescriptor(nvrhi::BindingSetItem item)
{
    const auto& found = m_DescriptorIndexMap.find(item);
    if (found != m_DescriptorIndexMap.end())
        return found->second;

    uint32_t capacity = m_DescriptorTable->getCapacity();
    bool foundFreeSlot = false;
    uint32_t index = 0;
    for (index = m_SearchStart; index < capacity; index++)
    {
        if (!m_AllocatedDescriptors[index])
        {
            foundFreeSlot = true;
            break;
        }
    }

    if (!foundFreeSlot)
    {
        uint32_t newCapacity = std::max(64u, capacity * 2); // handle the initial case when capacity == 0
        m_Device->resizeDescriptorTable(m_DescriptorTable, newCapacity);
        m_AllocatedDescriptors.resize(newCapacity);
        m_Descriptors.resize(newCapacity);

        // zero-fill the new descriptors
        memset(&m_Descriptors[capacity], 0, sizeof(nvrhi::BindingSetItem) * (newCapacity - capacity));
        
        index = capacity;
        capacity = newCapacity;
    }

    item.slot = index;
    m_SearchStart = index + 1;
    m_AllocatedDescriptors[index] = true;
    m_Descriptors[index] = item;
    m_DescriptorIndexMap[item] = index;
    m_Device->writeDescriptorTable(m_DescriptorTable, item);

    if (item.resourceHandle)
        item.resourceHandle->AddRef();

    return index;
}

donut::engine::DescriptorHandle donut::engine::DescriptorTableManager::CreateDescriptorHandle(nvrhi::BindingSetItem item)
{
    DescriptorIndex index = CreateDescriptor(item);
    return DescriptorHandle(shared_from_this(), index);
}

nvrhi::BindingSetItem donut::engine::DescriptorTableManager::GetDescriptor(DescriptorIndex index)
{
    if (size_t(index) >= m_Descriptors.size())
        return nvrhi::BindingSetItem::None(0);

    return m_Descriptors[index];
}

void donut::engine::DescriptorTableManager::ReleaseDescriptor(DescriptorIndex index)
{
    nvrhi::BindingSetItem& descriptor = m_Descriptors[index];

    if (descriptor.resourceHandle)
        descriptor.resourceHandle->Release();

    // Erase the existing descriptor from the index map to prevent its "reuse" later
    const auto indexMapEntry = m_DescriptorIndexMap.find(m_Descriptors[index]);
    if (indexMapEntry != m_DescriptorIndexMap.end())
        m_DescriptorIndexMap.erase(indexMapEntry);

    descriptor = nvrhi::BindingSetItem::None(index);

    m_Device->writeDescriptorTable(m_DescriptorTable, descriptor);

    m_AllocatedDescriptors[index] = false;
    m_SearchStart = std::min(m_SearchStart, index);
}

donut::engine::DescriptorTableManager::~DescriptorTableManager()
{
    for (auto& descriptor : m_Descriptors)
    {
        if (descriptor.resourceHandle)
        {
            descriptor.resourceHandle->Release();
            descriptor.resourceHandle = nullptr;
        }
    }
}
