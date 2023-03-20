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

#include <donut/engine/SceneGraph.h>
#include <donut/core/log.h>
#include <donut/core/json.h>
#include <sstream>

using namespace donut::engine;

const std::string& SceneGraphLeaf::GetName() const
{
    auto node = GetNode();
    if (node)
        return node->GetName();

    static const std::string emptyString = "";
    return emptyString;
}

void SceneGraphLeaf::SetName(const std::string& name) const
{
    auto node = GetNode();
    if (node)
        node->SetName(name);
    else
        assert(!"The leaf must be attached in order to set its name");
}

SkinnedMeshInstance::SkinnedMeshInstance(std::shared_ptr<SceneTypeFactory> sceneTypeFactory, std::shared_ptr<MeshInfo> prototypeMesh)
    : MeshInstance(nullptr)
    , m_SceneTypeFactory(sceneTypeFactory)
{
    m_PrototypeMesh = std::move(prototypeMesh);
    
    auto skinnedMesh = m_SceneTypeFactory->CreateMesh();
    skinnedMesh->skinPrototype = m_PrototypeMesh;
    skinnedMesh->name = m_PrototypeMesh->name;
    skinnedMesh->objectSpaceBounds = m_PrototypeMesh->objectSpaceBounds;
    skinnedMesh->totalVertices = m_PrototypeMesh->totalVertices;
    skinnedMesh->totalIndices = m_PrototypeMesh->totalIndices;
    skinnedMesh->geometries.reserve(m_PrototypeMesh->geometries.size());
    
    for (const auto& geometry : m_PrototypeMesh->geometries)
    {
        std::shared_ptr<MeshGeometry> newGeometry = std::make_shared<MeshGeometry>();
        *newGeometry = *geometry;
        skinnedMesh->geometries.push_back(newGeometry);
    }

    m_Mesh = skinnedMesh;
}

std::shared_ptr<SceneGraphLeaf> SkinnedMeshInstance::Clone()
{
    auto copy = std::make_shared<SkinnedMeshInstance>(m_SceneTypeFactory, m_PrototypeMesh);

    for (const auto& joint : joints)
    {
        copy->joints.push_back(joint);
    }

    return std::static_pointer_cast<SceneGraphLeaf>(copy);
}

std::shared_ptr<SceneGraphLeaf> SkinnedMeshReference::Clone()
{
    return std::make_shared<SkinnedMeshReference>(m_Instance.lock());
}

std::shared_ptr<SceneGraphLeaf> MeshInstance::Clone()
{
    return std::make_shared<MeshInstance>(m_Mesh);
}

SceneContentFlags MeshInstance::GetContentFlags() const
{
    if (!m_Mesh)
        return SceneContentFlags::None;

    SceneContentFlags flags = SceneContentFlags::None;

    for (const auto& geometry : m_Mesh->geometries)
    {
        if (!geometry->material)
            continue;

        switch(geometry->material->domain)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case MaterialDomain::Opaque:
            flags |= SceneContentFlags::OpaqueMeshes;
            break;
        case MaterialDomain::AlphaTested: 
            flags |= SceneContentFlags::AlphaTestedMeshes;
            break;
        default:
            flags |= SceneContentFlags::BlendedMeshes;
            break;
        }
    }

    return flags;
}

bool MeshInstance::SetProperty(const std::string& name, const dm::float4& value)
{
    if (m_Mesh && m_Mesh->geometries.size() == 1 && m_Mesh->geometries[0]->material) // TODO: what if we want to target a geometry other than 0?
        return m_Mesh->geometries[0]->material->SetProperty(name, value);

    return SceneGraphLeaf::SetProperty(name, value);
}

dm::affine3 SceneCamera::GetViewToWorldMatrix() const
{
    auto node = GetNode();
    if (!node)
        return dm::affine3::identity();

    return dm::scaling(dm::float3(1.f, 1.f, -1.f)) * dm::affine3(node->GetLocalToWorldTransform());
}

dm::affine3 SceneCamera::GetWorldToViewMatrix() const
{
    auto node = GetNode();
    if (!node)
        return dm::affine3::identity();

    return dm::affine3(inverse(node->GetLocalToWorldTransform())) * dm::scaling(dm::float3(1.f, 1.f, -1.f));
}

std::shared_ptr<SceneGraphLeaf> PerspectiveCamera::Clone()
{
    auto copy = std::make_shared<PerspectiveCamera>();
    copy->zNear = zNear;
    copy->zFar = zFar;
    copy->verticalFov = verticalFov;
    copy->aspectRatio = aspectRatio;
    return copy;
}

void PerspectiveCamera::Load(const Json::Value& node)
{
    node["verticalFov"] >> verticalFov;
    node["aspectRatio"] >> aspectRatio;
    node["zNear"] >> zNear;
    node["zFar"] >> zFar;
}

