// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  File name:   FrameProfileSystem.cpp
//  Version:     v1.00
//  Created:     24/6/2003 by Timur,Sergey,Wouter.
//  Compilers:   Visual Studio.NET
//  Description:
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "FrameProfileSystem.h"

#include <CrySystem/ILog.h>
#include <CryRenderer/IRenderer.h>
#include <CryInput/IInput.h>
#include <CryCore/StlUtils.h>
#include <CrySystem/IConsole.h>
#include <CryInput/IHardwareMouse.h>
#include <CryGame/IGameFramework.h>
#include <CrySystem/Profilers/IStatoscope.h>

#include "Sampler.h"
#include <CryThreading/IThreadManager.h>
#include "Timer.h"

#include <CryCore/Platform/CryWindows.h>

#if CRY_PLATFORM_WINDOWS
	#include <psapi.h>
static HMODULE hPsapiModule;
typedef BOOL (WINAPI * FUNC_GetProcessMemoryInfo)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
static FUNC_GetProcessMemoryInfo pfGetProcessMemoryInfo;
static bool m_bNoPsapiDll;
#endif

extern int CryMemoryGetAllocatedSize();

#ifdef USE_FRAME_PROFILER

	#define MAX_PEAK_PROFILERS         30
	#define MAX_ABSOLUTEPEAK_PROFILERS 100
//! Peak tolerance in milliseconds.
	#define PEAK_TOLERANCE             10.0f

//////////////////////////////////////////////////////////////////////////
// CFrameProfilerSystem static variable.
//////////////////////////////////////////////////////////////////////////
CFrameProfileSystem* CFrameProfileSystem::s_pFrameProfileSystem = nullptr;

CryCriticalSection CFrameProfileSystem::m_staticProfilersLock;

//////////////////////////////////////////////////////////////////////////
// FrameProfilerSystem Implementation.
//////////////////////////////////////////////////////////////////////////

int CFrameProfileSystem::profile_callstack = 0;
int CFrameProfileSystem::profile_log = 0;
threadID CFrameProfileSystem::s_nFilterThreadId = 0;

//////////////////////////////////////////////////////////////////////////
CFrameProfileSystem::CFrameProfileSystem()
	: m_nCurSample(-1)
	, m_pPrefix(nullptr)
	, m_bEnabled(false)
	, m_bCollectionPaused(false)
	, m_bCollect(false)
	, m_nThreadSupport(0)
	, m_bDisplay(false)
	, m_bNetworkProfiling(false)
	, m_bMemoryProfiling(true)
	, m_bDisplayMemoryInfo(false)
	, m_bLogMemoryInfo(false)
	, m_pRenderer(nullptr)
	, m_displayQuantity(SELF_TIME)
	, m_frameStartTime(0)
	, m_totalProfileTime(0)
	, m_frameTime(0)
	, m_callOverheadTime(0)
	, m_nCallOverheadTotal(0)
	, m_nCallOverheadRemainder(0)
	, m_nCallOverheadCalls(0)
	, m_frameSecAvg(0.0f)
	, m_frameLostSecAvg(0.0f)
	, m_frameOverheadSecAvg(0.0f)
	, m_ProfilerThreads(GetCurrentThreadId())
	, m_pCurrentCustomSection(nullptr)
	, m_peakTolerance(PEAK_TOLERANCE)
	, m_bDisplayedProfilersValid(false)
	, m_subsystemFilter(PROFILE_RENDERER)
	, m_bSubsystemFilterEnabled(false)
	, m_maxProfileCount(999)
	, m_peakDisplayDuration(8.0f)
	, m_bDrawGraph(false)
	#if defined(JOBMANAGER_SUPPORT_FRAMEPROFILER)
	, m_nWorkerGraphCurPos(0)
	#endif
	, m_timeGraphCurrentPos(0)
	, m_pGraphProfiler(nullptr)
	, m_bEnableHistograms(false)
	, m_histogramsCurrPos(0)
	, m_histogramsMaxPos(200)
	, m_histogramsHeight(16)
	, m_histogramScale(100.0f)
	, m_selectedRow(-1)
	, ROW_SIZE(0.0f)
	, COL_SIZE(0.0f)
	, m_baseY(0.0f)
	, m_offset(0.0f)
	, m_textModeBaseExtra(0)
	, m_nPagesFaultsLastFrame(0)
	, m_nPagesFaultsPerSec(0)
	, m_nLastPageFaultCount(0)
	, m_bPageFaultsGraph(false)
	, m_bRenderAdditionalSubsystems(false)
{
	s_pFrameProfileSystem = this;
	#if CRY_PLATFORM_WINDOWS
	hPsapiModule = nullptr;
	pfGetProcessMemoryInfo = nullptr;
	m_bNoPsapiDll = false;
	#endif

	s_nFilterThreadId = 0;

	// Allocate space for 1024 profilers.
	m_profilers.reserve(1024);

	//m_pProfilers = &m_netTrafficProfilers;
	m_pProfilers = &m_profilers;

	m_pSampler = new CSampler;

	//////////////////////////////////////////////////////////////////////////
	// Initialize subsystems list.
	memset(m_subsystems, 0, sizeof(m_subsystems));
	m_subsystems[PROFILE_RENDERER].name = "Renderer";
	m_subsystems[PROFILE_3DENGINE].name = "3DEngine";
	m_subsystems[PROFILE_PARTICLE].name = "Particle";
	m_subsystems[PROFILE_ANIMATION].name = "Animation";
	m_subsystems[PROFILE_AI].name = "AI";
	m_subsystems[PROFILE_ENTITY].name = "Entity";
	m_subsystems[PROFILE_PHYSICS].name = "Physics";
	m_subsystems[PROFILE_SCRIPT].name = "Script";
	m_subsystems[PROFILE_SCRIPT_CFUNC].name = "Script C funcs";
	m_subsystems[PROFILE_AUDIO].name = "Audio";
	m_subsystems[PROFILE_GAME].name = "Game";
	m_subsystems[PROFILE_ACTION].name = "Action";
	m_subsystems[PROFILE_EDITOR].name = "Editor";
	m_subsystems[PROFILE_NETWORK].name = "Network";
	m_subsystems[PROFILE_SYSTEM].name = "System";
	m_subsystems[PROFILE_INPUT].name = "Input";
	m_subsystems[PROFILE_MOVIE].name = "Movie";
	m_subsystems[PROFILE_FONT].name = "Font";
	m_subsystems[PROFILE_DEVICE].name = "Device";

	for (int i = 0; i < CRY_ARRAY_COUNT(m_subsystems); i++)
	{
		m_subsystems[i].budgetTime = 66.6f;
		m_subsystems[i].maxTime = 0.0f;
	}

	m_subsystems[PROFILE_AI].budgetTime = 5.0f;
	m_subsystems[PROFILE_ACTION].budgetTime = 3.0f;
	m_subsystems[PROFILE_GAME].budgetTime = 2.0f;

	#if defined(JOBMANAGER_SUPPORT_FRAMEPROFILER)
	m_ThreadFrameStats = new JobManager::CWorkerFrameStats(0);
	m_BlockingFrameStats = new JobManager::CWorkerFrameStats(0);
	#endif
};

