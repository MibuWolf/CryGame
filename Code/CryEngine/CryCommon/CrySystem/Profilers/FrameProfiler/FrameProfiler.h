// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include <CryCore/Platform/platform.h>
#include <CrySystem/ISystem.h>

#include "FrameProfiler_Shared.h"
#include "FrameProfiler_Internal.h"
#include "FrameProfiler_Platform.h"
#include "FrameProfiler_Brofiler.h"

//////////////////////////////////////////////////////////////////////////
/*! This callback will be called for Profiling Peaks.
 */
struct IFrameProfilePeakCallback
{
	virtual ~IFrameProfilePeakCallback(){}
	//! Called when peak is detected for this profiler.
	//! @param fPeakTime peak time in milliseconds.
	virtual void OnFrameProfilerPeak(CFrameProfiler* pProfiler, float fPeakTime) = 0;
};

struct SPeakRecord
{
	CFrameProfiler* pProfiler;
	float           peakValue;
	float           averageValue;
	float           variance;
	int             pageFaults; //!< Number of page faults at this frame.
	int             count;      //!< Number of times called for peak.
	float           when;       //!< When it added.
	BYTE            waiting;    //!< If it needs to go in separate waiting peak list
};

//////////////////////////////////////////////////////////////////////////
//! IFrameProfileSystem interface - system which does the gathering of stats.
struct IFrameProfileSystem
{
	virtual ~IFrameProfileSystem(){}
	enum EDisplayQuantity
	{
		SELF_TIME,
		TOTAL_TIME,
		SELF_TIME_EXTENDED,
		TOTAL_TIME_EXTENDED,
		PEAK_TIME,
		SUBSYSTEM_INFO,
		COUNT_INFO,
		STANDARD_DEVIATION,
		ALLOCATED_MEMORY,
		ALLOCATED_MEMORY_BYTES,
		STALL_TIME,
		PEAKS_ONLY,
	};

	//! Reset all profiling data.
	virtual void Reset() = 0;

	//! Add new frame profiler.
	virtual void AddFrameProfiler(CFrameProfiler* pProfiler) = 0;

	//! Remove existing frame profiler.
	virtual void RemoveFrameProfiler(CFrameProfiler* pProfiler) = 0;

	//! Must be called at the start of the frame.
	virtual void StartFrame() = 0;

	//! Must be called at the end of the frame.
	virtual void EndFrame() = 0;

	//! Must be called when something quacks like the end of the frame.
	virtual void OnSliceAndSleep() = 0;

	//! Get number of registered frame profilers.
	virtual int GetProfilerCount() const = 0;

	//! Get frame profiler at specified index.
	//! \param index must be 0 <= index < GetProfileCount().
	virtual CFrameProfiler* GetProfiler(int index) const = 0;

	//! Get number of registered peak records.
	virtual int GetPeaksCount() const = 0;

	//! Get peak record at specified index.
	//! \param index must be 0 <= index < GetPeaksCount().
	virtual const SPeakRecord* GetPeak(int index) const = 0;

	//! Gets the bottom active section.
	virtual CFrameProfilerSection const* GetCurrentProfilerSection() = 0;

	//! get internal time lost to profiling.
	virtual float GetLostFrameTimeMS() const = 0;

	virtual void  Enable(bool bCollect, bool bDisplay) = 0;
	virtual void  SetSubsystemFilter(bool bFilterSubsystem, EProfiledSubsystem subsystem) = 0;

	//! True if profiler is turned off (even if collection is paused).
	virtual bool IsEnabled() const = 0;

	//! True if profiler statistics is visible.
	virtual bool IsVisible() const = 0;

	//! True if profiler must collect profiling data.
	virtual bool IsProfiling() const = 0;
	virtual void SetDisplayQuantity(EDisplayQuantity quantity) = 0;

	//! For custom frame profilers.
	virtual void StartCustomSection(CCustomProfilerSection* pSection) = 0;
	virtual void EndCustomSection(CCustomProfilerSection* pSection) = 0;

	//! Register peak listener callback to be called when peak value is greater then this.
	virtual void AddPeaksListener(IFrameProfilePeakCallback* pPeakCallback) = 0;
	virtual void RemovePeaksListener(IFrameProfilePeakCallback* pPeakCallback) = 0;

	//! Access to call stack string.
	virtual const char* GetFullName(CFrameProfiler* pProfiler) = 0;
	virtual const char* GetModuleName(CFrameProfiler* pProfiler) = 0;
};

