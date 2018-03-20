// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include "ParticleCommon.h"

namespace pfx2
{

class CParticleProfiler;
class CParticleComponentRuntime;

enum EProfileStat
{
	EPS_Jobs,
	EPS_AllocatedParticles,
	EPS_ActiveParticles,
	EPS_RendereredParticles,
	EPS_NewBornTime,
	EPS_UpdateTime,
	EPS_ComputeVerticesTime,
	EPS_TotalTiming,

	EPST_Count,
};

class CTimeProfiler
{
public:
	CTimeProfiler(CParticleProfiler& profiler, CParticleComponentRuntime* pRuntime, EProfileStat stat);
	~CTimeProfiler();

private:
	CParticleProfiler&         m_profiler;
	CParticleComponentRuntime* m_pRuntime;
	int64                      m_startTicks;
	EProfileStat               m_stat;
};

struct SStatistics
{
	SStatistics();
	uint m_values[EPST_Count];
};

class CParticleProfiler
{
public:
	class CCSVFileOutput;
	class CStatisticsDisplay;

	struct SEntry
	{
		CParticleComponentRuntime* m_pRuntime;
		EProfileStat               m_type;
		uint                       m_value;
	};
	typedef std::vector<SEntry> TEntries;

public:
	CParticleProfiler();

	void Reset();
	void Display();
	void SaveToFile();

	void AddEntry(CParticleComponentRuntime* pRuntime, EProfileStat type, uint value = 1);

private:
	void SortEntries();
	void WriteEntries(CCSVFileOutput& output) const;

	void DrawPerfomanceStats();
	void DrawStatsCounts(CStatisticsDisplay& output, Vec2 pos, uint budget);
	void DrawStats(CStatisticsDisplay& output, Vec2 pos, EProfileStat stat, uint budget, cstr statName);
	void DrawMemoryStats();

	std::vector<TEntries> m_entries;
};

}

#include "ParticleProfilerImpl.h"
