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

#include <donut/engine/SceneTypes.h>
#include <donut/engine/KeyframeAnimation.h>
#include <donut/core/math/math.h>
#include <memory>
#include <unordered_map>
#include <utility>
#include <functional>
#include <filesystem>

namespace donut::engine
{
    class SceneGraph;
    class SceneGraphNode;
    class SceneTypeFactory;

    enum struct SceneContentFlags : uint32_t
    {
        None = 0,
        OpaqueMeshes = 0x01,
        AlphaTestedMeshes = 0x02,
        BlendedMeshes = 0x04,
        Lights = 0x08,
        Cameras = 0x10,
        Animations = 0x20
    };

    class SceneGraphLeaf
    {
    private:
        friend class SceneGraphNode;
        std::weak_ptr<SceneGraphNode> m_Node;

    protected:
        SceneGraphLeaf() = default;

    public:
        virtual ~SceneGraphLeaf() = default;

        [[nodiscard]] SceneGraphNode* GetNode() const { return m_Node.lock().get(); }
        [[nodiscard]] std::shared_ptr<SceneGraphNode> GetNodeSharedPtr() const { return m_Node.lock(); }
        [[nodiscard]] virtual dm::box3 GetLocalBoundingBox() { return dm::box3::empty(); }
        [[nodiscard]] virtual std::shared_ptr<SceneGraphLeaf> Clone() = 0;
        [[nodiscard]] virtual SceneContentFlags GetContentFlags() const { return SceneContentFlags::None; }
        [[nodiscard]] const std::string& GetName() const;
        void SetName(const std::string& name) const;
        virtual void Load(const Json::Value& node) { }
        virtual bool SetProperty(const std::string& name, const dm::float4& value) { return false; }

        // Non-copyable and non-movable
        SceneGraphLeaf(const SceneGraphLeaf&) = delete;
        SceneGraphLeaf(const SceneGraphLeaf&&) = delete;
        SceneGraphLeaf& operator=(const SceneGraphLeaf&) = delete;
        SceneGraphLeaf& operator=(const SceneGraphLeaf&&) = delete;
    };

    class MeshInstance : public SceneGraphLeaf
    {
    private:
        friend class SceneGraph;
        int m_InstanceIndex = -1;
        int m_GeometryInstanceIndex = -1;

    protected:
        std::shared_ptr<MeshInfo> m_Mesh;

    public:
        explicit MeshInstance(std::shared_ptr<MeshInfo> mesh)
            : m_Mesh(std::move(mesh))
        { }

        [[nodiscard]] const std::shared_ptr<MeshInfo>& GetMesh() const { return m_Mesh; }
        [[nodiscard]] int GetInstanceIndex() const { return m_InstanceIndex; }
        [[nodiscard]] int GetGeometryInstanceIndex() const { return m_GeometryInstanceIndex; }
        [[nodiscard]] dm::box3 GetLocalBoundingBox() override { return m_Mesh->objectSpaceBounds; }
        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        [[nodiscard]] SceneContentFlags GetContentFlags() const override;
        bool SetProperty(const std::string& name, const dm::float4& value) override;
    };

    struct SkinnedMeshJoint
    {
        std::shared_ptr<SceneGraphNode> node;
        dm::float4x4 inverseBindMatrix;
    };

    class SkinnedMeshInstance : public MeshInstance
    {
    private:
        friend class SceneGraph;
        std::shared_ptr<MeshInfo> m_PrototypeMesh;
        uint32_t m_LastUpdateFrameIndex = 0;
        std::shared_ptr<SceneTypeFactory> m_SceneTypeFactory;

    public:
        std::vector<SkinnedMeshJoint> joints;
        nvrhi::BufferHandle jointBuffer;
        nvrhi::BindingSetHandle skinningBindingSet;
        bool skinningInitialized = false;

        explicit SkinnedMeshInstance(std::shared_ptr<SceneTypeFactory> sceneTypeFactory, std::shared_ptr<MeshInfo> prototypeMesh);