bool PerspectiveCamera::SetProperty(const std::string& name, const dm::float4& value)
{
    if (name == "zNear")
    {
        zNear = value.x;
        return true;
    }

    if (name == "zFar")
    {
        zFar = value.x;
        return true;
    }

    if (name == "verticalFov")
    {
        verticalFov = value.x;
        return true;
    }

    if (name == "aspectRatio")
    {
        aspectRatio = value.x;
        return true;
    }

    return SceneGraphLeaf::SetProperty(name, value);
}

std::shared_ptr<SceneGraphLeaf> OrthographicCamera::Clone()
{
    auto copy = std::make_shared<OrthographicCamera>();
    copy->zNear = zNear;
    copy->zFar = zFar;
    copy->xMag = xMag;
    copy->yMag = yMag;
    return copy;
}

void OrthographicCamera::Load(const Json::Value& node)
{
    node["xMag"] >> xMag;
    node["yMag"] >> yMag;
    node["zNear"] >> zNear;
    node["zFar"] >> zFar;
}

bool OrthographicCamera::SetProperty(const std::string& name, const dm::float4& value)
{
    if (name == "zNear")
    {
        zNear = value.x;
        return true;
    }

    if (name == "zFar")
    {
        zFar = value.x;
        return true;
    }

    if (name == "xMag")
    {
        xMag = value.x;
        return true;
    }

    if (name == "yMag")
    {
        yMag = value.x;
        return true;
    }

    return SceneGraphLeaf::SetProperty(name, value);
}

void SceneGraphNode::UpdateLocalTransform()
{
    dm::daffine3 transform = dm::scaling(m_Scaling);
    transform *= m_Rotation.toAffine();
    transform *= dm::translation(m_Translation);
    m_LocalTransform = transform;
}

void SceneGraphNode::PropagateDirtyFlags(DirtyFlags flags)
{
    SceneGraphWalker walker(this, nullptr);
    while (walker)
    {
        walker->m_Dirty |= flags;
        walker.Up();
    }
}

std::filesystem::path SceneGraphNode::GetPath() const
{
    std::filesystem::path path = GetName();

    SceneGraphWalker walker(GetParent(), nullptr);
    while (walker)
    {
        path = walker->GetName() / path;
        walker.Up();
    }

    return "/" / path;
}

void SceneGraphNode::InvalidateContent()
{
    PropagateDirtyFlags(DirtyFlags::SubgraphContentUpdate);
}

void SceneGraphNode::SetTransform(const dm::double3* translation, const dm::dquat* rotation, const dm::double3* scaling)
{
    if (scaling) m_Scaling = *scaling;
    if (rotation) m_Rotation = *rotation;
    if (translation) m_Translation = *translation;

    m_Dirty |= DirtyFlags::LocalTransform;
    m_HasLocalTransform = true;
    PropagateDirtyFlags(DirtyFlags::SubgraphTransforms);
}

void SceneGraphNode::SetScaling(const dm::double3& scaling)
{
    SetTransform(nullptr, nullptr, &scaling);
}

void SceneGraphNode::SetRotation(const dm::dquat& rotation)
{
    SetTransform(nullptr, &rotation, nullptr);
}

void SceneGraphNode::SetTranslation(const dm::double3& translation)
{
    SetTransform(&translation, nullptr, nullptr);
}

void SceneGraphNode::SetLeaf(const std::shared_ptr<SceneGraphLeaf>& leaf)
{
    auto graph = m_Graph.lock();

    if (m_Leaf)
    {
        m_Leaf->m_Node.reset();
        if (graph)
            graph->UnregisterLeaf(m_Leaf);
    }

    m_Leaf = leaf;
    leaf->m_Node = weak_from_this();
    if (graph)
        graph->RegisterLeaf(leaf);

    m_Dirty |= DirtyFlags::Leaf;
    PropagateDirtyFlags(DirtyFlags::SubgraphStructure);
}

void SceneGraphNode::SetName(const std::string& name)
{
    m_Name = name;
}

void SceneGraphNode::ReverseChildren()
{
    // in-place linked list reverse algorithm
    std::shared_ptr<SceneGraphNode> current, prev, next;
    current = m_FirstChild;

    while (current)
    {
        next = current->m_NextSibling;
        current->m_NextSibling = prev;
        prev = current;
        current = next;
    }

    m_FirstChild = prev;
}

int SceneGraphWalker::Next(bool allowChildren)
{
    if (!m_Current)
        return 0;

    if (allowChildren)
    {
        auto firstChild = m_Current->GetFirstChild();
        if (firstChild)
        {
            m_Current = firstChild;
            return 1;
        }
    }

    int depth = 0;

    while (m_Current)
    {
        if (m_Current == m_Scope)
        {
            m_Current = nullptr;
            return depth;
        }

        auto nextSibling = m_Current->GetNextSibling();
        if (nextSibling)
        {
            m_Current = nextSibling;
            return depth;
        }

        m_Current = m_Current->GetParent();
        --depth;
    }

    return depth;
}