//! CFrameProfilerSamplesHistory provides information on history of sample values for profiler counters.
template<class T, int TCount>
class CFrameProfilerSamplesHistory
{
public:
	CFrameProfilerSamplesHistory() : m_nHistoryNext(0), m_nHistoryCount(0) {}

	//! Add a new sample to history.
	void Add(T sample)
	{
		m_history[m_nHistoryNext] = sample;
		m_nHistoryNext = (m_nHistoryNext + TCount - 1) % TCount;
		if (m_nHistoryCount < TCount)
			++m_nHistoryCount;
	}
	//! Cleans up the data history.
	void Clear()
	{
		m_nHistoryNext = 0;
		m_nHistoryCount = 0;
	}
	//! Get last sample value.
	T GetLast()
	{
		if (m_nHistoryCount)
			return m_history[(m_nHistoryNext + 1) % TCount];
		else
			return 0;
	}
	//! Calculates average sample value for at most the given number of frames (less if so many unavailable).
	T GetAverage(int nCount = TCount)
	{
		if (m_nHistoryCount)
		{
			T fSum = 0;
			if (nCount > m_nHistoryCount)
				nCount = m_nHistoryCount;
			for (int i = 1; i <= nCount; ++i)
			{
				fSum += m_history[(m_nHistoryNext + i) % (sizeof(m_history) / sizeof((m_history)[0]))];
			}
			return fSum / nCount;
		}
		else
			return 0;
	}
	//! Calculates average sample value for at most the given number of frames (less if so many unavailable),
	//! multiplied by the Poisson function.
	T GetAveragePoisson(int nCount, float fMultiplier)
	{
		if (m_nHistoryCount)
		{
			float fSum = 0, fCurrMult = 1, fSumWeight = 0;
			if (nCount > m_nHistoryCount)
				nCount = m_nHistoryCount;
			for (int i = 1; i <= nCount; ++i)
			{
				fSum += m_history[(m_nHistoryNext + i) % TCount] * fCurrMult;
				fSumWeight += fCurrMult;
				fCurrMult *= fMultiplier;
			}
			return (T)(fSum / fSumWeight);
		}
		else
			return 0;
	}
	//! calculates Standard deviation of values.
	//! stdev = Sqrt( Sum((X-Xave)^2)/(n-1) ).
	T GetStdDeviation(int nCount = TCount)
	{
		if (m_nHistoryCount > 1)  // standard deviation is undefined for only one element
		{
			T fAve = GetAverage(nCount);
			T fVal = 0;
			T fSumVariance = 0;
			if (nCount > m_nHistoryCount)
				nCount = m_nHistoryCount;
			for (int i = 1; i <= nCount; ++i)
			{
				fVal = m_history[(m_nHistoryNext + i) % (sizeof(m_history) / sizeof((m_history)[0]))];
				fSumVariance = (fVal - fAve) * (fVal - fAve); // (X-Xave)^2
			}
			return sqrtf(fSumVariance / (nCount - 1));
		}
		else
			return 0;
	}
	//! Calculates max sample value for at most the given number of frames (less if so many unavailable).
	T GetMax(int nCount = TCount)
	{
		if (m_nHistoryCount)
		{
			T fMax = 0;
			if (nCount > m_nHistoryCount)
				nCount = m_nHistoryCount;
			for (int i = 1; i <= nCount; ++i)
			{
				T fCur = m_history[(m_nHistoryNext + i) % (sizeof(m_history) / sizeof((m_history)[0]))];
				if (i == 1 || fCur > fMax)
					fMax = fCur;
			}
			return fMax;
		}
		else
			return 0;
	}
	//! Calculates min sample value for at most the given number of frames (less if so many unavailable).
	T GetMin(int nCount = TCount)
	{
		if (m_nHistoryCount)
		{
			T fMin = 0;
			if (nCount > m_nHistoryCount)
				nCount = m_nHistoryCount;
			for (int i = 1; i <= nCount; ++i)
			{
				T fCur = m_history[(m_nHistoryNext + i) % (sizeof(m_history) / sizeof((m_history)[0]))];
				if (i == 1 || fCur < fMin)
					fMin = fCur;
			}
			return fMin;
		}
		else
			return 0;
	}
protected:
	//! The timer values for the last frames.
	T m_history[TCount];

	//! The current pointer in the timer history, decreases.
	int m_nHistoryNext;

	//! The number of currently collected samples in the timer history.
	int m_nHistoryCount;

	// Adds the entry to the timer history (current timer value).
};

//////////////////////////////////////////////////////////////////////////
class CFrameProfilerGraph
{
public:
	int                m_x;
	int                m_y;
	int                m_width;
	int                m_height;
	std::vector<uint8> m_data;
};