        [[nodiscard]] const std::shared_ptr<MeshInfo>& GetPrototypeMesh() const { return m_PrototypeMesh; }
        [[nodiscard]] uint32_t GetLastUpdateFrameIndex() const { return m_LastUpdateFrameIndex; }
        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
    };

    // This leaf is attached to the joint nodes for a skeleton, and it makes them point at the mesh.
    // When the bones are updated, the mesh is flagged for rebuild.
    // Cannot do this through the graph because the skeleton can be separate from the mesh instance node.
    class SkinnedMeshReference : public SceneGraphLeaf
    {
    private:
        friend class SceneGraph;
        std::weak_ptr<SkinnedMeshInstance> m_Instance;
    public:
        explicit SkinnedMeshReference(std::shared_ptr<SkinnedMeshInstance> instance) : m_Instance(instance) { }
        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
    };

    class SceneCamera : public SceneGraphLeaf
    {
    public:
        [[nodiscard]] SceneContentFlags GetContentFlags() const override { return SceneContentFlags::Cameras; }

        [[nodiscard]] dm::affine3 GetViewToWorldMatrix() const;
        [[nodiscard]] dm::affine3 GetWorldToViewMatrix() const;
    };

    class PerspectiveCamera : public SceneCamera
    {
    public:
        float zNear = 1.f;
        float verticalFov = 1.f; // in radians
        std::optional<float> zFar; // use reverse infinite projection if not specified
        std::optional<float> aspectRatio;

        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        void Load(const Json::Value& node) override;
        bool SetProperty(const std::string& name, const dm::float4& value) override;
    };

    class OrthographicCamera : public SceneCamera
    {
    public:
        float zNear = 0.f;
        float zFar = 1.f;
        float xMag = 1.f;
        float yMag = 1.f;

        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        void Load(const Json::Value& node) override;
        bool SetProperty(const std::string& name, const dm::float4& value) override;
    };

    class IShadowMap;

    class Light : public SceneGraphLeaf
    {
    public:
        std::shared_ptr<IShadowMap> shadowMap;
        int shadowChannel = -1;
        dm::float3 color = dm::colors::white;

        [[nodiscard]] SceneContentFlags GetContentFlags() const override { return SceneContentFlags::Lights; }

        [[nodiscard]] virtual int GetLightType() const = 0;
        virtual void FillLightConstants(LightConstants& lightConstants) const;
        virtual void Store(Json::Value& node) const { }
        bool SetProperty(const std::string& name, const dm::float4& value) override;

        [[nodiscard]] dm::double3 GetPosition() const;
        [[nodiscard]] dm::double3 GetDirection() const;

        void SetPosition(const dm::double3& position) const;
        void SetDirection(const dm::double3& direction) const;
    };

    class DirectionalLight : public Light
    {
    public:
        float irradiance = 1.f; // Target illuminance (lm/m2) of surfaces lit by this light; multiplied by `color`.
        float angularSize = 0.f; // Angular size of the light source, in degrees.
        std::vector<std::shared_ptr<IShadowMap>> perObjectShadows;

        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        [[nodiscard]] int GetLightType() const override { return LightType_Directional; }
        void FillLightConstants(LightConstants& lightConstants) const override;
        void Load(const Json::Value& node) override;
        void Store(Json::Value& node) const override;
        bool SetProperty(const std::string& name, const dm::float4& value) override;
    };

    class SpotLight : public Light
    {
    public:
        float intensity = 1.f;  // Luminous intensity of the light (lm/sr) in its primary direction; multiplied by `color`.
        float radius = 0.f;     // Radius of the light sphere, in world units.
        float range = 0.f;      // Range of influence for the light. 0 means infinite range.
        float innerAngle = 180.f;    // Apex angle of the full-bright cone, in degrees; constant intensity inside the inner cone, smooth falloff between inside and outside.
        float outerAngle = 180.f;    // Apex angle of the light cone, in degrees - everything outside of that cone is dark.

        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        [[nodiscard]] int GetLightType() const override { return LightType_Spot; }
        void FillLightConstants(LightConstants& lightConstants) const override;
        void Load(const Json::Value& node) override;
        void Store(Json::Value& node) const override;
        bool SetProperty(const std::string& name, const dm::float4& value) override;
    };

