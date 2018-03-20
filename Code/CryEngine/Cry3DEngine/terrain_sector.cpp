// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  File name:   terrain_sector.cpp
//  Version:     v1.00
//  Created:     28/5/2001 by Vladimir Kajalin
//  Compilers:   Visual Studio.NET
//  Description: sector initialiazilation, objects rendering
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "terrain_sector.h"
#include "terrain.h"
#include "ObjMan.h"

int CTerrainNode::GetMML(int nDist, int mmMin, int mmMax)
{
	const int nStep = 48;

	for (int i = mmMin; i < mmMax; i++)
		if (nStep << i > nDist)
			return i;

	return mmMax;
}

void CTerrainNode::FillSectorHeightMapTextureData(Array2d<float> &arrHmData)
{
	FUNCTION_PROFILER_3DENGINE;

	int nTexSize = m_pTerrain->m_texCache[2].m_nDim;
	float fBoxSize = GetBBox().GetSize().x;
	arrHmData.Allocate(nTexSize);
	for (int x = 0; x < nTexSize; x++) for (int y = 0; y < nTexSize; y++)
	{
		arrHmData[x][y] = m_pTerrain->GetZApr(
			(float)m_nOriginX + fBoxSize * float(x) / nTexSize * (1.f + 1.f / (float)nTexSize),
			(float)m_nOriginY + fBoxSize * float(y) / nTexSize * (1.f + 1.f / (float)nTexSize), 0);
	}
}

void CTerrainNode::SetLOD(const SRenderingPassInfo& passInfo)
{
	// Calculate geometry LOD
	const float fDist = m_arrfDistance[passInfo.GetRecursiveLevel()];

	if (fDist < CTerrain::GetSectorSize() + (CTerrain::GetSectorSize() >> 2))
		m_cNewGeomMML = 0;
	else
	{
		float fAllowedError = (passInfo.GetZoomFactor() * GetCVars()->e_TerrainLodRatio * fDist) / 180.f * 2.5f;

		int nGeomMML;
		for (nGeomMML = GetTerrain()->m_nUnitsToSectorBitShift - 1; nGeomMML > m_rangeInfo.nUnitBitShift; nGeomMML--)
			if (m_pGeomErrors[nGeomMML] < fAllowedError)
				break;

		m_cNewGeomMML = min(nGeomMML, int(fDist / 32));
	}

	// Calculate Texture LOD
	if (passInfo.IsGeneralPass())
		m_cNodeNewTexMML = GetTextureLOD(fDist, passInfo);
}

uint8 CTerrainNode::GetTextureLOD(float fDistance, const SRenderingPassInfo& passInfo)
{
	int nDiffTexDim = GetTerrain()->m_arrBaseTexInfos[m_nSID].m_TerrainTextureLayer[0].nSectorSizePixels;

	float fTexSizeK = nDiffTexDim ? float(nDiffTexDim) / float(GetTerrain()->GetTerrainTextureNodeSizeMeters()) : 1.f;

	int nMinLod = 0;

	if (m_pTerrain->m_texCache[0].m_eTexFormat == eTF_R8G8B8A8)
	{
		nMinLod++; // limit amount of texture data if in fall-back mode
	}

	uint8 cNodeNewTexMML = GetMML(int(fTexSizeK * 0.05f * (fDistance * passInfo.GetZoomFactor()) * GetFloatCVar(e_TerrainTextureLodRatio)), nMinLod,
	                              m_bMergeNotAllowed ? 0 : GetTerrain()->GetParentNode(m_nSID)->m_nTreeLevel);

	return cNodeNewTexMML;
}

int CTerrainNode::GetSecIndex()
{
	int nSectorSize = CTerrain::GetSectorSize() << m_nTreeLevel;
	int nSectorsTableSize = CTerrain::GetSectorsTableSize(m_nSID) >> m_nTreeLevel;
	return (m_nOriginX / nSectorSize) * nSectorsTableSize + (m_nOriginY / nSectorSize);
}