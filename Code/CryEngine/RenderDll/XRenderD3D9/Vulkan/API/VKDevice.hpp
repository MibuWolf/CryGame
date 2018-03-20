// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include <vector>

#include "VKBase.hpp"
#include "VKHeap.hpp"
#include "VKBufferResource.hpp"
#include "VKImageResource.hpp"
#include "VKSampler.hpp"
#include "VKResourceView.hpp"

#include "VKCommandList.hpp"
#include "VKCommandScheduler.hpp"

#include "VKOcclusionQueryManager.hpp"

namespace NCryVulkan
{
#define CMDQUEUE_TYPE_GRAPHICS 0
#define CMDQUEUE_TYPE_COMPUTE 1
#define CMDQUEUE_TYPE_TRANSFER 2
#define CMDQUEUE_TYPE_PRESENT 3
#define CMDQUEUE_TYPE_COUNT 4

class CInstance;
struct SPhysicalDeviceInfo;

class CDevice : public CRefCounted
{

protected:
	struct SQueueConfiguration
	{
		uint32_t queueFamilyIndex  [CMDQUEUE_TYPE_COUNT];
		uint32_t queueIndexInFamily[CMDQUEUE_TYPE_COUNT];
	};

public:
	static _smart_ptr<CDevice> Create(const SPhysicalDeviceInfo* pDeviceInfo, VkAllocationCallbacks* hostAllocator, VkSurfaceKHR Surface, const std::vector<const char*>& layersToEnable, const std::vector<const char*>& extensionsToEnable);

	CDevice(const SPhysicalDeviceInfo* pDeviceInfo, VkAllocationCallbacks* hostAllocator, VkDevice Device, VkSurfaceKHR Surface);
	~CDevice();

	CCommandScheduler& GetScheduler() { return m_Scheduler; }
	const CCommandScheduler& GetScheduler() const { return m_Scheduler; }

	const VkDevice& GetVkDevice() const { return m_device; }
	const VkPipelineCache& GetVkPipelineCache() const { return m_pipelineCache; }

	const SPhysicalDeviceInfo* GetPhysicalDeviceInfo() const { return m_pDeviceInfo; }
	VkSurfaceKHR GetSurface() const { return m_surface; }

	VkDescriptorPool GetDescriptorPool() const { return m_descriptorPool; }
	CHeap& GetHeap() { return m_heap; }
	const CHeap& GetHeap() const { return m_heap; }

	COcclusionQueryManager& GetOcclusionQueries() { return m_occlusionQueries; }
	const COcclusionQueryManager& GetOcclusionQueries() const { return m_occlusionQueries; }

	// Vk spec requires "The following Vulkan objects must not be destroyed while any command buffers using the object are recording or pending execution"
	// - VkEvent
	// - VkQueryPool
	// - VkBufferView       done
	// - VkImageView        done
	// - VkPipeline
	// - VkSampler          done
	// - VkDescriptorPool
	// - VkFramebuffer      done
	// - VkRenderPass       done
	// - VkCommandPool
	// - VkDeviceMemory     done
	// - VkDescriptorSet
	void DeferDestruction(CSampler&& resource);
	void DeferDestruction(CBufferView&& view);
	void DeferDestruction(CImageView&& view);
	void DeferDestruction(VkRenderPass renderPass, VkFramebuffer frameBuffer);

	// Ticks the deferred destruction for the above types.
	// After kDeferTicks the deferred objects will be actually destroyed.
	void TickDestruction();