//////////////////////////////////////////////////////////////////////////
CFrameProfileSystem::~CFrameProfileSystem()
{
	#if defined(JOBMANAGER_SUPPORT_FRAMEPROFILER)
	SAFE_DELETE(m_BlockingFrameStats);
	SAFE_DELETE(m_ThreadFrameStats);
	#endif

	delete m_pSampler;

	// Delete graphs for all frame profilers.
	#if CRY_PLATFORM_WINDOWS
	if (hPsapiModule)
		::FreeLibrary(hPsapiModule);
	hPsapiModule = nullptr;
	#endif
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::Init(int nThreadSupport)
{
	m_nThreadSupport = nThreadSupport;

	gEnv->callbackStartSection = &StartProfilerSection;
	gEnv->callbackEndSection = &EndProfilerSection;

	REGISTER_CVAR(profile_callstack, 0, 0, "Logs all Call Stacks of the selected profiler function for one frame");
	REGISTER_CVAR(profile_log, 0, 0, "Logs profiler output");
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::Done()
{
	for (auto const pFrameProfiler : * m_pProfilers)
	{
		delete pFrameProfiler->m_pGraph;
		pFrameProfiler->m_pGraph = nullptr;

		delete pFrameProfiler->m_pOfflineHistory;
		pFrameProfiler->m_pOfflineHistory = nullptr;
		
		pFrameProfiler->m_pISystem = nullptr;
	}

	for (auto const pFrameProfiler : m_netTrafficProfilers)
	{
		delete pFrameProfiler->m_pGraph;
		pFrameProfiler->m_pGraph = nullptr;

		delete pFrameProfiler->m_pOfflineHistory;
		pFrameProfiler->m_pOfflineHistory = nullptr;

		pFrameProfiler->m_pISystem = nullptr;
	}

	stl::free_container(m_profilers);
	stl::free_container(m_netTrafficProfilers);
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::SetProfiling(bool on, bool display, char* prefix, ISystem* pSystem)
{
	Enable(on, display);
	m_pPrefix = prefix;
	if (on && m_nCurSample < 0)
	{
		m_nCurSample = 0;
		CryLogAlways("Profiling data started (%s), prefix = \"%s\"", display ? "display only" : "tracing", prefix);

		m_frameTimeOfflineHistory.m_selfTime.reserve(1000);
		m_frameTimeOfflineHistory.m_count.reserve(1000);
	}
	else if (!on && m_nCurSample >= 0)
	{
		CryLogAlways("Profiling data finished");
		{
	#if CRY_PLATFORM_WINDOWS
			// find the "frameprofileXX" filename for the file
			char outfilename[32] = "frameprofile.dat";
			// while there is such file already
			for (int i = 0; (GetFileAttributes(outfilename) != INVALID_FILE_ATTRIBUTES) && i < 1000; ++i)
				cry_sprintf(outfilename, "frameprofile%02d.dat", i);

			FILE* f = fopen(outfilename, "wb");
			if (!f)
			{
				CryFatalError("Could not write profiling data to file!");
			}
			else
			{
				int i;
				// Find out how many profilers was active.
				int numProf = 0;

				for (i = 0; i < (int)m_pProfilers->size(); i++)
				{
					CFrameProfiler* pProfiler = (*m_pProfilers)[i];
					if (pProfiler && pProfiler->m_pOfflineHistory)
						numProf++;
				}

				fwrite("FPROFDAT", 8, 1, f);                // magic header, for what its worth
				int version = 2;                            // bump this if any of the format below changes
				fwrite(&version, sizeof(int), 1, f);

				int numSamples = m_nCurSample;
				fwrite(&numSamples, sizeof(int), 1, f); // number of samples per group (everything little endian)
				int mapsize = numProf + 1;              // Plus 1 global.
				fwrite(&mapsize, sizeof(int), 1, f);

				// Write global profiler.
				fwrite("__frametime", strlen("__frametime") + 1, 1, f);
				int len = (int)m_frameTimeOfflineHistory.m_selfTime.size();
				assert(len == numSamples);
				for (i = 0; i < len; i++)
				{
					fwrite(&m_frameTimeOfflineHistory.m_selfTime[i], 1, sizeof(int), f);
					fwrite(&m_frameTimeOfflineHistory.m_count[i], 1, sizeof(short), f);
				}
				;

				// Write other profilers.
				for (i = 0; i < (int)m_pProfilers->size(); i++)
				{
					CFrameProfiler* pProfiler = (*m_pProfilers)[i];
					if (!pProfiler || !pProfiler->m_pOfflineHistory)
						continue;

					const char* name = GetFullName(pProfiler);
					//int slen = strlen(name)+1;
					fwrite(name, strlen(name) + 1, 1, f);

					len = (int)pProfiler->m_pOfflineHistory->m_selfTime.size();
					assert(len == numSamples);
					for (int i = 0; i < len; i++)
					{
						fwrite(&pProfiler->m_pOfflineHistory->m_selfTime[i], 1, sizeof(int), f);
						fwrite(&pProfiler->m_pOfflineHistory->m_count[i], 1, sizeof(short), f);
					}
					;

					// Delete offline data, from profiler.
					SAFE_DELETE(pProfiler->m_pOfflineHistory);
				}
				;
				fclose(f);
				CryLogAlways("Profiling data saved to file '%s'", outfilename);
			};
	#endif
		};
		m_frameTimeOfflineHistory.m_selfTime.clear();
		m_frameTimeOfflineHistory.m_count.clear();
		m_nCurSample = -1;
	}
	;
};

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::Enable(bool bCollect, bool bDisplay)
{
	if (m_bEnabled != bCollect)
	{
	#if CRY_PLATFORM_WINDOWS
		if (bCollect)
		{
		}
		else
		{
			if (m_bCollectionPaused)
			{
				if (gEnv->pHardwareMouse)
				{
					gEnv->pHardwareMouse->DecrementCounter();
				}
			}
		}
	#endif

		Reset();
	}

	m_bEnabled = bCollect;
	m_bDisplay = bDisplay;
	m_bDisplayedProfilersValid = false;

	// add ourselves to the input system
	UpdateInputSystemStatus();
}

void CFrameProfileSystem::EnableHistograms(bool bEnableHistograms)
{
	m_bEnableHistograms = bEnableHistograms;
	m_bDisplayedProfilersValid = false;
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::StartSampling(int nMaxSamples)
{
	if (m_pSampler)
	{
		m_pSampler->SetMaxSamples(nMaxSamples);
		m_pSampler->Start();
	}
}

//////////////////////////////////////////////////////////////////////////
CFrameProfiler* CFrameProfileSystem::GetProfiler(int index) const
{
	assert(index >= 0 && index < (int)m_profilers.size());
	return m_profilers[index];
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::Reset()
{
	//gEnv->callbackStartSection = &StartProfilerSection;
	//gEnv->callbackEndSection = &EndProfilerSection;

	m_absolutepeaks.clear();
	m_ProfilerThreads.Reset();
	m_pCurrentCustomSection = 0;
	m_totalProfileTime = 0;
	m_frameStartTime = 0;
	m_frameTime = 0;
	m_callOverheadTime = 0;
	m_nCallOverheadTotal = 0;
	m_nCallOverheadRemainder = 0;
	m_nCallOverheadCalls = 0;
	m_frameSecAvg = 0.f;
	m_frameLostSecAvg = 0.f;
	m_frameOverheadSecAvg = 0.f;
	m_bCollectionPaused = false;

	// Iterate over all profilers update their history and reset them.
	for (auto const pFrameProfiler : m_profilers)
	{
		if (pFrameProfiler != nullptr)
		{
			pFrameProfiler->m_totalTimeHistory.Clear();
			pFrameProfiler->m_selfTimeHistory.Clear();
			pFrameProfiler->m_countHistory.Clear();
			pFrameProfiler->m_totalTime = 0;
			pFrameProfiler->m_selfTime = 0;
			pFrameProfiler->m_count = 0;
			pFrameProfiler->m_displayedValue = 0;
			pFrameProfiler->m_variance = 0;
			pFrameProfiler->m_peak = 0;
		}
	}

	// Iterate over all profilers update their history and reset them.
	for (auto const pFrameProfiler : m_netTrafficProfilers)
	{
		if (pFrameProfiler != nullptr)
		{
			// Reset profiler.
			pFrameProfiler->m_totalTimeHistory.Clear();
			pFrameProfiler->m_selfTimeHistory.Clear();
			pFrameProfiler->m_countHistory.Clear();
			pFrameProfiler->m_totalTime = 0;
			pFrameProfiler->m_selfTime = 0;
			pFrameProfiler->m_count = 0;
			pFrameProfiler->m_peak = 0;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::AddFrameProfiler(CFrameProfiler* pProfiler)
{
	CryAutoCriticalSection lock(m_profilersLock);

	assert(pProfiler);
	if (!pProfiler)
		return;

	// limit color lookup to profile type specific bits
	pProfiler->m_colorIdentifier = (int)pProfiler->m_description & PROFILE_TYPE_CHECK_MASK;

	#if ALLOW_BROFILER
	pProfiler->m_brofilerEventDescription = std::shared_ptr<::Profiler::EventDescription>(::Profiler::EventDescription::Create(
		pProfiler->m_name, pProfiler->m_fileName, pProfiler->m_fileLine,
		profile_colors[pProfiler->m_colorIdentifier].pack_argb8888()),
		[](::Profiler::EventDescription* pDesc) { ::Profiler::EventDescription::DestroyEventDescription(pDesc); });
	#endif

	// Set default thread id.
	pProfiler->m_threadId = GetMainThreadId();
	if ((EProfiledSubsystem)pProfiler->m_subsystem == PROFILE_NETWORK_TRAFFIC)
	{
		m_netTrafficProfilers.push_back(pProfiler);
	}
	else
	{
		m_profilers.push_back(pProfiler);
	}

	pProfiler->m_bInitialized = true;
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::RemoveFrameProfiler(CFrameProfiler* pProfiler)
{
	CryAutoCriticalSection lock(m_profilersLock);

	assert(pProfiler);
	if (!pProfiler)
		return;

	#if ALLOW_BROFILER
	pProfiler->m_brofilerEventDescription = nullptr; // shared_ptr, the description might be destroyed after this
	#endif

	SAFE_DELETE(pProfiler->m_pGraph);
	SAFE_DELETE(pProfiler->m_pOfflineHistory);

	// When this static object gets destroyed its dtor checks against m_pISystem.
	// We need to make sure to nullptr it here so we do not access a dangling pointer during shutdown!
	pProfiler->m_pISystem = nullptr;

	if ((EProfiledSubsystem)pProfiler->m_subsystem == PROFILE_NETWORK_TRAFFIC)
	{
		stl::find_and_erase(m_netTrafficProfilers, pProfiler);
	}
	else
	{
		stl::find_and_erase(m_profilers, pProfiler);
	}
}

static int nMAX_THREADED_PROFILERS = 256;

void CFrameProfileSystem::SProfilerThreads::Reset()
{
	m_aThreadStacks[0].pProfilerSection = 0;
	if (!m_pReservedProfilers)
	{
		// Allocate reserved profilers;
		for (int i = 0; i < nMAX_THREADED_PROFILERS; i++)
		{
			CFrameProfiler* pProf = new CFrameProfiler(PROFILE_ANY, EProfileDescription::UNDEFINED, __FUNC__, __FUNC__, __LINE__);
			pProf->m_pNextThread = m_pReservedProfilers;
			m_pReservedProfilers = pProf;
		}
	}
}

CFrameProfiler* CFrameProfileSystem::SProfilerThreads::NewThreadProfiler(CFrameProfiler* pMainProfiler, TThreadId nThreadId)
{
	if (!pMainProfiler->m_bInitialized)
	{
		// main thread has not yet initialized this frame profiler
		return nullptr;
	}

	// Create new profiler for thread.
	CryWriteLock(&m_lock);
	if (!m_pReservedProfilers)
	{
		CryReleaseWriteLock(&m_lock);
		return nullptr;
	}
	CFrameProfiler* pProfiler = m_pReservedProfilers;
	m_pReservedProfilers = pProfiler->m_pNextThread;

	// Init.
	memset(pProfiler, 0, sizeof(*pProfiler));
	pProfiler->m_name = pMainProfiler->m_name;
	pProfiler->m_fileName = pMainProfiler->m_fileName;
	pProfiler->m_fileLine = pMainProfiler->m_fileLine;
	pProfiler->m_subsystem = pMainProfiler->m_subsystem;
	pProfiler->m_pISystem = pMainProfiler->m_pISystem;
	pProfiler->m_threadId = nThreadId;
	pProfiler->m_description = pMainProfiler->m_description;
	pProfiler->m_colorIdentifier = pMainProfiler->m_colorIdentifier;

	#if ALLOW_BROFILER
	// Brofiler is checking the current threadId with every update call - no need to create separate EventDescription per thread
	pProfiler->m_brofilerEventDescription = pMainProfiler->m_brofilerEventDescription;
	#endif

	// Insert in frame profiler list.
	pProfiler->m_pNextThread = pMainProfiler->m_pNextThread;
	pMainProfiler->m_pNextThread = pProfiler;

	CryReleaseWriteLock(&m_lock);
	return pProfiler;
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::StartProfilerSection(CFrameProfilerSection* pSection)
{
	//if (CFrameProfileSystem::s_pFrameProfileSystem->m_bCollectionPaused)
	//	return;

	// Find thread instance to collect profiles in.
	TThreadId nThreadId = GetCurrentThreadId();
	if (nThreadId != s_pFrameProfileSystem->GetMainThreadId())
	{
		if (!s_pFrameProfileSystem->m_nThreadSupport)
		{
			pSection->m_pFrameProfiler = nullptr;
			return;
		}

		pSection->m_pFrameProfiler = s_pFrameProfileSystem->m_ProfilerThreads.GetThreadProfiler(pSection->m_pFrameProfiler, nThreadId);
		if (!pSection->m_pFrameProfiler)
			return;
	}

	#if ALLOW_BROFILER
	// Brofiler
	if (::Profiler::IsActive())
	{
		pSection->m_brofilerEventData = ::Profiler::Event::Start(*(pSection->m_pFrameProfiler->m_brofilerEventDescription));
	}
	#endif

	// Platform profiling
	::CryProfile::detail::PushProfilingMarker(pSection->m_pFrameProfiler->m_colorIdentifier, pSection->m_pFrameProfiler->m_name);

	// Push section on stack for current thread.
	s_pFrameProfileSystem->m_ProfilerThreads.PushSection(pSection, nThreadId);
	pSection->m_excludeTime = 0;
	pSection->m_startTime = CryGetTicks();
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::EndProfilerSection(CFrameProfilerSection* pSection)
{
	AccumulateProfilerSection(pSection);

	// Not in a SLICE_AND_SLEEP here, account for call overhead.
	if (pSection->m_pParent)
	{
		pSection->m_pParent->m_excludeTime += s_pFrameProfileSystem->m_callOverheadTime;
	}

	#if ALLOW_BROFILER
	// Brofiler
	if (pSection->m_brofilerEventData)
	{
		::Profiler::Event::Stop(*(pSection->m_brofilerEventData));
	}
	#endif

	// Platform profiling
	CryProfile::detail::PopProfilingMarker();

	s_pFrameProfileSystem->m_ProfilerThreads.PopSection(pSection, pSection->m_pFrameProfiler->m_threadId);
}

void CFrameProfileSystem::AccumulateProfilerSection(CFrameProfilerSection* pSection)
{
	//if (CFrameProfileSystem::s_pFrameProfileSystem->m_bCollectionPaused)
	//	return;

	int64 endTime = CryGetTicks();

	CFrameProfiler* pProfiler = pSection->m_pFrameProfiler;
	if (!pProfiler)
		return;

	assert(GetCurrentThreadId() == pProfiler->m_threadId);

	int64 totalTime = endTime - pSection->m_startTime;
	int64 selfTime = totalTime - pSection->m_excludeTime;
	if (totalTime < 0)
		totalTime = 0;
	if (selfTime < 0)
		selfTime = 0;

	if (s_nFilterThreadId && GetCurrentThreadId() != s_nFilterThreadId)
	{
		selfTime = 0;
		totalTime = 0;
		pProfiler->m_count = 0;
	}

	pProfiler->m_count++;
	pProfiler->m_selfTime += selfTime;
	pProfiler->m_totalTime += totalTime;
	pProfiler->m_peak = max(pProfiler->m_peak, selfTime);

	if (pSection->m_pParent)
	{
		// If we have parent, add this counter total time (but not call overhead time, because
		// if this is a SLICE_AND_SLEEP instead of a real end of frame, we might not have entered
		// the section this frame and we certainly haven't left it) to parent exclude time.
		pSection->m_pParent->m_excludeTime += totalTime;
		if (!pProfiler->m_pParent && pSection->m_pParent->m_pFrameProfiler)
		{
			pSection->m_pParent->m_pFrameProfiler->m_bHaveChildren = 1;
			pProfiler->m_pParent = pSection->m_pParent->m_pFrameProfiler;
		}
	}
	else
		pProfiler->m_pParent = 0;

	if (profile_callstack)
	{
		if (pProfiler == s_pFrameProfileSystem->m_pGraphProfiler)
		{
			float fMillis = 1000.f * gEnv->pTimer->TicksToSeconds(totalTime);
			CryLogAlways("Function Profiler: %s  (time=%.2fms)", s_pFrameProfileSystem->GetFullName(pSection->m_pFrameProfiler), fMillis);
			GetISystem()->debug_LogCallStack();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::StartMemoryProfilerSection(CFrameProfilerSection* pSection)
{
	CryAutoCriticalSection lock(m_staticProfilersLock);

	// Find thread instance to collect profiles in.
	TThreadId nThreadId = GetCurrentThreadId();
	pSection->m_pFrameProfiler = s_pFrameProfileSystem->m_ProfilerThreads.GetThreadProfiler(pSection->m_pFrameProfiler, nThreadId);
	if (pSection->m_pFrameProfiler)
	{
		// Push section on stack for current thread.
		s_pFrameProfileSystem->m_ProfilerThreads.PushSection(pSection, nThreadId);
		pSection->m_excludeTime = 0;
		pSection->m_startTime = CryMemoryGetAllocatedSize();
	}
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::EndMemoryProfilerSection(CFrameProfilerSection* pSection)
{
	CryAutoCriticalSection lock(m_staticProfilersLock);

	int64 endTime = CryMemoryGetAllocatedSize();
	TThreadId nThreadId = GetCurrentThreadId();
	CFrameProfiler* pProfiler = pSection->m_pFrameProfiler;
	if (!pProfiler)
		return;

	assert(nThreadId == pProfiler->m_threadId);

	int64 totalTime = endTime - pSection->m_startTime;
	int64 selfTime = totalTime - pSection->m_excludeTime;
	if (totalTime < 0)
		totalTime = 0;
	if (selfTime < 0)
		selfTime = 0;

	if (s_nFilterThreadId && nThreadId != s_nFilterThreadId)
	{
		selfTime = 0;
		totalTime = 0;
		pProfiler->m_count = 0;
	}

	// Ignore allocation functions.
	if (0 == _stricmp(pProfiler->m_name, "CryMalloc") ||
	    0 == _stricmp(pProfiler->m_name, "CryRealloc") ||
	    0 == _stricmp(pProfiler->m_name, "CryFree"))
	{
		selfTime = 0;
		totalTime = 0;
	}

	pProfiler->m_count++;
	pProfiler->m_selfTime += selfTime;
	pProfiler->m_totalTime += totalTime;
	pProfiler->m_peak = max(pProfiler->m_peak, selfTime);

	s_pFrameProfileSystem->m_ProfilerThreads.PopSection(pSection, pProfiler->m_threadId);
	if (pSection->m_pParent)
	{
		// If we have parent, add this counter total time to parent exclude time.
		pSection->m_pParent->m_excludeTime += totalTime;
		if (!pProfiler->m_pParent && pSection->m_pParent->m_pFrameProfiler)
		{
			pSection->m_pParent->m_pFrameProfiler->m_bHaveChildren = 1;
			pProfiler->m_pParent = pSection->m_pParent->m_pFrameProfiler;
		}
	}
	else
		pProfiler->m_pParent = 0;
}

//////////////////////////////////////////////////////////////////////////
CFrameProfilerSection const* CFrameProfileSystem::GetCurrentProfilerSection()
{
	// Return current (main-thread) profile section.
	return m_ProfilerThreads.GetMainSection();
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::StartCustomSection(CCustomProfilerSection* pSection)
{
	if (!m_bNetworkProfiling)
		return;

	pSection->m_excludeValue = 0;
	pSection->m_pParent = m_pCurrentCustomSection;
	m_pCurrentCustomSection = pSection;
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::EndCustomSection(CCustomProfilerSection* pSection)
{
	if (!m_bNetworkProfiling || m_bCollectionPaused)
		return;

	int total = *pSection->m_pValue;
	int self = total - pSection->m_excludeValue;

	CFrameProfiler* pProfiler = pSection->m_pFrameProfiler;
	pProfiler->m_count++;
	pProfiler->m_selfTime += self;
	pProfiler->m_totalTime += total;
	pProfiler->m_peak = max(pProfiler->m_peak, (int64)self);

	m_pCurrentCustomSection = pSection->m_pParent;
	if (m_pCurrentCustomSection)
	{
		// If we have parent, add this counter total time to parent exclude time.
		m_pCurrentCustomSection->m_pFrameProfiler->m_bHaveChildren = 1;
		m_pCurrentCustomSection->m_excludeValue += total;
		pProfiler->m_pParent = m_pCurrentCustomSection->m_pFrameProfiler;
	}
	else
		pProfiler->m_pParent = 0;
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::StartFrame()
{
	SetThreadSupport(gEnv->pConsole->GetCVar("profile_allthreads")->GetIVal());
	m_ProfilerThreads.Reset();

	m_bCollect = m_bEnabled && !m_bCollectionPaused;

	if (m_bCollect)
	{
		m_pCurrentCustomSection = 0;
		m_frameStartTime = CryGetTicks();

		// Accumulate overhead measurement, in running average
		static const int nPROFILE_CALLS_PER_FRAME = 16;
		static CFrameProfiler staticFrameProfiler(PROFILE_SYSTEM, EProfileDescription::UNDEFINED, "CallOverhead", __FUNC__, __LINE__);

		int64 nTicks = CryGetTicks();
		for (int n = 0; n < nPROFILE_CALLS_PER_FRAME; n++)
		{
			CFrameProfilerSection frameProfilerSection(&staticFrameProfiler, "", 0, EProfileDescription::FUNCTIONENTRY);
		}
		nTicks = CryGetTicks() - nTicks;

		m_nCallOverheadTotal += nTicks;
		m_nCallOverheadCalls += nPROFILE_CALLS_PER_FRAME;

		if (m_nCallOverheadCalls > 1024)
		{
			m_nCallOverheadTotal = m_nCallOverheadTotal * 1024 / m_nCallOverheadCalls;
			m_nCallOverheadCalls = 1024;
		}

		// Compute per-call overhead, dithering error into remainder for accurate average over time.
		m_callOverheadTime = (m_nCallOverheadTotal + m_nCallOverheadRemainder + m_nCallOverheadCalls / 2) / m_nCallOverheadCalls;
		m_nCallOverheadRemainder += m_nCallOverheadTotal - m_callOverheadTime * m_nCallOverheadCalls;
	}
}

//////////////////////////////////////////////////////////////////////////
float CFrameProfileSystem::TranslateToDisplayValue(int64 val)
{
	if (!m_bNetworkProfiling && !m_bMemoryProfiling)
		return gEnv->pTimer->TicksToSeconds(val) * 1000;
	else if (m_displayQuantity == ALLOCATED_MEMORY)
		return (float)(val >> 10); // In Kilobytes
	else if (m_displayQuantity == ALLOCATED_MEMORY_BYTES)
		return (float)val; // In bytes
	else if (m_bNetworkProfiling)
		return (float)val;
	return (float)val;
}

const char* CFrameProfileSystem::GetFullName(CFrameProfiler* pProfiler)
{
	static char sNameBuffer[256];
	cry_sprintf(sNameBuffer, pProfiler->m_name);

	#if CRY_FUNC_HAS_SIGNATURE // __FUNCTION__ only contains classname on MSVC, for other function we have __PRETTY_FUNCTION__, so we need to strip return / argument types
	{
		char* pEnd = (char*)strchr(sNameBuffer, '(');
		if (pEnd)
		{
			*pEnd = 0;
			while (*(pEnd) != ' ' && *(pEnd) != '*' && pEnd != (sNameBuffer - 1))
			{
				--pEnd;
			}
			memmove(sNameBuffer, pEnd + 1, &sNameBuffer[sizeof(sNameBuffer)] - (pEnd + 1));
		}
	}
	#endif

	if (pProfiler->m_threadId == GetMainThreadId())
		return sNameBuffer;

	// Add thread name.
	static char sFullName[256];
	if (!pProfiler->m_pNextThread || m_nThreadSupport > 1)
	{
		const char* sThreadName = gEnv->pThreadManager->GetThreadName(pProfiler->m_threadId);
		if (sThreadName)
		{
			cry_sprintf(sFullName, "%s @%s", sNameBuffer, sThreadName);
		}
		else
		{
			cry_sprintf(sFullName, "%s @%" PRI_THREADID, sNameBuffer, pProfiler->m_threadId);
		}
	}
	else
	{
		int nThreads = 1;
		while (pProfiler->m_pNextThread)
		{
			pProfiler = pProfiler->m_pNextThread;
			nThreads++;
		}
		cry_sprintf(sFullName, "%s @(%d threads)", sNameBuffer, nThreads);
	}

	sFullName[sizeof(sFullName) - 1] = 0;
	return sFullName;
}

//////////////////////////////////////////////////////////////////////////
const char* CFrameProfileSystem::GetModuleName(CFrameProfiler* pProfiler)
{
	return m_subsystems[pProfiler->m_subsystem].name;
}

const char* CFrameProfileSystem::GetModuleName(int num) const
{
	assert(num >= 0 && num < PROFILE_LAST_SUBSYSTEM);
	if (num < GetModuleCount())
		return m_subsystems[num].name;
	else
		return 0;
}

const int CFrameProfileSystem::GetModuleCount(void) const
{
	return (int)PROFILE_LAST_SUBSYSTEM;
}

const float CFrameProfileSystem::GetOverBudgetRatio(int modulenumber) const
{
	assert(modulenumber >= 0 && modulenumber < PROFILE_LAST_SUBSYSTEM);
	if (modulenumber < GetModuleCount() && m_subsystems[modulenumber].totalAnalized)
		return (float)m_subsystems[modulenumber].totalOverBudget / (float)m_subsystems[modulenumber].totalAnalized;
	else
		return 0.0f;
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::EndFrame()
{
	if (m_pSampler)
		m_pSampler->Update();

	if (!m_bEnabled)
		m_totalProfileTime = 0;

	TThreadId mainThreadId = GetMainThreadId();
	TThreadId renderThreadId = 0;
	if (gEnv->pRenderer)
	{
		TThreadId id;
		gEnv->pRenderer->GetThreadIDs(id, renderThreadId);
	}

	if (!m_bEnabled && !m_bNetworkProfiling)
	{
		static ICVar* pDisplayInfo = nullptr;

		if (pDisplayInfo == nullptr)
		{
			pDisplayInfo = gEnv->pConsole->GetCVar("r_DisplayInfo");
			assert(pDisplayInfo != nullptr);
		}

		// We want single subsystems to be able to still sample profile data even if the Profiler is disabled.
		// This is needed for r_DisplayInfo 2 for instance as it displays subsystems' performance data.
		// Note: This is currently meant to be as light weight as possible and to not run the entire Profiler.
		// Therefore we're ignoring overhead accumulated in StartFrame for instance.
		// Feel free to add that if it proves necessary.
		if (pDisplayInfo != nullptr && pDisplayInfo->GetIVal() > 1)
		{
			// Update profilers that are Regions only (without waiting time).
			auto Iter = m_pProfilers->cbegin();
			const auto IterEnd = m_pProfilers->cend();

			for (auto const pFrameProfiler : * m_pProfilers)
			{
				if (pFrameProfiler != nullptr && (pFrameProfiler->m_description & PROFILE_TYPE_CHECK_MASK) == EProfileDescription::REGION)
				{
					float const fSelfTime = TranslateToDisplayValue(pFrameProfiler->m_selfTime);
					pFrameProfiler->m_selfTimeHistory.Add(fSelfTime);

					// Reset profiler.
					pFrameProfiler->m_totalTime = 0;
					pFrameProfiler->m_selfTime = 0;
					pFrameProfiler->m_count = 0;
				}
			}
		}

		return;
	}

	#if CRY_PLATFORM_WINDOWS

	bool bPaused = (GetKeyState(VK_SCROLL) & 1);

	// Will pause or resume collection.
	if (bPaused != m_bCollectionPaused)
	{
		if (bPaused)
		{
			// Must be paused.
			if (gEnv->pHardwareMouse)
			{
				gEnv->pHardwareMouse->IncrementCounter();
			}
		}
		else
		{
			// Must be resumed.
			if (gEnv->pHardwareMouse)
			{
				gEnv->pHardwareMouse->DecrementCounter();
			}
		}
	}
	if (m_bCollectionPaused != bPaused)
	{
		m_bDisplayedProfilersValid = false;
	}
	m_bCollectionPaused = bPaused;

	#endif

	if (m_bCollectionPaused || (!m_bCollect && !m_bNetworkProfiling))
		return;

	FUNCTION_PROFILER(GetISystem(), PROFILE_SYSTEM);

	float smoothTime = gEnv->pTimer->TicksToSeconds(m_totalProfileTime);
	float smoothFactor = 1.f - gEnv->pTimer->GetProfileFrameBlending(&smoothTime);

	int64 endTime = CryGetTicks();
	m_frameTime = endTime - m_frameStartTime;
	m_totalProfileTime += m_frameTime;

	if (m_bSubsystemFilterEnabled)
	{
		auto Iter = m_pProfilers->cbegin();
		const auto IterEnd = m_pProfilers->cend();

		for (; Iter != IterEnd; ++Iter)
		{
			CFrameProfiler* const pProfiler = (*Iter);

			if (pProfiler != nullptr && pProfiler->m_subsystem != (uint8)m_subsystemFilter)
			{
				pProfiler->m_totalTime = 0;
				pProfiler->m_selfTime = 0;
				pProfiler->m_count = 0;
				pProfiler->m_variance = 0;
				pProfiler->m_peak = 0;
			}
		}
	}

	if (gEnv->pStatoscope)
		gEnv->pStatoscope->SetCurrentProfilerRecords(m_pProfilers);

	//////////////////////////////////////////////////////////////////////////
	// Lets see how many page faults we got.
	//////////////////////////////////////////////////////////////////////////
	#if CRY_PLATFORM_WINDOWS

	// PSAPI is not supported on window9x
	// so, don't use it
	if (!m_bNoPsapiDll)
	{
		// Load psapi dll.
		if (!pfGetProcessMemoryInfo)
		{
			hPsapiModule = ::LoadLibraryA("psapi.dll");
			if (hPsapiModule)
			{
				pfGetProcessMemoryInfo = (FUNC_GetProcessMemoryInfo)(::GetProcAddress(hPsapiModule, "GetProcessMemoryInfo"));
			}
			else
				m_bNoPsapiDll = true;
		}
		if (pfGetProcessMemoryInfo)
		{
			PROCESS_MEMORY_COUNTERS pc;
			pfGetProcessMemoryInfo(GetCurrentProcess(), &pc, sizeof(pc));
			m_nPagesFaultsLastFrame = (int)(pc.PageFaultCount - m_nLastPageFaultCount);
			m_nLastPageFaultCount = pc.PageFaultCount;
			static float fLastPFTime = 0;
			static int nPFCounter = 0;
			nPFCounter += m_nPagesFaultsLastFrame;
			float fCurr = gEnv->pTimer->TicksToSeconds(endTime);
			if ((fCurr - fLastPFTime) >= 1.f)
			{
				fLastPFTime = fCurr;
				m_nPagesFaultsPerSec = nPFCounter;
				nPFCounter = 0;
			}
		}
	}
	#endif
	//////////////////////////////////////////////////////////////////////////

	static ICVar* pVarMode = GetISystem()->GetIConsole()->GetCVar("profile_weighting");
	int nWeightMode = pVarMode ? pVarMode->GetIVal() : 0;

	int64 selfAccountedTime = 0;

	float fPeakTolerance = m_peakTolerance;
	if (m_bMemoryProfiling)
	{
		fPeakTolerance = 0;
	}

	// Iterate over all profilers update their history and reset them.
	int numProfileCalls = 0;

	for (auto const pFrameProfiler : * m_pProfilers)
	{
		// Skip this profiler if its filtered out.
		if (pFrameProfiler != nullptr && (!m_bSubsystemFilterEnabled || (m_bSubsystemFilterEnabled && pFrameProfiler->m_subsystem == (uint8)m_subsystemFilter)))
		{
			//filter stall profilers
			if ((int)m_displayQuantity == STALL_TIME)
				continue;

			if (pFrameProfiler->m_threadId == mainThreadId)
			{
				selfAccountedTime += pFrameProfiler->m_selfTime;
				numProfileCalls += pFrameProfiler->m_count;
			}

			float aveValue;
			float currentValue;

			if (m_nThreadSupport == 1 && pFrameProfiler->m_threadId == mainThreadId)
			{
				// Combine all non-main thread stats into 1
				if (CFrameProfiler* pThread = pFrameProfiler->m_pNextThread)
				{
					for (CFrameProfiler* pOtherThread = pThread->m_pNextThread; pOtherThread; pOtherThread = pOtherThread->m_pNextThread)
					{
						pThread->m_count += pOtherThread->m_count;
						pOtherThread->m_count = 0;
						pThread->m_selfTime += pOtherThread->m_selfTime;
						pOtherThread->m_selfTime = 0;
						pThread->m_totalTime += pOtherThread->m_totalTime;
						pOtherThread->m_totalTime = 0;
						pOtherThread->m_displayedValue = 0;
					}
				}
			}

			float fDisplay_TotalTime = TranslateToDisplayValue(pFrameProfiler->m_totalTime);
			float fDisplay_SelfTime = TranslateToDisplayValue(pFrameProfiler->m_selfTime);

			bool bEnablePeaks = nWeightMode < 3;

			// Only visually display render & main thread wait times
			// Wait times for other threads are still visible in Statoscope
			if (m_displayQuantity == SELF_TIME && pFrameProfiler->m_description & EProfileDescription::WAITING && (pFrameProfiler->m_threadId != renderThreadId && pFrameProfiler->m_threadId != mainThreadId))
			{
				// Reset profiler
				pFrameProfiler->m_totalTime = 0;
				pFrameProfiler->m_selfTime = 0;
				pFrameProfiler->m_peak = 0;
				pFrameProfiler->m_count = 0;
				continue;
			}

			switch ((int)m_displayQuantity)
			{
			case SELF_TIME:
			case PEAK_TIME:
			case COUNT_INFO:
			case STALL_TIME:
				currentValue = fDisplay_SelfTime;
				aveValue = pFrameProfiler->m_selfTimeHistory.GetAverage();
				break;
			case TOTAL_TIME:
				currentValue = fDisplay_TotalTime;
				aveValue = pFrameProfiler->m_totalTimeHistory.GetAverage();
				break;
			case SELF_TIME_EXTENDED:
				currentValue = fDisplay_SelfTime;
				aveValue = pFrameProfiler->m_selfTimeHistory.GetAverage();
				bEnablePeaks = false;
				break;
			case TOTAL_TIME_EXTENDED:
				currentValue = fDisplay_TotalTime;
				aveValue = pFrameProfiler->m_totalTimeHistory.GetAverage();
				bEnablePeaks = false;
				break;
			case SUBSYSTEM_INFO:
				currentValue = (float)pFrameProfiler->m_count;
				aveValue = pFrameProfiler->m_selfTimeHistory.GetAverage();
				if (pFrameProfiler->m_subsystem < PROFILE_LAST_SUBSYSTEM)
				{
					if (pFrameProfiler->m_description & EProfileDescription::WAITING)
					{
						m_subsystems[pFrameProfiler->m_subsystem].waitTime += aveValue;
					}
					else
					{
						m_subsystems[pFrameProfiler->m_subsystem].selfTime += aveValue;
					}
				}
				break;
			case STANDARD_DEVIATION:
				// Standard Deviation.
				aveValue = pFrameProfiler->m_selfTimeHistory.GetStdDeviation();
				aveValue *= 100.0f;
				currentValue = aveValue;
				break;

			case ALLOCATED_MEMORY:
				//currentValue = pProfiler->m_selfTime / 1024.0f;
				//FIX this
				//fDisplay_SelfTime = fDisplay_SelfTime;
				//fDisplay_TotalTime = fDisplay_TotalTime;
				currentValue = pFrameProfiler->m_selfTimeHistory.GetMax();
				aveValue = pFrameProfiler->m_selfTimeHistory.GetAverage();
				break;
			}
			;

			if ((SUBSYSTEM_INFO != m_displayQuantity) && (GetAdditionalSubsystems()))
			{
				float faveValue = pFrameProfiler->m_selfTimeHistory.GetAverage();
				if (pFrameProfiler->m_subsystem < PROFILE_LAST_SUBSYSTEM)
				{
					if (pFrameProfiler->m_description & EProfileDescription::WAITING)
					{
						m_subsystems[pFrameProfiler->m_subsystem].waitTime += faveValue;
					}
					else
					{
						m_subsystems[pFrameProfiler->m_subsystem].selfTime += faveValue;
					}
				}
			}

			//////////////////////////////////////////////////////////////////////////
			// Records Peaks.
			uint64 frameID = gEnv->nMainFrameID;
			if (bEnablePeaks)
			{
				float prevValue = pFrameProfiler->m_selfTimeHistory.GetLast();
				float peakValue = fDisplay_SelfTime;

				if (pFrameProfiler->m_latestFrame != frameID - 1)
				{
					prevValue = 0.0f;
				}

				if ((peakValue - prevValue) > fPeakTolerance)
				{
					SPeakRecord peak;
					peak.pProfiler = pFrameProfiler;
					peak.peakValue = peakValue;
					peak.averageValue = pFrameProfiler->m_selfTimeHistory.GetAverage();
					peak.count = pFrameProfiler->m_count;
					peak.pageFaults = m_nPagesFaultsLastFrame;
					peak.waiting = pFrameProfiler->m_description & EProfileDescription::WAITING;
					peak.when = gEnv->pTimer->TicksToSeconds(m_totalProfileTime);
					AddPeak(peak);

					// Call peak callbacks.
					for (auto const pPeakCallback : m_peakCallbacks)
					{
						pPeakCallback->OnFrameProfilerPeak(pFrameProfiler, peakValue);
					}
				}
			}

			//////////////////////////////////////////////////////////////////////////
			pFrameProfiler->m_latestFrame = frameID;
			pFrameProfiler->m_totalTimeHistory.Add(fDisplay_TotalTime);
			pFrameProfiler->m_selfTimeHistory.Add(fDisplay_SelfTime);
			pFrameProfiler->m_countHistory.Add(pFrameProfiler->m_count);

			pFrameProfiler->m_displayedValue = pFrameProfiler->m_displayedValue * smoothFactor + currentValue * (1.0f - smoothFactor);
			float variance = square(currentValue - aveValue);
			pFrameProfiler->m_variance = pFrameProfiler->m_variance * smoothFactor + variance * (1.0f - smoothFactor);

			if (m_bMemoryProfiling)
			{
				pFrameProfiler->m_displayedValue = currentValue;
			}

			if (m_bEnableHistograms)
			{
				if (!pFrameProfiler->m_pGraph)
				{
					// Create graph.
					pFrameProfiler->m_pGraph = new CFrameProfilerGraph;
				}
				// Update values in histogram graph.
				if (m_histogramsMaxPos != pFrameProfiler->m_pGraph->m_data.size())
				{
					pFrameProfiler->m_pGraph->m_width = m_histogramsMaxPos;
					pFrameProfiler->m_pGraph->m_height = m_histogramsHeight;
					pFrameProfiler->m_pGraph->m_data.resize(m_histogramsMaxPos);
				}
				float millis;
				if (m_displayQuantity == TOTAL_TIME || m_displayQuantity == TOTAL_TIME_EXTENDED)
					millis = m_histogramScale * pFrameProfiler->m_totalTimeHistory.GetLast();
				else
					millis = m_histogramScale * pFrameProfiler->m_selfTimeHistory.GetLast();
				if (millis < 0) millis = 0;
				if (millis > 255) millis = 255;
				pFrameProfiler->m_pGraph->m_data[m_histogramsCurrPos] = 255 - FtoI(millis); // must use ftoi.
			}

			if (m_nCurSample >= 0)
			{
				UpdateOfflineHistory(pFrameProfiler);
			}

			// Reset profiler.
			pFrameProfiler->m_totalTime = 0;
			pFrameProfiler->m_selfTime = 0;
			pFrameProfiler->m_peak = 0;
			pFrameProfiler->m_count = 0;
		}
	}

	for (int i = 0; i < PROFILE_LAST_SUBSYSTEM; i++)
	{
		m_subsystems[i].totalAnalized++;
		if (m_subsystems[i].selfTime > m_subsystems[i].budgetTime)
		{
			m_subsystems[i].totalOverBudget++;
		}

		m_subsystems[i].maxTime = (float)__fsel(m_subsystems[i].maxTime - m_subsystems[i].selfTime, m_subsystems[i].maxTime, m_subsystems[i].selfTime);
	}

	float frameSec = gEnv->pTimer->TicksToSeconds(m_frameTime);
	m_frameSecAvg = m_frameSecAvg * smoothFactor + frameSec * (1.0f - smoothFactor);

	float frameLostSec = gEnv->pTimer->TicksToSeconds(m_frameTime - selfAccountedTime);
	m_frameLostSecAvg = m_frameLostSecAvg * smoothFactor + frameLostSec * (1.0f - smoothFactor);

	float overheadtime = gEnv->pTimer->TicksToSeconds(numProfileCalls * m_nCallOverheadTotal / m_nCallOverheadCalls);
	m_frameOverheadSecAvg = m_frameOverheadSecAvg * smoothFactor + overheadtime * (1.0f - smoothFactor);

	if (m_nCurSample >= 0)
	{
		// Keep offline global time history.
		m_frameTimeOfflineHistory.m_selfTime.push_back(FtoI(frameSec * 1e6f));
		m_frameTimeOfflineHistory.m_count.push_back(1);
		m_nCurSample++;
	}

	// Reset profile callstack var.
	profile_callstack = 0;
}

void CFrameProfileSystem::OnSliceAndSleep()
{
	m_ProfilerThreads.OnEnterSliceAndSleep(GetCurrentThreadId());
	EndFrame();
	m_ProfilerThreads.OnLeaveSliceAndSleep(GetCurrentThreadId());
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::UpdateOfflineHistory(CFrameProfiler* pProfiler)
{
	if (!pProfiler->m_pOfflineHistory)
	{
		pProfiler->m_pOfflineHistory = new CFrameProfilerOfflineHistory;
		pProfiler->m_pOfflineHistory->m_count.reserve(1000 + m_nCurSample * 2);
		pProfiler->m_pOfflineHistory->m_selfTime.reserve(1000 + m_nCurSample * 2);
	}
	int prevCont = (int)pProfiler->m_pOfflineHistory->m_selfTime.size();
	int newCount = m_nCurSample + 1;
	pProfiler->m_pOfflineHistory->m_selfTime.resize(newCount);
	pProfiler->m_pOfflineHistory->m_count.resize(newCount);

	uint32 micros = FtoI(gEnv->pTimer->TicksToSeconds(pProfiler->m_selfTime) * 1e6f);
	uint16 count = pProfiler->m_count;
	for (int i = prevCont; i < newCount; i++)
	{
		pProfiler->m_pOfflineHistory->m_selfTime[i] = micros;
		pProfiler->m_pOfflineHistory->m_count[i] = count;
	}
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::AddPeak(SPeakRecord& peak)
{
	// Add peak.
	if (m_peaks.size() > MAX_PEAK_PROFILERS)
		m_peaks.pop_back();

	if (gEnv->IsDedicated())
		GetISystem()->GetILog()->Log("Peak: name:'%s' val:%.2f avg:%.2f cnt:%d",
		                             (const char*)GetFullName(peak.pProfiler),
		                             peak.peakValue,
		                             peak.averageValue,
		                             peak.count);

	m_peaks.insert(m_peaks.begin(), peak);

	// Add to absolute value
	std::vector<SPeakRecord>::iterator iter(m_absolutepeaks.begin());
	std::vector<SPeakRecord>::const_iterator const iterEnd(m_absolutepeaks.cend());

	for (; iter != iterEnd; ++iter)
	{
		SPeakRecord const& peakRecord = *iter;
		if (peakRecord.pProfiler == peak.pProfiler)
		{
			if (peakRecord.peakValue < peak.peakValue)
			{
				m_absolutepeaks.erase(iter);
				break;
			}
			else
			{
				return;
			}
		}
	}

	bool bInserted = false;
	for (auto& peakRecord : m_absolutepeaks)
	{
		if (peakRecord.peakValue < peak.peakValue)
		{
			peakRecord = peak;
			bInserted = true;
			break;
		}
	}

	if (!bInserted && m_absolutepeaks.size() < MAX_ABSOLUTEPEAK_PROFILERS)
	{
		m_absolutepeaks.push_back(peak);
	}
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::SetDisplayQuantity(EDisplayQuantity quantity)
{
	m_displayQuantity = quantity;
	m_bDisplayedProfilersValid = false;
	if (m_displayQuantity == SELF_TIME_EXTENDED || m_displayQuantity == TOTAL_TIME_EXTENDED)
		EnableHistograms(true);
	else
		EnableHistograms(false);

	if (m_displayQuantity == ALLOCATED_MEMORY || m_displayQuantity == ALLOCATED_MEMORY_BYTES)
	{
		m_bMemoryProfiling = true;
		gEnv->callbackStartSection = &StartMemoryProfilerSection;
		gEnv->callbackEndSection = &EndMemoryProfilerSection;
	}
	else
	{
		gEnv->callbackStartSection = &StartProfilerSection;
		gEnv->callbackEndSection = &EndProfilerSection;
		m_bMemoryProfiling = false;
	}
};

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::SetSubsystemFilter(bool bFilterSubsystem, EProfiledSubsystem subsystem)
{
	m_bSubsystemFilterEnabled = bFilterSubsystem;
	m_subsystemFilter = subsystem;
	m_bDisplayedProfilersValid = false;
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::SetSubsystemFilterThread(const char* sFilterThreadName)
{
	if (sFilterThreadName[0] != 0)
	{
		s_nFilterThreadId = gEnv->pThreadManager->GetThreadId(sFilterThreadName);
	}
	else
	{
		s_nFilterThreadId = 0;
	}
	if (s_nFilterThreadId)
	{
		m_filterThreadName = sFilterThreadName;
	}
	else
		m_filterThreadName = "";
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::SetSubsystemFilter(const char* szFilterName)
{
	bool bFound = false;
	for (int i = 0; i < PROFILE_LAST_SUBSYSTEM; i++)
	{
		if (!m_subsystems[i].name)
			continue;
		if (stricmp(m_subsystems[i].name, szFilterName) == 0)
		{
			SetSubsystemFilter(true, (EProfiledSubsystem)i);
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		// Check for count limit.
		int nCount = atoi(szFilterName);
		if (nCount > 0)
			m_maxProfileCount = nCount;
		else
			SetSubsystemFilter(false, PROFILE_ANY);
	}
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::AddPeaksListener(IFrameProfilePeakCallback* pPeakCallback)
{
	// Only add one time.
	stl::push_back_unique(m_peakCallbacks, pPeakCallback);
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::RemovePeaksListener(IFrameProfilePeakCallback* pPeakCallback)
{
	stl::find_and_erase(m_peakCallbacks, pPeakCallback);
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::EnableMemoryProfile(bool bEnable)
{
	if (bEnable != m_bDisplayMemoryInfo)
		m_bLogMemoryInfo = true;
	m_bDisplayMemoryInfo = bEnable;
}

bool CFrameProfileSystem::OnInputEvent(const SInputEvent& event)
{
	bool ret = false;

	//only react to keyboard input
	if (eIDT_Keyboard == event.deviceType)
	{
		//and only if the key was just pressed
		if (eIS_Pressed == event.state)
		{
			switch (event.keyId)
			{
			case eKI_ScrollLock:
				// toggle collection pause
				m_bCollectionPaused = !m_bCollectionPaused;
				m_bDisplayedProfilersValid = false;

				for (auto const pFrameProfiler : * m_pProfilers)
				{
					pFrameProfiler->m_selfTimeHistory.Clear();
				}

				UpdateInputSystemStatus();
				ret = true;
				break;
			case eKI_Escape:
				break;

			case eKI_Down:
			case eKI_Up:
				{
					// are we going up or down?
					int32 off = (event.keyId == eKI_Up) ? -1 : 1;

					//if the collection has been paused ...
					if (m_bCollectionPaused)
					{
						// ... allow selecting the row ...
						m_selectedRow += off;
						m_offset = (m_selectedRow - 30) * ROW_SIZE;

						m_pGraphProfiler = GetSelectedProfiler();
					}
					else
					{
						// ... otherwise scroll the whole display
						m_offset += off * ROW_SIZE;
					}

					// never allow negative offsets
					m_offset = (float)__fsel(m_offset, m_offset, 0.0f);
					ret = true;
				}
				break;

			case eKI_Left:
			case eKI_Right:
				if (m_bCollectionPaused)
				{
					// expand the selected profiler
					m_bDisplayedProfilersValid = false;
					if (m_pGraphProfiler)
					{
						m_pGraphProfiler->m_bExpended = (event.keyId == eKI_Right);
					}

					ret = true;
				}
				break;

			}
		}
	}

	return ret;
}

//////////////////////////////////////////////////////////////////////////
void CFrameProfileSystem::UpdateInputSystemStatus()
{
	if (gEnv->pInput)
	{
		// Remove ourself from the system
		if (gEnv->pInput->GetExclusiveListener() == this)
			gEnv->pInput->SetExclusiveListener(0);

		gEnv->pInput->RemoveEventListener(this);

		// Add ourself if needed
		if (m_bEnabled)
		{
			if (m_bCollectionPaused)
				gEnv->pInput->SetExclusiveListener(this);
			else
				gEnv->pInput->AddEventListener(this);
		}
	}
}

const char* CFrameProfileSystem::GetProfilerThreadName(CFrameProfiler* pProfiler) const
{
	if (pProfiler->m_threadId == GetMainThreadId())
	{
		return "Main";
	}

	// Add thread name.
	static char sThreadNameBuffer[256];
	const char* sThreadName = gEnv->pThreadManager->GetThreadName(pProfiler->m_threadId);
	if (sThreadName)
	{
		return sThreadName;
	}
	else
	{
		cry_sprintf(sThreadNameBuffer, "@%" PRI_THREADID, pProfiler->m_threadId);
	}
	return sThreadNameBuffer;
}

#else //USE_FRAME_PROFILER

// dummy functions if no frame profiler is used
CFrameProfiler* CFrameProfileSystem::GetProfiler(int index) const                                  { return nullptr; }
void            CFrameProfileSystem::StartFrame()                                                  {}
void            CFrameProfileSystem::EndFrame()                                                    {}
void            CFrameProfileSystem::OnSliceAndSleep()                                             {}
void            CFrameProfileSystem::AddPeaksListener(IFrameProfilePeakCallback* pPeakCallback)    {}
void            CFrameProfileSystem::RemovePeaksListener(IFrameProfilePeakCallback* pPeakCallback) {}
void            CFrameProfileSystem::AddFrameProfiler(CFrameProfiler* pProfiler)                   {}
void            CFrameProfileSystem::RemoveFrameProfiler(CFrameProfiler* pProfiler)                {}
void            CFrameProfileSystem::Enable(bool bCollect, bool bDisplay)                          {}
#endif
