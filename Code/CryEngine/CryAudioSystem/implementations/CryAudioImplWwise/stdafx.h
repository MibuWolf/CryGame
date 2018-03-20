// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include <CryCore/Project/CryModuleDefs.h>
#define eCryModule eCryM_AudioImplementation
#include <CryCore/Platform/platform.h>
#include <CryCore/StlUtils.h>
#include <CryCore/Project/ProjectDefines.h>
#include <CryString/CryPath.h> // need to include before AK includes windows.h

#if CRY_PLATFORM_DURANGO
	#define PROVIDE_WWISE_IMPL_SECONDARY_POOL
// Memory Allocation
	#include <CryMemory/CryPool/PoolAlloc.h>
#endif

#if !defined(_RELEASE)
#define INCLUDE_WWISE_IMPL_PRODUCTION_CODE
#define ENABLE_AUDIO_LOGGING
#endif // _RELEASE

#include <AudioLogger.h>

#if CRY_PLATFORM_DURANGO
#define PROVIDE_WWISE_IMPL_SECONDARY_POOL
#endif

namespace CryAudio
{
namespace Impl
{
namespace Wwise
{
extern CLogger g_implLogger;

// Memory Allocation
#if defined(PROVIDE_WWISE_IMPL_SECONDARY_POOL)
typedef NCryPoolAlloc::CThreadSafe<NCryPoolAlloc::CBestFit<NCryPoolAlloc::CReferenced<NCryPoolAlloc::CMemoryDynamic, 4* 1024, true>, NCryPoolAlloc::CListItemReference>> MemoryPoolReferenced;

extern MemoryPoolReferenced g_audioImplMemoryPoolSecondary;

//////////////////////////////////////////////////////////////////////////
inline void* Secondary_Allocate(size_t const nSize)
{
	// Secondary Memory is Referenced. To not loose the handle, more memory is allocated
	// and at the beginning the handle is saved.

	/* Allocate in Referenced Secondary Pool */
	uint32 const allocHandle = g_audioImplMemoryPoolSecondary.Allocate<uint32>(nSize, MEMORY_ALLOCATION_ALIGNMENT);
	CRY_ASSERT(allocHandle > 0);
	void* pAlloc = NULL;

	if (allocHandle > 0)
	{
		pAlloc = g_audioImplMemoryPoolSecondary.Resolve<void*>(allocHandle);
	}

	return pAlloc;
}

//////////////////////////////////////////////////////////////////////////
inline bool Secondary_Free(void* pFree)
{
	// Secondary Memory is Referenced. To not loose the handle, more memory is allocated
	// and at the beginning the handle is saved.

	// retrieve handle
	bool bFreed = (pFree == NULL);      //true by default when passing NULL
	uint32 const allocHandle = g_audioImplMemoryPoolSecondary.AddressToHandle(pFree);

	if (allocHandle > 0)
	{
		bFreed = g_audioImplMemoryPoolSecondary.Free(allocHandle);
	}

	return bFreed;
}
#endif // PROVIDE_AUDIO_IMPL_SECONDARY_POOL
}      // Wwise
}      // Impl
}      // CryAudio
