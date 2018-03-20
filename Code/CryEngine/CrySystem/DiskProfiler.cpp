// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "StdAfx.h"

#ifdef USE_DISK_PROFILER

	#include "DiskProfiler.h"
	#include <CrySystem/IConsole.h>
	#include <CryRenderer/IRenderer.h>
	#include <CryRenderer/IRenderAuxGeom.h>
	#include <CryThreading/IThreadManager.h>

	#pragma warning(disable: 4244)

int CDiskProfiler::profile_disk = 1;
int CDiskProfiler::profile_disk_max_items = 10000;
int CDiskProfiler::profile_disk_max_draw_items = 2000;
float CDiskProfiler::profile_disk_timeframe = 5;    // max time for profiling timeframe
int CDiskProfiler::profile_disk_type_filter = -1;
int CDiskProfiler::profile_disk_budget = -1;

//////////////////////////////////////////////////////////////////////////
// Disk Profile main class
//////////////////////////////////////////////////////////////////////////
CDiskProfiler::~CDiskProfiler()
{
	for (Statistics::iterator it = m_statistics.begin(); it != m_statistics.end(); ++it)
	{
		delete *it;
	}
}

CDiskProfiler::CDiskProfiler(ISystem* pSystem) : m_bEnabled(false), m_pSystem(pSystem)
{
	assert(m_pSystem);
	m_outStatistics.m_nSeeksCount = 0;
	m_outStatistics.m_nFileReadCount = 0;
	m_outStatistics.m_nFileOpenCount = 0;

	// Register console var.
	REGISTER_CVAR(profile_disk, CDiskProfiler::profile_disk, 0,
	              "Enables Disk Profiler (should be deactivated for final product)\n"
	              "0=off, 1=on screen, 2=on disk\n"
	              "Disk profiling may not work on all combinations of OS and CPUs\n"
	              "Usage: profile_disk [0/1/2]");
	REGISTER_CVAR(profile_disk_max_items, 10000, 0,
	              "Set maximum number of IO statistics items to collect\n"
	              "The default value is 10000\n"
	              "Usage: profile_disk_max_items [num]");
	REGISTER_CVAR(profile_disk_max_draw_items, 10000, 0,
	              "Set maximum number of IO statistics items to visualize\n"
	              "The default value is 2000\n"
	              "Usage: profile_disk_max_draw_items [num]");
	REGISTER_CVAR(profile_disk_timeframe, 5, 0,
	              "Set maximum keeping time for collected IO statistics items in seconds\n"
	              "The default value is 5 sec\n"
	              "Usage: profile_disk_timeframe [sec]");
	REGISTER_CVAR(profile_disk_type_filter, -1, 0,
	              "Set the tasks to be filtered\n"
	              "Allowed values are: Textures = 1, Geometry = 2, Animation = 3, Audio = 4"
	              "The default value is -1 (disabled)\n"
	              "Usage: profile_disk_timeframe [val]");
	REGISTER_CVAR(profile_disk_budget, -1, 0,
	              "Set the budget in KB for the current time frame\n"
	              "The default value is -1 (disabled)\n"
	              "Usage: profile_disk_budget [val]");
}

bool CDiskProfiler::RegisterStatistics(SDiskProfileStatistics* pStatistics)
{
	if (m_bEnabled)
	{
		assert(pStatistics);
		CryAutoCriticalSection lock(m_csLock);

		if (m_currentThreadTaskType.find(pStatistics->m_threadId) != m_currentThreadTaskType.end())
			pStatistics->m_nTaskType = m_currentThreadTaskType[pStatistics->m_threadId];

		m_outStatistics.m_dOperationSize += pStatistics->m_size;
		if (pStatistics->m_nIOType == edotSeek)
		{
			m_outStatistics.m_nSeeksCount++;
		}
		else if (pStatistics->m_nIOType == edotOpen)
		{
			m_outStatistics.m_nFileOpenCount++;
		}
		else if (pStatistics->m_nIOType == edotRead)
		{
			m_outStatistics.m_nFileReadCount++;
		}
		if ((int)m_statistics.size() < profile_disk_max_items)
			m_statistics.push_back(pStatistics);
		else
			return false;
	}

	return true;
}

void Draw2dLabel(float x, float y, float font_size, const float* pfColor, bool bCenter, const char* label_text, ...)
{
	va_list args;
	va_start(args, label_text);

	char buf[1024];
	cry_vsprintf(buf, label_text, args);

	IRenderAuxText::Draw2dLabel(x, y, font_size, pfColor, bCenter, "%s", buf);

	if (CDiskProfiler::profile_disk == 3)
	{
		FILE* f = fopen("streaming.log", "a+");
		if (f)
		{
			fprintf_s(f, "%s\n", buf);
			fclose(f);
		}
	}
	va_end(args);
}