int SceneGraphWalker::Up()
{
    if (!m_Current)
        return 0;

    if (m_Current == m_Scope)
    {
        m_Current = nullptr;
        return 0;
    }

    m_Current = m_Current->GetParent();
    return -1;
}

bool SceneGraphAnimationChannel::Apply(float time) const
{
    auto node = m_TargetNode.lock();
    auto material = m_TargetMaterial.lock();
    if ((!node && m_Attribute != AnimationAttribute::LeafProperty) || 
        (!material && !node && m_Attribute == AnimationAttribute::LeafProperty))
        return false;

    auto valueOption = m_Sampler->Evaluate(time, true);
    if (!valueOption.has_value())
        return false;

    auto value = valueOption.value();

    switch(m_Attribute)
    {
    case AnimationAttribute::Scaling:
        node->SetScaling(dm::double3(value.xyz()));
        break;

    case AnimationAttribute::Rotation: {
        dm::dquat quat = dm::dquat::fromXYZW(dm::double4(value));
        double len = length(quat);
        if (len == 0.0)
        {
            log::warning("Rotation quaternion interpolated to zero, ignoring.");
        }
        else
        {
            quat /= len;
            node->SetRotation(quat);
        }
        break;
    }

    case AnimationAttribute::Translation:
        node->SetTranslation(dm::double3(value.xyz()));
        break;

    case AnimationAttribute::LeafProperty: {
        if (material)
        {
            if (!material->SetProperty(m_LeafPropertyName, value))
            {
                log::warning("Cannot set property '%s' on material '%s': the material doesn't support this property.",
                    m_LeafPropertyName.c_str(), material->name.c_str());
            }
        }
        else
        {
            const auto& leaf = node->GetLeaf();
            if (leaf)
            {
                if (!leaf->SetProperty(m_LeafPropertyName, value))
                {
                    log::warning("Cannot set property '%s' on node '%s': the leaf doesn't support this property.",
                        m_LeafPropertyName.c_str(), node->GetName().c_str());
                }
            }
            else
            {
                log::warning("Cannot set property '%s' on node '%s' which has no leaf.",
                    m_LeafPropertyName.c_str(), node->GetName().c_str());
            }
        }
        break;
    }
        
    case AnimationAttribute::Undefined:
    default:
        log::warning("Unsupported animation target (%d), ignoring.", uint32_t(m_Attribute));
        return false;
    }

    return true;
}

std::shared_ptr<SceneGraphLeaf> SceneGraphAnimation::Clone()
{
    auto copy = std::make_shared<SceneGraphAnimation>();
    for (const auto& channel : m_Channels)
    {
        auto channelCopy = std::make_shared<SceneGraphAnimationChannel>(
            channel->GetSampler(), channel->GetTargetNode(), channel->GetAttribute());
        copy->AddChannel(channelCopy);
    }
    return std::static_pointer_cast<SceneGraphLeaf>(copy);
}

bool SceneGraphAnimationChannel::IsValid() const
{
    return !m_TargetNode.expired();
}

void SceneGraphAnimation::AddChannel(const std::shared_ptr<SceneGraphAnimationChannel>& channel)
{
    m_Channels.push_back(channel);
    m_Duration = std::max(m_Duration, channel->GetSampler()->GetEndTime());
;}

bool SceneGraphAnimation::Apply(float time) const
{
    bool success = false;

    for (const auto& channel : m_Channels)
    {
        success = channel->Apply(time) && success;
    }

    return success;
}

bool SceneGraphAnimation::IsVald() const
{
    for (const auto& channel : m_Channels)
    {
        if (!channel->IsValid())
            return false;
    }

    return true;
}

void SceneGraph::RegisterLeaf(const std::shared_ptr<SceneGraphLeaf>& leaf)
{
    if (!leaf)
        return;
    
    auto meshInstance = std::dynamic_pointer_cast<MeshInstance>(leaf);
    if (meshInstance)
    {
        const auto& mesh = meshInstance->GetMesh();
        if (mesh)
        {
            if (m_Meshes.AddRef(mesh))
            {
                m_GeometryCount += mesh->geometries.size();
                if (OnMeshAdded)
                    OnMeshAdded(mesh);
            }

            for (const auto& geometry : mesh->geometries)
            {
                if (m_Materials.AddRef(geometry->material) && OnMaterialAdded)
                    OnMaterialAdded(geometry->material);
            }

            if (mesh->skinPrototype)
            {
                if (m_Meshes.AddRef(mesh->skinPrototype))
                {
                    m_GeometryCount += mesh->skinPrototype->geometries.size();
                    if (OnMeshAdded)
                        OnMeshAdded(mesh->skinPrototype);
                }
            }
        }
        m_MeshInstances.push_back(meshInstance);

        auto skinnedInstance = std::dynamic_pointer_cast<SkinnedMeshInstance>(meshInstance);
        if (skinnedInstance)
        {
            m_SkinnedMeshInstances.push_back(skinnedInstance);
        }

        return;
    }
    
    auto animation = std::dynamic_pointer_cast<SceneGraphAnimation>(leaf);
    if (animation)
    {
        m_Animations.push_back(animation);
        return;
    }

    auto camera = std::dynamic_pointer_cast<SceneCamera>(leaf);
    if (camera)
    {
        m_Cameras.push_back(camera);
        return;
    }

    auto light = std::dynamic_pointer_cast<Light>(leaf);
    if (light)
    {
        m_Lights.push_back(light);
        return;
    }
}