    class PointLight : public Light
    {
    public:
        float intensity = 1.f;  // Luminous intensity of the light (lm/sr); multiplied by `color`.
        float radius = 0.f;    // Radius of the light sphere, in world units.
        float range = 0.f;     // Range of influence for the light. 0 means infinite range.

        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        [[nodiscard]] int GetLightType() const override { return LightType_Point; }
        void FillLightConstants(LightConstants& lightConstants) const override;
        void Load(const Json::Value& node) override;
        void Store(Json::Value& node) const override;
        bool SetProperty(const std::string& name, const dm::float4& value) override;
    };
    
    class SceneGraphNode final : public std::enable_shared_from_this<SceneGraphNode>
    {
    public:
        enum struct DirtyFlags : uint32_t
        {
            None                    = 0,
            LocalTransform          = 0x01,
            PrevTransform           = 0x02,
            Leaf                    = 0x04,
            SubgraphStructure       = 0x08,
            SubgraphTransforms      = 0x10,
            SubgraphPrevTransforms  = 0x20,
            SubgraphContentUpdate   = 0x40,
            SubgraphMask            = (SubgraphStructure | SubgraphTransforms | SubgraphPrevTransforms | SubgraphContentUpdate)
        };

    private:
        friend class SceneGraph;
        std::weak_ptr<SceneGraph> m_Graph;
        SceneGraphNode* m_Parent = nullptr;
        std::shared_ptr<SceneGraphNode> m_FirstChild;
        std::shared_ptr<SceneGraphNode> m_NextSibling;
        std::shared_ptr<SceneGraphLeaf> m_Leaf;

        std::string m_Name;
        dm::daffine3 m_LocalTransform = dm::daffine3::identity();
        dm::daffine3 m_GlobalTransform = dm::daffine3::identity();
        dm::affine3 m_GlobalTransformFloat = dm::affine3::identity();
        dm::daffine3 m_PrevLocalTransform = dm::daffine3::identity();
        dm::daffine3 m_PrevGlobalTransform = dm::daffine3::identity();
        dm::affine3 m_PrevGlobalTransformFloat = dm::affine3::identity();
        dm::dquat m_Rotation = dm::dquat::identity();
        dm::double3 m_Scaling = 1.0;
        dm::double3 m_Translation = 0.0;
        dm::box3 m_GlobalBoundingBox = dm::box3::empty();
        bool m_HasLocalTransform = false;
        DirtyFlags m_Dirty = DirtyFlags::None;
        SceneContentFlags m_LeafContent = SceneContentFlags::None;
        SceneContentFlags m_SubgraphContent = SceneContentFlags::None;

        void UpdateLocalTransform();
        void PropagateDirtyFlags(SceneGraphNode::DirtyFlags flags);

    public:
        SceneGraphNode() = default;
        /* non-virtual */ ~SceneGraphNode() = default;

        [[nodiscard]] const dm::dquat& GetRotation() const { return m_Rotation; }
        [[nodiscard]] const dm::double3& GetScaling() const { return m_Scaling; }
        [[nodiscard]] const dm::double3& GetTranslation() const { return m_Translation; }

        [[nodiscard]] const dm::daffine3& GetLocalToParentTransform() const { return m_LocalTransform; }
        [[nodiscard]] const dm::daffine3& GetLocalToWorldTransform() const { return m_GlobalTransform; }
        [[nodiscard]] const dm::affine3& GetLocalToWorldTransformFloat() const { return m_GlobalTransformFloat; }
        [[nodiscard]] const dm::daffine3& GetPrevLocalToParentTransform() const { return m_PrevLocalTransform; }
        [[nodiscard]] const dm::daffine3& GetPrevLocalToWorldTransform() const { return m_PrevGlobalTransform; }
        [[nodiscard]] const dm::affine3& GetPrevLocalToWorldTransformFloat() const { return m_PrevGlobalTransformFloat; }
        [[nodiscard]] const dm::box3& GetGlobalBoundingBox() const { return m_GlobalBoundingBox; }
        [[nodiscard]] DirtyFlags GetDirtyFlags() const { return m_Dirty; }
        [[nodiscard]] SceneContentFlags GetLeafContentFlags() const { return m_LeafContent; }
        [[nodiscard]] SceneContentFlags GetSubgraphContentFlags() const { return m_SubgraphContent; }