void CDiskProfiler::Render()
{
	const int width = gEnv->pRenderer->GetWidth();
	const int height = gEnv->pRenderer->GetHeight();

	// by default it's located at the bottom of the screen
	m_nHeightOffset = (height - 20);
	// but if the thread profiler is enabled, we move it to top
	if (gEnv->pConsole->GetCVar("profile_threads") && gEnv->pConsole->GetCVar("profile_threads")->GetIVal())
		m_nHeightOffset = (height - height * 9 / 16);

	float timeNow = gEnv->pTimer->GetAsyncCurTime();

	gEnv->pRenderer->Set2DMode(true, width, height);

	IRenderAuxGeom* pAux = gEnv->pRenderer->GetIRenderAuxGeom();

	float fTextSize = 1.1f;
	float colInfo1[4] = { 0, 1, 1, 1 };
	float colRed[4] = { 1, 0, 0, 1 };
	ColorB white(255, 255, 255, 255);

	int32 szy = 20; // vertical text labels' step

	int32 labelHeight = m_nHeightOffset - szy;  // height for current label in screen stack

	int32 startFileLabel = 0;
	// display IO statistics
	{
		uint32 nTextLeft = 10;
		// IO count
		Draw2dLabel(5, labelHeight, fTextSize, colInfo1, false, "IO calls: %d", m_statistics.size());
		nTextLeft += 100;

		// display read statistics
		size_t bytesRead = 0;
		bool firstMeasure = true;
		float minBandwidth = 0; // bytes/s
		float maxBandwidth = 0;
		int seeks = 0;
		for (Statistics::const_iterator it = m_statistics.begin(); it != m_statistics.end(); ++it)
		{
			if ((*it)->m_nIOType == edotSeek)  // item is not read IO
			{
				seeks++;
				if (!(*it)->m_strFile.empty() && startFileLabel < 1024)
				{
					Draw2dLabel(0, startFileLabel, fTextSize, colInfo1, false, "%s", (*it)->m_strFile.c_str());
					startFileLabel += 16;
				}
				continue;
			}

			// filtering by task type
			if (profile_disk_type_filter != -1 && (*it)->m_nTaskType != profile_disk_type_filter)
				continue;

			if ((*it)->m_endIOTime < 0)  // item is not finished yep
				continue;

			if ((*it)->m_nIOType != edotRead)  // item is not read IO
				continue;

			float dTime = (*it)->m_endIOTime - (*it)->m_beginIOTime;

			if (dTime > 0)
			{
				float throughput = (float)(*it)->m_size / dTime;
				if (firstMeasure)
				{
					minBandwidth = throughput;
					maxBandwidth = throughput;
					firstMeasure = false;
				}
				else
				{
					minBandwidth = min(minBandwidth, throughput);
					maxBandwidth = max(maxBandwidth, throughput);
				}
			}

			// total read bytes
			bytesRead += (*it)->m_size;
		}

		if (!firstMeasure)
		{
			const float fThp = (float)bytesRead / 1024 / max(.01f, profile_disk_timeframe);

			if (profile_disk_budget > 0 && profile_disk_budget < (int)fThp)
			{
				Draw2dLabel(nTextLeft, labelHeight - szy * 3, fTextSize * 3.f, colRed, false, "Budget overflow! Current bandwidth: %.2f KB, specified budget is: %d KB/s", fThp, profile_disk_budget);
			}

			Draw2dLabel(nTextLeft, labelHeight, fTextSize, colInfo1, false, "Avg read bandwidth: %.2f KB/s", fThp);
			nTextLeft += 200;
			//Draw2dLabel( nTextLeft,labelHeight,fTextSize,colInfo1,false,"Max read bandwidth: %.2f KB/s", maxBandwidth / 1024 );
			//nTextLeft += 200;
			//Draw2dLabel( nTextLeft,labelHeight,fTextSize,colInfo1,false,"Min read bandwidth: %.2f KB/s", minBandwidth / 1024 );
			//nTextLeft += 200;
			Draw2dLabel(nTextLeft, labelHeight, fTextSize, colInfo1, false, "Seeks: %i. Avg seeks %.2f seeks/s", seeks, (float)seeks / profile_disk_timeframe);
			nTextLeft += 200;
		}
	}

	// draw center line
	pAux->DrawLine(Vec3(0, m_nHeightOffset, 0), white, Vec3(width, m_nHeightOffset, 0), white);

	// refresh color legend for threads
	ThreadColorMap newColorLegend;

	// show file IOs
	int max_draws = 0;
	float last_time = -1;
	for (Statistics::reverse_iterator it = m_statistics.rbegin(); it != m_statistics.rend(); ++it)
	{
		if ((*it)->m_endIOTime < 0)  // item is not finished yep
			continue;

		// filtering by task type
		if (profile_disk_type_filter != -1 && (*it)->m_nTaskType != profile_disk_type_filter)
			continue;

		const uint32 nColorId = profile_disk != 4 ? (*it)->m_threadId : (*it)->m_nTaskType;

		// generate new color legend
		if (newColorLegend.find(nColorId) == newColorLegend.end())
		{
			if (m_threadsColorLegend.find(nColorId) == m_threadsColorLegend.end())
				newColorLegend[nColorId] = ColorB(32 * (rand() % 8), 32 * (rand() % 8), 32 * (rand() % 8), 255);
			else
				newColorLegend[nColorId] = m_threadsColorLegend[nColorId];
		}

		// draw block if it's more then one pixel in length
		if ((*it)->m_beginIOTime - last_time < profile_disk_timeframe / width)
		{
			ColorB IOTypeColor((((*it)->m_nIOType & edotRead) != 0) * 255, (((*it)->m_nIOType & edotCompress) != 0) * 255, (((*it)->m_nIOType & edotWrite) != 0) * 255, 255);

			RenderBlock((*it)->m_beginIOTime, (*it)->m_endIOTime, newColorLegend[nColorId], IOTypeColor);
			if (++max_draws > profile_disk_max_draw_items)
				break;
		}

		last_time = (*it)->m_endIOTime;
	}

	// removing unused threads legend colors
	m_threadsColorLegend = newColorLegend;

	// show threads legend
	for (ThreadColorMap::const_iterator i = m_threadsColorLegend.begin(); i != m_threadsColorLegend.end(); ++i)
	{
		labelHeight -= szy;
		float fThreadColor[4] = { (float)i->second.r / 255.0f, (float)i->second.g / 255.0f, (float)i->second.b / 255.0f, 1.0f };
		if (profile_disk != 4)
			Draw2dLabel(1, labelHeight, fTextSize, fThreadColor, false, "Thread: %s", gEnv->pThreadManager->GetThreadName(i->first));
		else
		{
			if (i->first < eStreamTaskTypeCount)
				Draw2dLabel(1, labelHeight, fTextSize, fThreadColor, false, "Task: %s", gEnv->pSystem->GetStreamEngine()->GetStreamTaskTypeName((EStreamTaskType)i->first));
		}
	}

	gEnv->pRenderer->Set2DMode(false, 0, 0);
}

