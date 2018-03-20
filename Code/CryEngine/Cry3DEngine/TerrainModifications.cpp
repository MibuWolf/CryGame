// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "StdAfx.h"
#include "TerrainModifications.h"         // CTerrainModifications
#include "terrain.h"                      // CTerrain
#include "terrain_sector.h"               // CTerrainNode

CTerrainModifications::CTerrainModifications() : m_pModifiedTerrain(0)
{
}

void CTerrainModifications::FreeData()
{
	m_TerrainMods.clear();
}

bool CTerrainModifications::PushModification(const Vec3& vPos, const float fRadius)
{
	assert(fRadius > 0);
	assert(m_pModifiedTerrain);       // you need to call SetTerrain()

	m_TerrainMods.push_back(STerrainMod(vPos, fRadius, m_pModifiedTerrain->GetZ((int)vPos.x, (int)vPos.y, GetDefSID())));

	MakeCrater(m_TerrainMods.back(), m_TerrainMods.size() - 1);

	return true;
}

// Arguments:
//   fDamage - 0=outside..1=midpoint
static float CalcDepth(const float fDamage)
{
	return (fDamage);
}

float CTerrainModifications::ComputeMaxDepthAt(const float fX, const float fY, const uint32 dwCheckExistingMods) const
{
	float fRet = 0;

	std::vector<STerrainMod>::const_iterator it, end = m_TerrainMods.end();

	float fGroundPos = m_pModifiedTerrain->GetZ((int)fX, (int)fY, GetDefSID());

	uint32 dwI = 0;
	for (it = m_TerrainMods.begin(); it != end && dwI < dwCheckExistingMods; ++it, ++dwI)
	{
		const STerrainMod& ref = *it;

		float fDist2 = sqr(ref.m_vPos.x - fX) + sqr(ref.m_vPos.y - fY) + sqr(ref.m_vPos.z - fGroundPos);

		if (fDist2 < ref.m_fRadius * ref.m_fRadius)
		{
			float fDamage = 1.0f - sqrtf(fDist2) / ref.m_fRadius;

			fRet += CalcDepth(fDamage);
		}
	}

	return fRet;
}

void CTerrainModifications::MakeCrater(const STerrainMod& ref, const uint32 dwCheckExistingMod)
{
	if (!GetCVars()->e_TerrainDeformations)
		return;

	assert(m_pModifiedTerrain);

	// calculate 2d area
	int nUnitSize = m_pModifiedTerrain->GetHeightMapUnitSize();
	int x1 = int(ref.m_vPos.x - ref.m_fRadius - nUnitSize);
	int y1 = int(ref.m_vPos.y - ref.m_fRadius - nUnitSize);
	int x2 = int(ref.m_vPos.x + ref.m_fRadius + nUnitSize);
	int y2 = int(ref.m_vPos.y + ref.m_fRadius + nUnitSize);
	x1 = x1 / nUnitSize * nUnitSize;
	x2 = x2 / nUnitSize * nUnitSize;
	y1 = y1 / nUnitSize * nUnitSize;
	y2 = y2 / nUnitSize * nUnitSize;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= CTerrain::GetTerrainSize()) x2 = CTerrain::GetTerrainSize() - 1;
	if (y2 >= CTerrain::GetTerrainSize()) y2 = CTerrain::GetTerrainSize() - 1;

	float fOceanLevel = m_pModifiedTerrain->GetWaterLevel() + 0.25f;

	float fInvExplRadius = 1.0f / ref.m_fRadius;

	// to limit the height modifications to a reasonable value
	float fMaxDepth = ComputeMaxDepthAt(ref.m_vPos.x, ref.m_vPos.y, dwCheckExistingMod);

	// modify height map
	for (int x = x1; x <= x2; x += nUnitSize)
		for (int y = y1; y <= y2; y += nUnitSize)
		{
			float fHeight = m_pModifiedTerrain->GetZ(x, y, GetDefSID());

			float fDamage = 1.0f - ref.m_vPos.GetDistance(Vec3((float)x, (float)y, fHeight)) * fInvExplRadius;
			if (fDamage < 0)
				continue;

			float fDepthMod = CLAMP(CalcDepth(fDamage) - fMaxDepth, 0, TERRAIN_DEFORMATION_MAX_DEPTH);

			// modify elevation if above the ocean level
			if (fHeight > fOceanLevel && fHeight > fDepthMod)
			{
				fHeight -= fDepthMod;
				if (fHeight < fOceanLevel)
					fHeight = fOceanLevel;
			}

			m_pModifiedTerrain->m_bHeightMapModified = 1;

			m_pModifiedTerrain->SetZ(x, y, fHeight, GetDefSID());
		}

	// update mesh or terrain near sectors
	PodArray<CTerrainNode*> lstNearSecInfos;
	Vec3 vRadius(ref.m_fRadius, ref.m_fRadius, ref.m_fRadius);
	m_pModifiedTerrain->IntersectWithBox(AABB(ref.m_vPos - vRadius, ref.m_vPos + vRadius), &lstNearSecInfos, GetDefSID());
	for (int s = 0; s < lstNearSecInfos.Count(); s++)
		if (CTerrainNode* pSecInfo = lstNearSecInfos[s])
			pSecInfo->ReleaseHeightMapGeometry();

	m_pModifiedTerrain->ResetHeightMapCache();
}

void CTerrainModifications::SetTerrain(CTerrain& rTerrain)
{
	m_pModifiedTerrain = &rTerrain;
}

void CTerrainModifications::SerializeTerrainState(TSerialize ser)
{
	assert(m_pModifiedTerrain);       // you need to call SetTerrain()

	if (ser.IsReading())
	{
		FreeData();
	}

	//	if(ser.IsReading())
	//		m_TerrainMods.clear();

	ser.Value("TerrainMods", m_TerrainMods);

	if (ser.IsReading())
	{
		uint32 dwI = 0;
		std::vector<STerrainMod>::const_iterator it, end = m_TerrainMods.end();

		for (it = m_TerrainMods.begin(); it != end; ++it, ++dwI)
		{
			const STerrainMod& ref = *it;

			MakeCrater(ref, dwI);
		}
	}
}