	template<class CResource> VkResult CommitResource(EHeapType HeapHint, CResource* pInputResource) threadsafe;
	template<class CResource, class VkCreateInfo> VkResult CreateCommittedResource(EHeapType HeapHint, const VkCreateInfo& createInfo, CResource** ppOutputResource) threadsafe;
	template<class CResource> VkResult DuplicateCommittedResource(CResource* pInputResource, CResource** ppOutputResource) threadsafe;
	template<class CResource> VkResult SubstituteUsedCommittedResource(const FVAL64 (&fenceValues)[CMDQUEUE_NUM], CResource** ppSubstituteResource) threadsafe;
	template<class CResource> VkResult CreateOrReuseStagingResource(CResource* pInputResource, VkDeviceSize minSize, CBufferResource** ppStagingResource, bool bUpload) threadsafe;
	template<class CResource, class VkCreateInfo> VkResult CreateOrReuseCommittedResource(EHeapType HeapHint, const VkCreateInfo& createInfo, CResource** ppOutputResource) threadsafe;
	template<class CResource> void ReleaseLater(const FVAL64 (&fenceValues)[CMDQUEUE_NUM], CResource* pObject, bool bReusable = true) threadsafe;
	template<class CResource> void FlushReleaseHeap(const UINT64 (&completedFenceValues)[CMDQUEUE_NUM], const UINT64 (&pruneFenceValues)[CMDQUEUE_NUM]) threadsafe;

	void FlushReleaseHeaps(const UINT64 (&completedFenceValues)[CMDQUEUE_NUM], const UINT64 (&pruneFenceValues)[CMDQUEUE_NUM]) threadsafe;
	void FlushAndWaitForGPU();

private:
	const SPhysicalDeviceInfo* m_pDeviceInfo;
	VkAllocationCallbacks m_Allocator;
	VkSurfaceKHR m_surface;
	VkDevice m_device;
	VkPipelineCache m_pipelineCache;
	VkDescriptorPool m_descriptorPool;
	CHeap m_heap;

	struct SRenderPass
	{
		VkDevice self;
		CAutoHandle<VkRenderPass>  renderPass;
		CAutoHandle<VkFramebuffer> frameBuffer;
		SRenderPass(SRenderPass&&) = default;
		~SRenderPass();
	};

	static const uint32_t        kDeferTicks = 2; // One for "currently recording", one for "currently executing on GPU", may need to be adjusted?
	CryCriticalSection           m_deferredLock;
	std::vector<CBufferView>     m_deferredBufferViews[kDeferTicks];
	std::vector<CImageView>      m_deferredImageViews[kDeferTicks];
	std::vector<CSampler>        m_deferredSamplers[kDeferTicks];
	std::vector<SRenderPass>     m_deferredRenderPasses[kDeferTicks];

	// Objects that should be released when they are not in use anymore
	static CryCriticalSectionNonRecursive m_ReleaseHeapTheadSafeScope[2];
	static CryCriticalSectionNonRecursive m_RecycleHeapTheadSafeScope[2];

	template<class CResource> CryCriticalSectionNonRecursive& GetReleaseHeapCriticalSection();
	template<class CResource> CryCriticalSectionNonRecursive& GetRecycleHeapCriticalSection();

	struct ReleaseInfo
	{
		THash      hHash;
		UINT32     bFlags;
		UINT64     fenceValues[CMDQUEUE_NUM];
	};

	template<class CResource>
	struct RecycleInfo
	{
		CResource* pObject;
		UINT64     fenceValues[CMDQUEUE_NUM];
	};

	template<class CResource> using TReleaseHeap = std::unordered_map<CResource*, ReleaseInfo>;
	template<class CResource> using TRecycleHeap = std::unordered_multimap<THash, RecycleInfo<CResource>>;

	TReleaseHeap<CBufferResource> m_BufferReleaseHeap;
	TReleaseHeap<CImageResource > m_ImageReleaseHeap;
	TRecycleHeap<CBufferResource> m_BufferRecycleHeap;
	TRecycleHeap<CImageResource > m_ImageRecycleHeap;

	template<class CResource> TReleaseHeap<CResource>& GetReleaseHeap();
	template<class CResource> TRecycleHeap<CResource>& GetRecycleHeap();

	CCommandScheduler      m_Scheduler;
	COcclusionQueryManager m_occlusionQueries;

};

}