        [[nodiscard]] SceneGraphNode* GetParent() const { return m_Parent; }
        [[nodiscard]] SceneGraphNode* GetFirstChild() const { return m_FirstChild.get(); }
        [[nodiscard]] SceneGraphNode* GetNextSibling() const { return m_NextSibling.get(); }
        [[nodiscard]] const std::shared_ptr<SceneGraphLeaf>& GetLeaf() const { return m_Leaf; }

        [[nodiscard]] const std::string& GetName() const { return m_Name; }
        [[nodiscard]] std::shared_ptr<SceneGraph> GetGraph() const { return m_Graph.lock(); }

        [[nodiscard]] std::filesystem::path GetPath() const;

        void InvalidateContent();

        void SetTransform(const dm::double3* translation, const dm::dquat* rotation, const dm::double3* scaling);
        void SetScaling(const dm::double3& scaling);
        void SetRotation(const dm::dquat& rotation);
        void SetTranslation(const dm::double3& translation);
        void SetLeaf(const std::shared_ptr<SceneGraphLeaf>& leaf);
        void SetName(const std::string& name);

        void ReverseChildren();

        // Non-copyable and non-movable
        SceneGraphNode(const SceneGraphNode&) = delete;
        SceneGraphNode(const SceneGraphNode&&) = delete;
        SceneGraphNode& operator=(const SceneGraphNode&) = delete;
        SceneGraphNode& operator=(const SceneGraphNode&&) = delete;
    };

    inline SceneGraphNode::DirtyFlags operator | (SceneGraphNode::DirtyFlags a, SceneGraphNode::DirtyFlags b) { return SceneGraphNode::DirtyFlags(uint32_t(a) | uint32_t(b)); }
    inline SceneGraphNode::DirtyFlags operator & (SceneGraphNode::DirtyFlags a, SceneGraphNode::DirtyFlags b) { return SceneGraphNode::DirtyFlags(uint32_t(a) & uint32_t(b)); }
    inline SceneGraphNode::DirtyFlags operator ~ (SceneGraphNode::DirtyFlags a) { return SceneGraphNode::DirtyFlags(~uint32_t(a)); }
    inline SceneGraphNode::DirtyFlags operator |= (SceneGraphNode::DirtyFlags& a, SceneGraphNode::DirtyFlags b) { a = SceneGraphNode::DirtyFlags(uint32_t(a) | uint32_t(b)); return a; }
    inline SceneGraphNode::DirtyFlags operator &= (SceneGraphNode::DirtyFlags& a, SceneGraphNode::DirtyFlags b) { a = SceneGraphNode::DirtyFlags(uint32_t(a) & uint32_t(b)); return a; }
    inline bool operator !(SceneGraphNode::DirtyFlags a) { return uint32_t(a) == 0; }
    inline bool operator ==(SceneGraphNode::DirtyFlags a, uint32_t b) { return uint32_t(a) == b; }
    inline bool operator !=(SceneGraphNode::DirtyFlags a, uint32_t b) { return uint32_t(a) != b; }

    inline SceneContentFlags operator | (SceneContentFlags a, SceneContentFlags b) { return SceneContentFlags(uint32_t(a) | uint32_t(b)); }
    inline SceneContentFlags operator & (SceneContentFlags a, SceneContentFlags b) { return SceneContentFlags(uint32_t(a) & uint32_t(b)); }
    inline SceneContentFlags operator ~ (SceneContentFlags a) { return SceneContentFlags(~uint32_t(a)); }
    inline SceneContentFlags operator |= (SceneContentFlags& a, SceneContentFlags b) { a = SceneContentFlags(uint32_t(a) | uint32_t(b)); return a; }
    inline SceneContentFlags operator &= (SceneContentFlags& a, SceneContentFlags b) { a = SceneContentFlags(uint32_t(a) & uint32_t(b)); return a; }
    inline bool operator !(SceneContentFlags a) { return uint32_t(a) == 0; }
    inline bool operator ==(SceneContentFlags a, uint32_t b) { return uint32_t(a) == b; }
    inline bool operator !=(SceneContentFlags a, uint32_t b) { return uint32_t(a) != b; }