void SceneGraph::UnregisterLeaf(const std::shared_ptr<SceneGraphLeaf>& leaf)
{
    if (!leaf)
        return;

    auto meshInstance = std::dynamic_pointer_cast<MeshInstance>(leaf);
    if (meshInstance)
    {
        const auto& mesh = meshInstance->GetMesh();
        if (mesh)
        {
            if (m_Meshes.Release(mesh))
            {
                m_GeometryCount += mesh->geometries.size();
                if (OnMeshRemoved)
                    OnMeshRemoved(mesh);
            }

            for (const auto& geometry : mesh->geometries)
            {
                if (m_Materials.Release(geometry->material) && OnMaterialRemoved)
                    OnMaterialRemoved(geometry->material);
            }

            if (mesh->skinPrototype)
            {
                if (m_Meshes.Release(mesh->skinPrototype))
                {
                    m_GeometryCount += mesh->skinPrototype->geometries.size();
                    if (OnMeshRemoved)
                        OnMeshRemoved(mesh->skinPrototype);
                }
            }
        }

        auto it = std::find(m_MeshInstances.begin(), m_MeshInstances.end(), meshInstance);
        if (it != m_MeshInstances.end())
            m_MeshInstances.erase(it);
        return;
    }

    auto skinnedInstance = std::dynamic_pointer_cast<SkinnedMeshInstance>(leaf);
    if (skinnedInstance)
    {
        auto it = std::find(m_SkinnedMeshInstances.begin(), m_SkinnedMeshInstances.end(), skinnedInstance);
        if (it != m_SkinnedMeshInstances.end())
            m_SkinnedMeshInstances.erase(it);
        return;
    }

    auto animation = std::dynamic_pointer_cast<SceneGraphAnimation>(leaf);
    if (animation)
    {
        auto it = std::find(m_Animations.begin(), m_Animations.end(), animation);
        if (it != m_Animations.end())
            m_Animations.erase(it);
        return;
    }

    auto camera = std::dynamic_pointer_cast<SceneCamera>(leaf);
    if (camera)
    {
        auto it = std::find(m_Cameras.begin(), m_Cameras.end(), camera);
        if (it != m_Cameras.end())
            m_Cameras.erase(it);
        return;
    }

    auto light = std::dynamic_pointer_cast<Light>(leaf);
    if (light)
    {
        auto it = std::find(m_Lights.begin(), m_Lights.end(), light);
        if (it != m_Lights.end())
            m_Lights.erase(it);
        return;
    }
}

std::shared_ptr<SceneGraphNode> SceneGraph::SetRootNode(const std::shared_ptr<SceneGraphNode>& root)
{
    auto oldRoot = m_Root;
    if (m_Root)
        Detach(m_Root);

    Attach(nullptr, root);

    return oldRoot;
}