//////////////////////////////////////////////////////////////////////////
class CFrameProfilerOfflineHistory
{
public:
	//! Self time in microseconds.
	std::vector<uint32> m_selfTime;
	//! Number of calls.
	std::vector<uint16> m_count;
};

//! CFrameProfiler is a single profiler counter with unique name and data.
//! Multiple Sections can be executed for this profiler, they all will be merged in this class.
//! CFrameProfileSection must reference pointer to instance of this counter, to collect the sampling data.
class CFrameProfiler
{
public:
	ISystem*      m_pISystem;
	const char*   m_name;
	const char*   m_fileName;
	unsigned long m_fileLine;

	//! Total time spent in this counter including time of child profilers in current frame.
	int64 m_totalTime;

	//! Self frame time spent only in this counter (But includes recursive calls to same counter) in current frame.
	int64 m_selfTime;

	//! Latest frame ID.
	uint64 m_latestFrame;
	//! Displayed quantity (interpolated or average).
	float  m_displayedValue;

	//! How many times this profiler counter was executed.
	int m_count;

	//! How variant this value.
	float m_variance;

	//! peak from this frame (useful if count is >1).
	int64 m_peak;

	//! Current parent profiler in last frame.
	CFrameProfiler* m_pParent;

	//! Expended or collapsed displaying state.
	uint64 m_bExpended     : 8;
	uint64 m_bHaveChildren : 8;
	uint64 m_subsystem     : 8;

	//! Identifier to be used for color lookup
	uint64      m_colorIdentifier : 8;

	const char* m_stallCause;     //!< Additional information of stall cause.

	//! Thread Id of this instance.
	threadID            m_threadId;
	//! Linked list to other thread instances.
	CFrameProfiler*     m_pNextThread;

	EProfileDescription m_description;

#if ALLOW_BROFILER
	std::shared_ptr<::Profiler::EventDescription> m_brofilerEventDescription;
#endif

	//! Tells if this FrameProfiler has yet been added to the FrameProfileSystem.
	bool m_bInitialized;

	CFrameProfilerSamplesHistory<float, PROFILE_HISTORY_COUNT> m_totalTimeHistory;
	CFrameProfilerSamplesHistory<float, PROFILE_HISTORY_COUNT> m_selfTimeHistory;
	CFrameProfilerSamplesHistory<int, PROFILE_HISTORY_COUNT>   m_countHistory;

	//! Graph data for this frame profiler.

	//! Graph associated with this profiler.
	CFrameProfilerGraph*          m_pGraph;
	CFrameProfilerOfflineHistory* m_pOfflineHistory;

	CFrameProfiler(const EProfiledSubsystem subsystem, const EProfileDescription desc,
	               const char* sCollectorName, const char* fileName, const unsigned long fileLine)
		: m_pISystem(GetISystem())
		, m_name(sCollectorName)
		, m_fileName(fileName)
		, m_fileLine(fileLine)
		, m_totalTime(0)
		, m_selfTime(0)
		, m_latestFrame(0)
		, m_displayedValue(0.0f)
		, m_count(0)
		, m_variance(0.0f)
		, m_peak(0)
		, m_pParent(nullptr)
		, m_bExpended(0)
		, m_bHaveChildren(0)
		, m_subsystem((uint8)subsystem)
		, m_colorIdentifier(0)
		, m_stallCause(nullptr)
		, m_threadId(0)
		, m_pNextThread(nullptr)
		, m_description(desc)
		, m_bInitialized(false)
#if ALLOW_BROFILER
		, m_brofilerEventDescription(nullptr)
#endif
		, m_pGraph(nullptr)
		, m_pOfflineHistory(nullptr)
	{
		if (IFrameProfileSystem* const pFrameProfileSystem = m_pISystem->GetIProfileSystem())
		{
			pFrameProfileSystem->AddFrameProfiler(this);
		}
	}