    // Scene graph traversal helper. Similar to an iterator, but only goes forward.
    // Create a SceneGraphWalker from a node, and it will go over every node in the sub-tree of that node.
    // On each location, the walker can move either down (deeper) or right (siblings), depending on the needs.
    class SceneGraphWalker final
    {
    private:
        SceneGraphNode* m_Current;
        SceneGraphNode* m_Scope;
    public:
        SceneGraphWalker() = default;

        explicit SceneGraphWalker(SceneGraphNode* scope)
            : m_Current(scope)
            , m_Scope(scope)
        { }

        SceneGraphWalker(SceneGraphNode* current, SceneGraphNode* scope)
            : m_Current(current)
            , m_Scope(scope)
        { }

        [[nodiscard]] SceneGraphNode* Get() const { return m_Current; }
        [[nodiscard]] operator bool() const { return m_Current != nullptr; }
        SceneGraphNode* operator->() const { return m_Current; }
        
        // Moves the pointer to the first child of the current node, if it exists, and if allowChildren = true.
        // Otherwise, moves the pointer to the next sibling of the current node, if it exists.
        // Otherwise, goes up and tries to find the next sibiling up the hierarchy.
        // Returns the depth of the new node relative to the current node.
        int Next(bool allowChildren);

        // Moves the pointer to the parent of the current node, up to the scope.
        // Note that using Up and Next together may result in an infinite loop.
        // Returns the depth of the new node relative to the current node.
        int Up();
    };

    enum class AnimationAttribute : uint32_t
    {
        Undefined,
        Scaling,
        Rotation,
        Translation,
        LeafProperty
    };

    class SceneGraphAnimationChannel
    {
    private:
        std::shared_ptr<animation::Sampler> m_Sampler;
        std::weak_ptr<SceneGraphNode> m_TargetNode;
        std::weak_ptr<Material> m_TargetMaterial;
        AnimationAttribute m_Attribute;
        std::string m_LeafPropertyName;

    public:
        SceneGraphAnimationChannel(std::shared_ptr<animation::Sampler> sampler, const std::shared_ptr<SceneGraphNode>& targetNode, AnimationAttribute attribute)
            : m_Sampler(std::move(sampler))
            , m_TargetNode(targetNode)
            , m_Attribute(attribute)
        { }
        SceneGraphAnimationChannel(std::shared_ptr<animation::Sampler> sampler, const std::shared_ptr<Material>& targetMaterial)
            : m_Sampler(std::move(sampler))
            , m_TargetMaterial(targetMaterial)
            , m_Attribute(AnimationAttribute::LeafProperty)
        { }

        [[nodiscard]] bool IsValid() const;
        [[nodiscard]] const std::shared_ptr<animation::Sampler>& GetSampler() const { return m_Sampler; }
        [[nodiscard]] AnimationAttribute GetAttribute() const { return m_Attribute; }
        [[nodiscard]] std::shared_ptr<SceneGraphNode> GetTargetNode() const { return m_TargetNode.lock(); }
        [[nodiscard]] const std::string& GetLeafPropertyName() const { return m_LeafPropertyName; }
        void SetTargetNode(const std::shared_ptr<SceneGraphNode>& node) { m_TargetNode = node; }
        void SetLeafProperyName(const std::string& name) { m_LeafPropertyName = name; }
        bool Apply(float time) const;  // NOLINT(modernize-use-nodiscard)
    };

    class SceneGraphAnimation : public SceneGraphLeaf
    {
    private:
        std::vector<std::shared_ptr<SceneGraphAnimationChannel>> m_Channels;
        float m_Duration = 0.f;