std::shared_ptr<SceneGraphNode> SceneGraph::Attach(const std::shared_ptr<SceneGraphNode>& parent, const std::shared_ptr<SceneGraphNode>& child)
{
    auto parentGraph = parent ? parent->m_Graph.lock() : shared_from_this();
    auto childGraph = child->m_Graph.lock();

    if (!parentGraph && !childGraph)
    {
        // operating on an orphaned subgraph - do not copy or register anything

        assert(parent);
        child->m_NextSibling = parent->m_FirstChild;
        child->m_Parent = parent.get();
        parent->m_FirstChild = child;
        return child;
    }

    assert(parentGraph.get() == this);
    std::shared_ptr<SceneGraphNode> attachedChild;
    
    if (childGraph)
    {
        // attaching a subgraph that already belongs to a graph - this one or another
        // copy the subgraph first

        // keep a mapping of old nodes to new nodes to patch the copied animations
        std::unordered_map<SceneGraphNode*, std::shared_ptr<SceneGraphNode>> nodeMap;
        
        SceneGraphNode* currentParent = parent.get();
        SceneGraphWalker walker(child.get());
        while (walker)
        {
            // make a copy of the current node
            std::shared_ptr<SceneGraphNode> copy = std::make_shared<SceneGraphNode>();
            nodeMap[walker.Get()] = copy;

            copy->m_Name = walker->m_Name;
            copy->m_Parent = currentParent;
            copy->m_Graph = weak_from_this();
            copy->m_Dirty = walker->m_Dirty;

            if (walker->m_HasLocalTransform)
            {
                copy->SetTransform(&walker->m_Translation, &walker->m_Rotation, &walker->m_Scaling);
            }

            if (walker->m_Leaf)
            {
                auto leafCopy = walker->m_Leaf->Clone();
                copy->SetLeaf(leafCopy);
            }

            // attach the copy to the new parent
            if (currentParent)
            {
                copy->m_NextSibling = currentParent->m_FirstChild;
                currentParent->m_FirstChild = copy;
            }
            else
            {
                m_Root = copy;
            }

            // if it's the first node we copied, store it as the new root
            if (!attachedChild)
                attachedChild = copy;

            // go to the next node
            int deltaDepth = walker.Next(true);

            if (deltaDepth > 0)
            {
                currentParent = copy.get();
            }
            else
            {
                while (deltaDepth++ < 0)
                {
                    // reverse the children list of this parent to make them consistent with the original
                    currentParent->ReverseChildren();

                    // go up the new tree
                    currentParent = currentParent->m_Parent;
                }
            }
        }

        // go over the new nodes and patch the cloned animations and skinned groups to use the *new* nodes
        walker = SceneGraphWalker(attachedChild.get());
        while (walker)
        {
            if (auto animation = dynamic_cast<SceneGraphAnimation*>(walker->m_Leaf.get()))
            {
                for (const auto& channel : animation->GetChannels())
                {
                    auto newNode = nodeMap[channel->GetTargetNode().get()];
                    if (newNode)
                    {
                        channel->SetTargetNode(newNode);
                    }
                }
            }
            else if (auto skinnedInstance = dynamic_cast<SkinnedMeshInstance*>(walker->m_Leaf.get()))
            {
                for (auto& joint : skinnedInstance->joints)
                {
                    auto newNode = nodeMap[joint.node.get()];
                    if (newNode)
                    {
                        joint.node = newNode;
                    }
                }
            }
            else if (auto meshReference = dynamic_cast<SkinnedMeshReference*>(walker->m_Leaf.get()))
            {
                auto instance = meshReference->m_Instance.lock();
                if (instance)
                {
                    auto oldNode = instance->GetNode();

                    auto newNode = nodeMap[oldNode];
                    if (newNode)
                        meshReference->m_Instance = std::dynamic_pointer_cast<SkinnedMeshInstance>(newNode->m_Leaf);
                    else
                        meshReference->m_Instance.reset();
                }
            }

            walker.Next(true);
        }
    }
    else
    {
        // attaching a subgraph that has been detached from another graph (or never attached)

        SceneGraphWalker walker(child.get());
        while (walker)
        {
            walker->m_Graph = weak_from_this();
            auto leaf = walker->GetLeaf();
            if (leaf)
                RegisterLeaf(leaf);
            walker.Next(true);
        }

        child->m_Parent = parent.get();

        if (parent)
        {
            child->m_NextSibling = parent->m_FirstChild;
            parent->m_FirstChild = child;
        }
        else
        {
            m_Root = child;
        }

        attachedChild = child;
    }

    attachedChild->PropagateDirtyFlags(SceneGraphNode::DirtyFlags::SubgraphStructure
        | (child->m_Dirty & SceneGraphNode::DirtyFlags::SubgraphMask));

    return attachedChild;
}

std::shared_ptr<SceneGraphNode> SceneGraph::AttachLeafNode(const std::shared_ptr<SceneGraphNode>& parent, const std::shared_ptr<SceneGraphLeaf>& leaf)
{
    auto node = std::make_shared<SceneGraphNode>();
    if (leaf->GetNode())
        node->SetLeaf(leaf->Clone());
    else
        node->SetLeaf(leaf);
    return Attach(parent, node);
}

std::shared_ptr<SceneGraphNode> SceneGraph::Detach(const std::shared_ptr<SceneGraphNode>& node)
{
    auto nodeGraph = node->m_Graph.lock();

    if (nodeGraph)
    {
        assert(nodeGraph.get() == this);

        // unregister all leaves in the subgraph, detach all nodes from the graph
        SceneGraphWalker walker(node.get());
        while (walker)
        {
            walker->m_Graph.reset();
            auto leaf = walker->GetLeaf();
            if (leaf)
                UnregisterLeaf(leaf);
            walker.Next(true);
        }
    }

    // remove the node from its parent
    if (node->m_Parent)
    {
        node->m_Parent->PropagateDirtyFlags(SceneGraphNode::DirtyFlags::SubgraphStructure);

        std::shared_ptr<SceneGraphNode>* sibling = &node->m_Parent->m_FirstChild;
        while (*sibling && *sibling != node)
            sibling = &(*sibling)->m_NextSibling;
        if (*sibling)
            *sibling = node->m_NextSibling;
    }

    node->m_Parent = nullptr;
    node->m_NextSibling.reset();

    if (m_Root == node)
    {
        m_Root.reset();
        m_Root = std::make_shared<SceneGraphNode>();
    }

    return node;
}