	~CFrameProfiler()
	{
		// This is needed for when modules get unloaded at runtime.
		if (m_pISystem != nullptr)
		{
			if (IFrameProfileSystem* const pFrameProfileSystem = m_pISystem->GetIProfileSystem())
			{
				pFrameProfileSystem->RemoveFrameProfiler(this);
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////////
//! CFrameProfilerSection is an auto class placed where code block need to be profiled.
//! Every time this object is constructed and destructed the time between constructor
//! and destructor is merged into the referenced CFrameProfiler instance.
//! For consoles, a faster non LHS triggering way is used.
//! (but potentially problematic with many threads when profiling is enabled at a very 'bad' time).
class CFrameProfilerSection
{
public:
	int64                  m_startTime;
	int64                  m_excludeTime;
	CFrameProfiler*        m_pFrameProfiler;
	CFrameProfilerSection* m_pParent;
	CBootProfilerRecord*   m_pRecord;
#if ALLOW_BROFILER
	::Profiler::EventData * m_brofilerEventData;
#endif

	ILINE CFrameProfilerSection(CFrameProfiler* profiler, const char* sectionName, const char* instanceArguments, EProfileDescription profileDescription)
		: m_startTime(0)
		, m_excludeTime(0)
		, m_pFrameProfiler(nullptr)
		, m_pParent(nullptr)
		, m_pRecord(nullptr)
#if ALLOW_BROFILER
		, m_brofilerEventData(nullptr)
#endif
	{
		m_pRecord = (gEnv->bBootProfilerEnabledFrames) ? gEnv->pSystem->StartBootSectionProfiler(sectionName, instanceArguments) : nullptr;
		if (gEnv->bDeepProfiling || profileDescription == EProfileDescription::REGION)
		{
			m_pFrameProfiler = profiler;
			gEnv->callbackStartSection(this);
		}
	}

	ILINE ~CFrameProfilerSection()
	{
		if (m_pFrameProfiler)
		{
			gEnv->callbackEndSection(this);
		}
		if (m_pRecord)
		{
			gEnv->pSystem->StopBootSectionProfiler(m_pRecord);
		}
	}
};

//////////////////////////////////////////////////////////////////////////
//! CCustomProfilerSection is an auto class placed where any custom data need to be profiled.
//! Works similarly to CFrameProfilerSection, but records any custom data, instead of elapsed time.
class CRY_ALIGN(32) CCustomProfilerSection
{
public:
	int* m_pValue;
	int m_excludeValue;
	CFrameProfiler* m_pFrameProfiler;
	CCustomProfilerSection* m_pParent;

	//! pValue pointer must remain valid until after calling destructor of this custom profiler section.
	ILINE CCustomProfilerSection(CFrameProfiler* profiler, int* pValue)
		: m_pValue(pValue)
		  , m_excludeValue(0)
		  , m_pFrameProfiler(profiler)
		  , m_pParent(nullptr)
	{
		if (profiler)
		{
			m_pFrameProfiler->m_pISystem->GetIProfileSystem()->StartCustomSection(this);
		}
	}
	ILINE ~CCustomProfilerSection()
	{
		if (m_pFrameProfiler)
		{
			m_pFrameProfiler->m_pISystem->GetIProfileSystem()->EndCustomSection(this);
		}
	}
};

class CBootProfileFrameScope
{
public:
	CBootProfileFrameScope()
	{
		if (gEnv && gEnv->pSystem)
		{
			gEnv->pSystem->OnFrameStart("frames");
		}
	}
	~CBootProfileFrameScope()
	{
		if (gEnv && gEnv->pSystem)
		{
			gEnv->pSystem->OnFrameEnd();
		}
	}
};

#if !defined(_RELEASE) || defined(PERFORMANCE_BUILD)
// Profiling is enabled in every configuration except Release

	#define CRY_PROFILE_THREADNAME(szName) \
	  INTERNAL_PROFILER_THREADNAME(szName) \
	  BROFILER_THREADNAME(szName)          \
	  PLATFORM_PROFILER_THREADNAME(szName)

	#define CRY_PROFILE_FRAMESTART(szName) \
	  INTERNAL_PROFILER_FRAMESTART(szName) \
	  BROFILER_FRAMESTART(szName)          \
	  PLATFORM_PROFILER_FRAMESTART(szName)

	#define CRY_PROFILE_REGION(subsystem, szName) \
	  INTERNAL_PROFILER_REGION(subsystem, szName) \
	  BROFILER_REGION(szName)                     \
	  PLATFORM_PROFILER_REGION(szName)

	#define CRY_PROFILE_REGION_ARG(subsystem, szName, argument) \
	  INTERNAL_PROFILER_REGION_ARG(subsystem, szName, argument) \
	  BROFILER_REGION(szName)                                   \
	  PLATFORM_PROFILER_REGION(szName)

	#define CRY_PROFILE_REGION_WAITING(subsystem, szName) \
	  INTERNAL_PROFILER_REGION_WAITING(subsystem, szName) \
	  BROFILER_REGION_WAITING(szName)                     \
	  PLATFORM_PROFILER_REGION_WAITING(szName)

	#if ALLOW_DEEP_PROFILING
		#define CRY_PROFILE_FUNCTION(subsystem)           \
		  INTERNAL_PROFILER_FUNCTION(subsystem, __FUNC__) \
		  BROFILER_FUNCTION(__FUNC__)                     \
		  PLATFORM_PROFILER_FUNCTION(__FUNC__)

		#define CRY_PROFILE_FUNCTION_ARG(subsystem, argument)           \
		  INTERNAL_PROFILER_FUNCTION_ARG(subsystem, __FUNC__, argument) \
		  BROFILER_FUNCTION(__FUNC__)                                   \
		  PLATFORM_PROFILER_FUNCTION(__FUNC__)

		#define CRY_PROFILE_FUNCTION_WAITING(subsystem)           \
		  INTERNAL_PROFILER_FUNCTION_WAITING(subsystem, __FUNC__) \
		  BROFILER_FUNCTION_WAITING(__FUNC__)                     \
		  PLATFORM_PROFILER_FUNCTION_WAITING(__FUNC__)

		#define CRY_PROFILE_SECTION(subsystem, szName) \
		  INTERNAL_PROFILER_SECTION(subsystem, szName) \
		  BROFILER_SECTION(szName)                     \
		  PLATFORM_PROFILER_SECTION(szName)

		#define CRY_PROFILE_SECTION_WAITING(subsystem, szName) \
		  INTERNAL_PROFILER_SECTION_WAITING(subsystem, szName) \
		  BROFILER_SECTION_WAITING(szName)                     \
		  PLATFORM_PROFILER_SECTION_WAITING(szName)

		#define CRY_PROFILE_MARKER(szName) \
		  INTERNAL_PROFILER_MARKER(szName) \
		  BROFILER_MARKER(szName)          \
		  PLATFORM_PROFILER_MARKER(szName)

/*
 # define CRY_PROFILE_PUSH(subsystem, szName) \
   INTERNAL_PROFILER_PUSH(subsystem, szName) \
   BROFILER_PUSH(szName) \
   PLATFORM_PROFILER_PUSH(szName)

 # define CRY_PROFILE_POP() \
   INTERNAL_PROFILER_POP() \
   BROFILER_POP() \
   PLATFORM_PROFILER_POP()
 */

	#else
		#define CRY_PROFILE_FUNCTION(subsystem)                /*do nothing*/
		#define CRY_PROFILE_FUNCTION_WAITING(subsystem)        /*do nothing*/
		#define CRY_PROFILE_SECTION(subsystem, szName)         /*do nothing*/
		#define CRY_PROFILE_SECTION_WAITING(subsystem, szName) /*do nothing*/
		#define CRY_PROFILE_MARKER(szName)                     /*do nothing*/
//#	define CRY_PROFILE_PUSH(subsystem, szName) /*do nothing*/
//#	define CRY_PROFILE_POP() /*do nothing*/
	#endif

#else
// Release builds do not have any kind of profiling information
	#define CRY_PROFILE_THREADNAME(szName)                      /*do nothing*/
	#define CRY_PROFILE_FRAMESTART(szName)                      /*do nothing*/
	#define CRY_PROFILE_REGION(subsystem, szName)               /*do nothing*/
	#define CRY_PROFILE_REGION_ARG(subsystem, szName, argument) /*do nothing*/
	#define CRY_PROFILE_REGION_WAITING(subsystem, szName)       /*do nothing*/
	#define CRY_PROFILE_FUNCTION(subsystem)                     /*do nothing*/
	#define CRY_PROFILE_FUNCTION_ARG(subsystem, argument)       /*do nothing*/
	#define CRY_PROFILE_FUNCTION_WAITING(subsystem)             /*do nothing*/
	#define CRY_PROFILE_SECTION(subsystem, szName)              /*do nothing*/
	#define CRY_PROFILE_SECTION_WAITING(subsystem, szName)      /*do nothing*/
	#define CRY_PROFILE_MARKER(szName)                          /*do nothing*/
//#define CRY_PROFILE_PUSH(subsystem, szName) /*do nothing*/
//#define CRY_PROFILE_POP() /*do nothing*/
#endif

/////////// DEPRECATED - do not use anymore /////////////

#define FUNCTION_PROFILER(pISystem, subsystem) \
  CRY_PROFILE_FUNCTION(subsystem)

#define FRAME_PROFILER(szName, pISystem, subsystem) \
  CRY_PROFILE_SECTION(subsystem, szName)

// STALL_PROFILER labels should be converted to WAITING sections
#define STALL_PROFILER(cause)

/////////////////////////////////////////////////////////