void CDiskProfiler::Update()
{
	CryAutoCriticalSection lock(m_csLock);

	if (profile_disk > 0)
	{
		m_bEnabled = true;

		if (profile_disk > 1)
			Render();

		const float timeNow = gEnv->pTimer->GetAsyncCurTime();

		// clear redundant statistics
		for (uint32 i = 0; i < m_statistics.size(); )
		{
			SDiskProfileStatistics* pStatistics = m_statistics[i];
			if (pStatistics->m_endIOTime > 0 && timeNow - pStatistics->m_endIOTime > profile_disk_timeframe)
			{
				delete pStatistics;
				m_statistics.erase(m_statistics.begin() + i);
			}
			else
				++i;
		}
	}
	else
		m_bEnabled = false;

}

void CDiskProfiler::RenderBlock(const float timeStart, const float timeEnd, const ColorB threadColor, const ColorB IOTypeColor)
{
	IRenderAuxGeom* pAux = gEnv->pRenderer->GetIRenderAuxGeom();

	const float timeNow = gEnv->pTimer->GetAsyncCurTime();

	const int width = gEnv->pRenderer->GetWidth();
	const int height = gEnv->pRenderer->GetHeight();

	static const float halfSize = 8;  // bar thickness

	// calc bar screen coords(in pixels)
	float start = floorf((timeNow - timeStart) / profile_disk_timeframe * width);
	start = min((float)width, max(0.f, start));
	float end = floorf((timeNow - timeEnd) / profile_disk_timeframe * width);
	end = min((float)width, max(0.f, end));
	end = max(end, start + 1.f); // avoid empty bars

	Vec3 quad[6] = {
		Vec3(start, m_nHeightOffset - halfSize, 0),
		Vec3(end,   m_nHeightOffset - halfSize, 0),
		Vec3(end,   m_nHeightOffset,            0),
		Vec3(start, m_nHeightOffset,            0),
		Vec3(end,   m_nHeightOffset + halfSize, 0),
		Vec3(start, m_nHeightOffset + halfSize, 0)
	};

	// top half
	pAux->DrawTriangle(quad[0], threadColor, quad[2], threadColor, quad[1], threadColor);
	pAux->DrawTriangle(quad[0], threadColor, quad[3], threadColor, quad[2], threadColor);

	// bottom half
	pAux->DrawTriangle(quad[3], IOTypeColor, quad[4], IOTypeColor, quad[2], IOTypeColor);
	pAux->DrawTriangle(quad[3], IOTypeColor, quad[5], IOTypeColor, quad[4], IOTypeColor);
}

bool CDiskProfiler::IsEnabled() const
{
	return m_bEnabled && (int)m_statistics.size() < profile_disk_max_items;
}

void CDiskProfiler::SetTaskType(const threadID nThreadId, const uint32 nType /*= eStreamTaskTypeCount*/)
{
	CryAutoCriticalSection lock(m_csLock);
	m_currentThreadTaskType[nThreadId] = (EStreamTaskType)nType;
}

#endif