std::shared_ptr<SceneGraphNode> SceneGraph::FindNode(const std::filesystem::path& path, SceneGraphNode* context) const
{
    auto pathComponent = path.begin();

    if (pathComponent == path.end())
        return nullptr;

    if (*pathComponent == "/")
    {
        context = m_Root.get();
        ++pathComponent;
    }

    if (!context)
    {
        log::error("Relative node queries with NULL context are not supported");
        return nullptr;
    }

    SceneGraphNode* current = context;
    
    while (current && pathComponent != path.end())
    {
        if (*pathComponent == "..")
        {
            current = current->GetParent();
            ++pathComponent;
            continue;
        }

        SceneGraphNode* child;
        for (child = current->GetFirstChild(); child; child = child->GetNextSibling())
        {
            if (child->GetName() == *pathComponent)
                break;
        }

        if (child)
        {
            current = child;
            ++pathComponent;
            continue;
        }

        return nullptr;
    }

    return current->shared_from_this();
}

void SceneGraph::Refresh(uint32_t frameIndex)
{
    struct StackItem
    {
        bool supergraphTransformUpdated = false;
        bool supergraphContentUpdate = false;
    };

    bool structureDirty = HasPendingStructureChanges();

    StackItem context;
    std::vector<StackItem> stack;

    SceneGraphWalker walker(m_Root.get());
    while (walker)
    {
        auto current = walker.Get();
        auto parent = current->m_Parent;

        // save the current local/global transforms as previous
        current->m_PrevLocalTransform = current->m_LocalTransform;
        current->m_PrevGlobalTransform = current->m_GlobalTransform;
        current->m_PrevGlobalTransformFloat = current->m_GlobalTransformFloat;

        bool currentTransformUpdated = (current->m_Dirty & SceneGraphNode::DirtyFlags::LocalTransform) != 0;
        bool currentContentUpdated = (current->m_Dirty & SceneGraphNode::DirtyFlags::SubgraphContentUpdate) != 0;

        if (currentTransformUpdated)
        {
            current->UpdateLocalTransform();
        }

        // update the global transform of the current node
        if (parent)
        {
            current->m_GlobalTransform = current->m_HasLocalTransform
                ? current->m_LocalTransform * parent->m_GlobalTransform
                : parent->m_GlobalTransform;
        }
        else
        {
            current->m_GlobalTransform = current->m_LocalTransform;
        }
        current->m_GlobalTransformFloat = dm::affine3(current->m_GlobalTransform);

        // initialize the global bbox of the current node, start with the leaf (or an empty box if there is no leaf)
        if ((current->m_Dirty & (SceneGraphNode::DirtyFlags::SubgraphStructure | SceneGraphNode::DirtyFlags::SubgraphTransforms)) != 0 || context.supergraphTransformUpdated)
        {
            current->m_GlobalBoundingBox = dm::box3::empty();
            if (current->m_Leaf)
            {
                dm::box3 localBoundingBox = current->m_Leaf->GetLocalBoundingBox();
                if (!localBoundingBox.isempty())
                    current->m_GlobalBoundingBox = localBoundingBox * current->m_GlobalTransformFloat;
            }
        }

        // initialize the content flags of the current node
        if (context.supergraphContentUpdate || (current->m_Dirty & (SceneGraphNode::DirtyFlags::SubgraphStructure | SceneGraphNode::DirtyFlags::SubgraphContentUpdate)) != 0)
        {
            if (current->m_Leaf)
                current->m_LeafContent = current->m_Leaf->GetContentFlags();
            else
                current->m_LeafContent = SceneContentFlags::None;

            current->m_SubgraphContent = current->m_LeafContent;
        }

        // store the update frame number for skinned groups
        if (auto meshReference = dynamic_cast<SkinnedMeshReference*>(current->m_Leaf.get()))
        {
            if ((current->m_Dirty & SceneGraphNode::DirtyFlags::LocalTransform) != 0)
            {
                auto instance = meshReference->m_Instance.lock();
                if (instance)
                {
                    instance->m_LastUpdateFrameIndex = frameIndex;
                }
            }
        }

        // advance to the next node
        bool subgraphNeedsRefresh = (current->m_Dirty & SceneGraphNode::DirtyFlags::SubgraphMask) != 0;
        int deltaDepth = walker.Next(subgraphNeedsRefresh || context.supergraphTransformUpdated || context.supergraphContentUpdate);

        // save the dirty flag to update the same nodes' previous transforms on the next frame
        current->m_Dirty = (currentTransformUpdated || context.supergraphTransformUpdated)
            ? SceneGraphNode::DirtyFlags::PrevTransform
            : SceneGraphNode::DirtyFlags::None;
        
        if (deltaDepth > 0)
        {
            // going down the tree
            stack.push_back(context);
            context.supergraphTransformUpdated |= currentTransformUpdated;
            context.supergraphContentUpdate |= currentContentUpdated;
        }
        else
        {
            // sibling or going up. done with our bbox, update the parent.
            if (parent)
            {
                parent->m_GlobalBoundingBox |= current->m_GlobalBoundingBox;
                if ((current->m_Dirty & SceneGraphNode::DirtyFlags::PrevTransform) != 0)
                    parent->m_Dirty |= SceneGraphNode::DirtyFlags::SubgraphPrevTransforms;
                parent->m_Dirty |= current->m_Dirty & SceneGraphNode::DirtyFlags::SubgraphMask;
                parent->m_SubgraphContent |= current->m_SubgraphContent;
            }

            // going up the tree, potentially multiple levels
            while (deltaDepth++ < 0)
            {
                if (stack.empty())
                {
                    context = StackItem(); // should only happen once when we reach the top
                }
                else
                {
                    // we're moving up to node 'parent' whose parent is 'newParent'
                    // update 'newParent's bbox with the finished bbox of 'parent'
                    assert(parent);
                    current = parent;
                    parent = current->m_Parent;

                    if (parent)
                    {
                        parent->m_GlobalBoundingBox |= current->m_GlobalBoundingBox;
                        parent->m_Dirty |= current->m_Dirty & SceneGraphNode::DirtyFlags::SubgraphMask;
                        parent->m_SubgraphContent |= current->m_SubgraphContent;
                    }

                    context = stack.back();
                    stack.pop_back();
                }
            }
        }
    }

    if (structureDirty)
    {
        int instanceIndex = 0;
        int geometryInstanceIndex = 0;
        for (const auto& instance : m_MeshInstances)
        {
            instance->m_InstanceIndex = instanceIndex;
            ++instanceIndex;

            const auto& mesh = instance->GetMesh();
            instance->m_GeometryInstanceIndex = geometryInstanceIndex;
            geometryInstanceIndex += int(mesh->geometries.size());
        }
        m_GeometryInstancesCount = geometryInstanceIndex;

        int meshIndex = 0;
        int geometryIndex = 0;
        for (const auto& mesh : m_Meshes)
        {
            for (const auto& geometry : mesh->geometries)
            {
                geometry->globalGeometryIndex = geometryIndex;
                ++geometryIndex;
            }

            mesh->globalMeshIndex = meshIndex;
            ++meshIndex;
        }

        assert(m_GeometryCount == geometryIndex);

        int materialIndex = 0;
        for (const auto& material : m_Materials)
        {
            material->materialID = materialIndex;
            ++materialIndex;
        }
    }
}