    public:
        SceneGraphAnimation() = default;

        [[nodiscard]] std::shared_ptr<SceneGraphLeaf> Clone() override;
        [[nodiscard]] SceneContentFlags GetContentFlags() const override { return SceneContentFlags::Animations; }
        [[nodiscard]] const std::vector<std::shared_ptr<SceneGraphAnimationChannel>>& GetChannels() const { return m_Channels; }
        [[nodiscard]] float GetDuration() const { return m_Duration; }
        [[nodiscard]] bool IsVald() const;
        bool Apply(float time) const;  // NOLINT(modernize-use-nodiscard)
        void AddChannel(const std::shared_ptr<SceneGraphAnimationChannel>& channel);
    };

    // A container that tracks unique resources of the same type used by some entity, for example unique meshes used in a scene graph.
    // It works by putting the resource shared pointers into a map and associating a reference count with each resource.
    // When the resource is added and released an equal number of times, its refrence count reaches zero, and it's removed from the container.
    template<typename T>
    class ResourceTracker
    {
    private:
        std::unordered_map<std::shared_ptr<T>, uint32_t> m_Map;
        using UnderlyingConstIterator = typename std::unordered_map<std::shared_ptr<T>, uint32_t>::const_iterator;

    public:
        class ConstIterator
        {
        private:
            UnderlyingConstIterator m_Iter;
        public:
            ConstIterator(UnderlyingConstIterator iter) : m_Iter(std::move(iter)) {}
            ConstIterator& operator++() { ++m_Iter; return *this; }
            ConstIterator operator++(int) { ConstIterator res = *this; ++m_Iter; return res; }
            bool operator==(ConstIterator other) const { return m_Iter == other.m_Iter; }
            bool operator!=(ConstIterator other) const { return !(*this == other); }
            const std::shared_ptr<T>& operator*() { return m_Iter->first; }
        };

        // Adds a reference to the specified resource.
        // Returns true if this is the first reference, i.e. if the resource has just been added to the tracker.
        bool AddRef(const std::shared_ptr<T>& resource)
        {
            if (!resource) return false;
            uint32_t refCount = ++m_Map[resource];
            return (refCount == 1);
        }

        // Removes a reference from the specified resource.
        // Returns true if this was the last reference, i.e. if the resource has just been removed from the tracker.
        bool Release(const std::shared_ptr<T>& resource)
        {
            if (!resource) return false;
            auto it = m_Map.find(resource);
            if (it == m_Map.end())
            {
                assert(false); // trying to release an object not owned by this tracker
                return false;
            }

            if (it->second == 0)
                assert(false); // zero-reference entries should not be possible; might indicate concurrency issues
            else
                --it->second;

            if (it->second == 0)
            {
                m_Map.erase(it);
                return true;
            }
            return false;
        }

        [[nodiscard]] ConstIterator begin() const { return ConstIterator(m_Map.cbegin()); }
        [[nodiscard]] ConstIterator end() const { return ConstIterator(m_Map.cend()); }
        [[nodiscard]] bool empty() const { return m_Map.empty(); }
        [[nodiscard]] size_t size() const { return m_Map.size(); }
        [[nodiscard]] const std::shared_ptr<T>& operator[](size_t i) { return m_Map[i].first; }
    };

    template<typename T>
    using SceneResourceCallback = std::function<void(const std::shared_ptr<T>&)>;
    
    class SceneGraph : public std::enable_shared_from_this<SceneGraph>
    {
    private:
        friend class SceneGraphNode;
        std::shared_ptr<SceneGraphNode> m_Root;
        ResourceTracker<Material> m_Materials;
        ResourceTracker<MeshInfo> m_Meshes;
        size_t m_GeometryCount = 0;
        size_t m_GeometryInstancesCount = 0;
        std::vector<std::shared_ptr<MeshInstance>> m_MeshInstances;
        std::vector<std::shared_ptr<SkinnedMeshInstance>> m_SkinnedMeshInstances;
        std::vector<std::shared_ptr<SceneGraphAnimation>> m_Animations;
        std::vector<std::shared_ptr<SceneCamera>> m_Cameras;
        std::vector<std::shared_ptr<Light>> m_Lights;
        
    protected:
        virtual void RegisterLeaf(const std::shared_ptr<SceneGraphLeaf>& leaf);
        virtual void UnregisterLeaf(const std::shared_ptr<SceneGraphLeaf>& leaf);

    public:
        SceneGraph() = default;
        virtual ~SceneGraph() = default;

        SceneResourceCallback<MeshInfo> OnMeshAdded;
        SceneResourceCallback<MeshInfo> OnMeshRemoved;
        SceneResourceCallback<Material> OnMaterialAdded;
        SceneResourceCallback<Material> OnMaterialRemoved;

        [[nodiscard]] const std::shared_ptr<SceneGraphNode>& GetRootNode() const { return m_Root; }
        [[nodiscard]] const ResourceTracker<Material>& GetMaterials() const { return m_Materials; }
        [[nodiscard]] const ResourceTracker<MeshInfo>& GetMeshes() const { return m_Meshes; }
        [[nodiscard]] const size_t GetGeometryCount() const { return m_GeometryCount; }
        [[nodiscard]] const size_t GetGeometryInstancesCount() const { return m_GeometryInstancesCount; }
        [[nodiscard]] const std::vector<std::shared_ptr<MeshInstance>>& GetMeshInstances() const { return m_MeshInstances; }
        [[nodiscard]] const std::vector<std::shared_ptr<SkinnedMeshInstance>>& GetSkinnedMeshInstances() const { return m_SkinnedMeshInstances; }
        [[nodiscard]] const std::vector<std::shared_ptr<SceneGraphAnimation>>& GetAnimations() const { return m_Animations; }
        [[nodiscard]] const std::vector<std::shared_ptr<SceneCamera>>& GetCameras() const { return m_Cameras; }
        [[nodiscard]] const std::vector<std::shared_ptr<Light>>& GetLights() const { return m_Lights; }
        [[nodiscard]] bool HasPendingStructureChanges() const { return m_Root && (m_Root->m_Dirty & SceneGraphNode::DirtyFlags::SubgraphStructure) != 0; }
        [[nodiscard]] bool HasPendingTransformChanges() const { return m_Root && (m_Root->m_Dirty & (SceneGraphNode::DirtyFlags::SubgraphTransforms | SceneGraphNode::DirtyFlags::SubgraphPrevTransforms)) != 0; }

        std::shared_ptr<SceneGraphNode> SetRootNode(const std::shared_ptr<SceneGraphNode>& root);
        std::shared_ptr<SceneGraphNode> Attach(const std::shared_ptr<SceneGraphNode>& parent, const std::shared_ptr<SceneGraphNode>& child);
        std::shared_ptr<SceneGraphNode> AttachLeafNode(const std::shared_ptr<SceneGraphNode>& parent, const std::shared_ptr<SceneGraphLeaf>& leaf);
        std::shared_ptr<SceneGraphNode> Detach(const std::shared_ptr<SceneGraphNode>& node);

        [[nodiscard]] std::shared_ptr<SceneGraphNode> FindNode(const std::filesystem::path& path, SceneGraphNode* context = nullptr) const;
        
        void Refresh(uint32_t frameIndex);
    };

    struct SceneImportResult
    {
        std::shared_ptr<SceneGraphNode> rootNode;
    };

    class SceneTypeFactory
    {
    public:
        virtual ~SceneTypeFactory() = default;
        virtual std::shared_ptr<SceneGraphLeaf> CreateLeaf(const std::string& type);
        virtual std::shared_ptr<Material> CreateMaterial();
        virtual std::shared_ptr<MeshInfo> CreateMesh();
        virtual std::shared_ptr<MeshGeometry> CreateMeshGeometry();
        virtual std::shared_ptr<MeshInstance> CreateMeshInstance(const std::shared_ptr<MeshInfo>& mesh);
    };

    void PrintSceneGraph(const std::shared_ptr<SceneGraphNode>& root);
}