std::shared_ptr<SceneGraphLeaf> SceneTypeFactory::CreateLeaf(const std::string& type)
{
    if (type == "DirectionalLight")
    {
        return std::make_shared<DirectionalLight>();
    }
    if (type == "PointLight")
    {
        return std::make_shared<PointLight>();
    }
    if (type == "SpotLight")
    {
        return std::make_shared<SpotLight>();
    }
    if (type == "PerspectiveCamera")
    {
        return std::make_shared<PerspectiveCamera>();
    }
    if (type == "OrthographicCamera")
    {
        return std::make_shared<OrthographicCamera>();
    }

    return nullptr;
}

std::shared_ptr<Material> SceneTypeFactory::CreateMaterial()
{
    return std::make_shared<Material>();
}

std::shared_ptr<MeshInfo> SceneTypeFactory::CreateMesh()
{
    return std::make_shared<MeshInfo>();
}

std::shared_ptr<MeshGeometry> SceneTypeFactory::CreateMeshGeometry()
{
    return std::make_shared<MeshGeometry>();
}

std::shared_ptr<MeshInstance> SceneTypeFactory::CreateMeshInstance(const std::shared_ptr<MeshInfo>& mesh)
{
    return std::make_shared<MeshInstance>(mesh);
}

void donut::engine::PrintSceneGraph(const std::shared_ptr<SceneGraphNode>& root)
{
    SceneGraphWalker walker(root.get());
    int depth = 0;
    while(walker)
    {
        std::stringstream ss;

        for (int i = 0; i < depth; i++)
            ss << "  ";

        if (walker->GetName().empty())
            ss << "<Unnamed>";
        else
            ss << walker->GetName();

        bool hasTranslation = dm::any(walker->GetTranslation() != dm::double3::zero());
        bool hasRotation = dm::any(walker->GetRotation() != dm::dquat::identity());
        bool hasScaling = dm::any(walker->GetScaling() != dm::double3(1.0));

        if (hasTranslation || hasRotation || hasScaling)
        {
            ss << " (";
            if (hasTranslation) ss << "T";
            if (hasRotation) ss << "R";
            if (hasScaling) ss << "S";
            ss << ")";
        }

        auto bbox = walker->GetGlobalBoundingBox();
        if (!bbox.isempty())
        {
            ss << " [" << bbox.m_mins.x << ", " << bbox.m_mins.y << ", " << bbox.m_mins.z << " .. "
                      << bbox.m_maxs.x << ", " << bbox.m_maxs.y << ", " << bbox.m_maxs.z << "]";
        }

        if (walker->GetLeaf())
        {
            ss << ": ";

            if (auto meshInstance = dynamic_cast<MeshInstance*>(walker->GetLeaf().get()))
            {
                if (!meshInstance->GetMesh()->name.empty())
                    ss << meshInstance->GetMesh()->name;
                else
                    ss << "Unnamed Mesh";

                ss << " (" << meshInstance->GetMesh()->geometries.size() << " geometries)";

                auto skinnedInstance = dynamic_cast<SkinnedMeshInstance*>(meshInstance);
                if (skinnedInstance)
                {
                    ss << " - skinned, " << skinnedInstance->joints.size() << " joints";
                }
            }
            else if (auto animation = dynamic_cast<SceneGraphAnimation*>(walker->GetLeaf().get()))
            {
                ss << "Animation (" << animation->GetChannels().size() << " channels)";
                log::info("%s", ss.str().c_str());
                ss.str(std::string()); // clear

                for (const auto& channel : animation->GetChannels())
                {
                    for (int i = 0; i < depth + 1; i++)
                        ss << "  ";

                    auto targetNode = channel->GetTargetNode();
                    if (targetNode)
                        if (!targetNode->GetName().empty())
                            ss << targetNode->GetName();
                        else
                            ss << "<Unnamed Target>";
                    else
                        ss << "<No Target>";

                    ss << " (";
                    switch(channel->GetAttribute())
                    {
                    case AnimationAttribute::Scaling: ss << "Scaling"; break;
                    case AnimationAttribute::Rotation: ss << "Rotation"; break;
                    case AnimationAttribute::Translation: ss << "Translation"; break;
                    case AnimationAttribute::Undefined:
                    default:
                        ss << "Unknown Attribute";
                    }
                    ss << "): ";
                    auto keyframes = channel->GetSampler()->GetKeyframes();
                    ss << keyframes.size() << " keyframes";
                    if (!keyframes.empty())
                    {
                        ss << ", " << keyframes[0].time << "s - " << keyframes[keyframes.size() - 1].time << "s";
                    }

                    log::info("%s", ss.str().c_str());
                    ss.str(std::string()); // clear
                }
            }
            else if (auto camera = dynamic_cast<SceneCamera*>(walker->GetLeaf().get()))
            {
                auto perspective = dynamic_cast<PerspectiveCamera*>(camera);
                auto orthographic = dynamic_cast<OrthographicCamera*>(camera);

                if (perspective)
                {
                    ss << "Perspective Camera (yFov = " << perspective->verticalFov
                        << ", zNear = " << perspective->zNear << ")";
                }
                else if (orthographic)
                {
                    ss << "Orthographic Camera (xMag = " << orthographic->xMag
                        << ", yMag = " << orthographic->yMag
                        << ", zNear = " << orthographic->zNear << ")";
                }
                else
                {
                    ss << "Unknown Type Camera";
                }
            }
            else if (auto light = dynamic_cast<Light*>(walker->GetLeaf().get()))
            {
                auto directional = dynamic_cast<DirectionalLight*>(light);
                auto point = dynamic_cast<PointLight*>(light);
                auto spot = dynamic_cast<SpotLight*>(light);

                if (directional)
                {
                    ss << "Directional Light (irradiance = " << directional->irradiance
                        << ", r = " << directional->color.x
                        << ", g = " << directional->color.y
                        << ", g = " << directional->color.z
                        << ", angularSize = " << directional->angularSize
                        << ")";
                }
                else if (point)
                {
                    ss << "Point Light (intensity = " << point->intensity
                        << ", r = " << point->color.x
                        << ", g = " << point->color.y
                        << ", g = " << point->color.z
                        << ", radius = " << point->radius
                        << ", range = " << point->range
                        << ")";
                }
                else if (spot)
                {
                    ss << "Spot Light (intensity = " << spot->intensity
                        << ", r = " << spot->color.x
                        << ", g = " << spot->color.y
                        << ", g = " << spot->color.z
                        << ", radius = " << spot->radius
                        << ", range = " << spot->range
                        << ", innerAngle = " << spot->innerAngle
                        << ", outerAngle = " << spot->outerAngle
                        << ")";
                }
                else
                {
                    ss << "Unknown Type Light";
                }
            }
            else if (auto ref = dynamic_cast<SkinnedMeshReference*>(walker->GetLeaf().get()))
            {
                ss << "Joint";
            }
            else
            {
                ss << "Unkwown Leaf Type";
            }
        }

        if (!ss.str().empty())
            log::info("%s", ss.str().c_str());

        depth += walker.Next(true);
    }
}
