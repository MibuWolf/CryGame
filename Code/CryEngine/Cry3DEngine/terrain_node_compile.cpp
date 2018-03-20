// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  File name:   terrain_node.cpp
//  Version:     v1.00
//  Created:     28/5/2001 by Vladimir Kajalin
//  Compilers:   Visual Studio.NET
//  Description: terrain node
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "terrain_sector.h"
#include "terrain.h"
#include "ObjMan.h"
#include "VisAreas.h"

#define TERRAIN_NODE_CHUNK_VERSION 7

int CTerrainNode::Load(uint8*& f, int& nDataSize, EEndian eEndian, bool bSectorPalettes, SHotUpdateInfo* pExportInfo) { return Load_T(f, nDataSize, eEndian, bSectorPalettes, pExportInfo); }
int CTerrainNode::Load(FILE*& f, int& nDataSize, EEndian eEndian, bool bSectorPalettes, SHotUpdateInfo* pExportInfo)  { return Load_T(f, nDataSize, eEndian, bSectorPalettes, pExportInfo); }

int CTerrainNode::FTell(uint8*& f)                                                                                    { return -1; }
int CTerrainNode::FTell(FILE*& f)                                                                                     { return GetPak()->FTell(f); }

template<class T>
int CTerrainNode::Load_T(T*& f, int& nDataSize, EEndian eEndian, bool bSectorPalettes, SHotUpdateInfo* pExportInfo)
{
	const AABB* pAreaBox = (pExportInfo && !pExportInfo->areaBox.IsReset()) ? &pExportInfo->areaBox : NULL;

	if (pAreaBox && !Overlap::AABB_AABB(GetBBox(), *pAreaBox))
		return 0;

	// set node data
	STerrainNodeChunk chunk;
	if (!CTerrain::LoadDataFromFile(&chunk, 1, f, nDataSize, eEndian))
		return 0;
	assert(chunk.nChunkVersion >= 5 && chunk.nChunkVersion <= TERRAIN_NODE_CHUNK_VERSION);
	if (chunk.nChunkVersion < 5 || chunk.nChunkVersion > TERRAIN_NODE_CHUNK_VERSION)
		return 0;

	// set error levels, bounding boxes and some flags
	m_boxHeigtmapLocal = chunk.boxHeightmap;
	m_boxHeigtmapLocal.max.z = max(m_boxHeigtmapLocal.max.z, GetTerrain()->GetWaterLevel());
	m_bHasHoles = chunk.bHasHoles;
	m_rangeInfo.fOffset = chunk.fOffset;
	m_rangeInfo.fRange = chunk.fRange;
	m_rangeInfo.nSize = chunk.nSize;
	SAFE_DELETE_ARRAY(m_rangeInfo.pHMData);
	m_rangeInfo.pSTPalette = NULL;
	m_rangeInfo.UpdateBitShift(GetTerrain()->m_nUnitsToSectorBitShift);
	m_rangeInfo.nModified = false;

	{
		float fMin = m_rangeInfo.fOffset;
		float fMax = m_rangeInfo.fOffset + 0xFFF0 * m_rangeInfo.fRange;

		const unsigned mask12bit = (1 << 12) - 1;
		const int inv5cm = 20;

		int iOffset = int(fMin * inv5cm);
		int iRange = int((fMax - fMin) * inv5cm);
		int iStep = iRange ? (iRange + mask12bit - 1) / mask12bit : 1;

		iRange /= iStep;

		m_rangeInfo.iOffset = iOffset;
		m_rangeInfo.iRange = iRange;
		m_rangeInfo.iStep = iStep;
	}

	m_nNodeHMDataOffset = -1;
	if (m_rangeInfo.nSize)
	{
		m_rangeInfo.pHMData = new uint16[((m_rangeInfo.nSize * m_rangeInfo.nSize + 7) / 8) * 8];  // Align up to 16 bytes for console unaligned vector loads
		m_nNodeHMDataOffset = FTell(f);
		if (!CTerrain::LoadDataFromFile(m_rangeInfo.pHMData, m_rangeInfo.nSize * m_rangeInfo.nSize, f, nDataSize, eEndian))
			return 0;
		if (chunk.nChunkVersion != 5)
			CTerrain::LoadDataFromFile_FixAllignemt(f, nDataSize);

		if (chunk.nChunkVersion < 7)
		{
			RescaleToInt();
		}
	}

	// load LOD errors
	delete[] m_pGeomErrors;
	m_pGeomErrors = new float[GetTerrain()->m_nUnitsToSectorBitShift];
	if (!CTerrain::LoadDataFromFile(m_pGeomErrors, GetTerrain()->m_nUnitsToSectorBitShift, f, nDataSize, eEndian))
		return 0;
	assert(m_pGeomErrors[0] == 0);

	// load used surf types
	for (int i = 0; i < m_lstSurfaceTypeInfo.Count(); i++)
		m_lstSurfaceTypeInfo[i].DeleteRenderMeshes(GetRenderer());
	m_lstSurfaceTypeInfo.Clear();

	m_lstSurfaceTypeInfo.PreAllocate(chunk.nSurfaceTypesNum, chunk.nSurfaceTypesNum);

	if (chunk.nSurfaceTypesNum)
	{
		uint8* pTypes = new uint8[chunk.nSurfaceTypesNum];  // TODO: avoid temporary allocations

		if (!CTerrain::LoadDataFromFile(pTypes, chunk.nSurfaceTypesNum, f, nDataSize, eEndian))
			return 0;

		for (int i = 0; i < m_lstSurfaceTypeInfo.Count() && i < SRangeInfo::e_hole; i++)
			m_lstSurfaceTypeInfo[i].pSurfaceType = &GetTerrain()->m_SSurfaceType[m_nSID][min((int)pTypes[i], (int)SRangeInfo::e_undefined)];

		if (!m_pChilds)
		{
			// For leaves, we reconstruct the sector's surface type palette from the surface type array.
			m_rangeInfo.pSTPalette = new uchar[SRangeInfo::e_palette_size];
			for (int i = 0; i < SRangeInfo::e_index_hole; i++)
				m_rangeInfo.pSTPalette[i] = SRangeInfo::e_undefined;

			m_rangeInfo.pSTPalette[SRangeInfo::e_index_hole] = SRangeInfo::e_hole;

			// Check which palette entries are actually used in the sector. We need to map all of them to global IDs.
			int nDataCount = m_rangeInfo.nSize * (m_rangeInfo.nSize - 1);
			int nUsedPaletteEntriesCount = 0;
			bool UsedPaletteEntries[SRangeInfo::e_palette_size];
			memset(UsedPaletteEntries, 0, sizeof(UsedPaletteEntries));
			if (m_rangeInfo.pHMData)
			{
				for (int i = 0; i < nDataCount; i++)
				{
					if ((i + 1) % m_rangeInfo.nSize)
						UsedPaletteEntries[m_rangeInfo.GetSurfaceTypeByIndex(i)] = true;
				}
			}

			for (int i = 0; i < SRangeInfo::e_index_undefined; i++)
			{
				if (UsedPaletteEntries[i])
					nUsedPaletteEntriesCount++;
			}

			// Get the first nUsedPaletteEntriesCount entries from m_lstSurfaceTypeInfo and put them into the used entries
			// of the palette, skipping the unused entries.
			int nCurrentListEntry = 0;
			assert(nUsedPaletteEntriesCount <= m_lstSurfaceTypeInfo.Count());
			if (m_rangeInfo.pHMData && bSectorPalettes)
			{
				for (int i = 0; i < SRangeInfo::e_index_undefined; i++)
				{
					if (UsedPaletteEntries[i])
					{
						m_rangeInfo.pSTPalette[i] = pTypes[nCurrentListEntry];
						//!!! shielding a problem
						if (nCurrentListEntry + 1 < chunk.nSurfaceTypesNum)
							++nCurrentListEntry;
					}
				}
			}
			else
				// In old levels, no palette had been saved, so create a trivial palette (trying to decode will yield garbage).
				for (int i = 0; i < SRangeInfo::e_index_undefined; i++)
				{
					if (UsedPaletteEntries[i])
						m_rangeInfo.pSTPalette[i] = i;
				}
		}

		delete[] pTypes;

		if (chunk.nChunkVersion != 5)
			CTerrain::LoadDataFromFile_FixAllignemt(f, nDataSize);
	}

	// count number of nodes saved
	int nNodesNum = 1;

	// process childs
	for (int i = 0; m_pChilds && i < 4; i++)
		nNodesNum += m_pChilds[i].Load_T(f, nDataSize, eEndian, bSectorPalettes, pExportInfo);

	return nNodesNum;
}

void CTerrainNode::RescaleToInt()
{
	if (m_rangeInfo.nSize && m_rangeInfo.pHMData)
	{
		const float fRange = m_rangeInfo.fRange;
		const float fMin = m_rangeInfo.fOffset;

		const int step = m_rangeInfo.iStep;
		const int inv5cm = 20;

		uint16* hmap = m_rangeInfo.pHMData;

		for (int i = 0, size = m_rangeInfo.nSize * m_rangeInfo.nSize; i < size; i++)
		{
			uint16 hraw = hmap[i];
			float height = fMin + (0xFFF0 & hraw) * fRange;
			uint16 hdec = int((height - fMin) * inv5cm) / step;

			hmap[i] = (hraw & 0xF) | (hdec << 4);
		}
	}
}

void CTerrainNode::ReleaseHoleNodes()
{
	if (!m_pChilds)
		return;

	if (m_bHasHoles == 2)
	{
		SAFE_DELETE_ARRAY(m_pChilds);
	}
	else
	{
		for (int i = 0; i < 4; i++)
			m_pChilds[i].ReleaseHoleNodes();
	}
}

int CTerrainNode::ReloadModifiedHMData(FILE* f)
{
	if (m_rangeInfo.nSize && m_nNodeHMDataOffset >= 0 && m_rangeInfo.nModified)
	{
		m_rangeInfo.nModified = false;

		GetPak()->FSeek(f, m_nNodeHMDataOffset, SEEK_SET);
		int nDataSize = m_rangeInfo.nSize * m_rangeInfo.nSize * sizeof(m_rangeInfo.pHMData[0]);
		if (!CTerrain::LoadDataFromFile(m_rangeInfo.pHMData, m_rangeInfo.nSize * m_rangeInfo.nSize, f, nDataSize, m_pTerrain->m_eEndianOfTexture))
			return 0;
	}

	// process childs
	for (int i = 0; m_pChilds && i < 4; i++)
		m_pChilds[i].ReloadModifiedHMData(f);

	return 1;
}

int CTerrainNode::GetData(byte*& pData, int& nDataSize, EEndian eEndian, SHotUpdateInfo* pExportInfo)
{
	const AABB* pAreaBox = (pExportInfo && !pExportInfo->areaBox.IsReset()) ? &pExportInfo->areaBox : NULL;

	AABB boxWS = GetBBox();

	if (pAreaBox && !Overlap::AABB_AABB(boxWS, *pAreaBox))
		return 0;

	if (pData)
	{
		// get node data
		STerrainNodeChunk* pCunk = (STerrainNodeChunk*)pData;
		pCunk->nChunkVersion = TERRAIN_NODE_CHUNK_VERSION;
#ifdef SEG_WORLD
		pCunk->boxHeightmap = this->m_boxHeigtmapLocal;
#else
		pCunk->boxHeightmap = boxWS;
#endif
		pCunk->bHasHoles = m_bHasHoles;
		pCunk->fOffset = m_rangeInfo.fOffset;
		pCunk->fRange = m_rangeInfo.fRange;
		pCunk->nSize = m_rangeInfo.nSize;
		pCunk->nSurfaceTypesNum = m_lstSurfaceTypeInfo.Count();

		SwapEndian(*pCunk, eEndian);
		UPDATE_PTR_AND_SIZE(pData, nDataSize, sizeof(STerrainNodeChunk));

		// get heightmap data
		AddToPtr(pData, nDataSize, m_rangeInfo.pHMData, m_rangeInfo.nSize * m_rangeInfo.nSize, eEndian, true);
		//		memcpy(pData, m_rangeInfo.pHMData, nBlockSize);
		//	UPDATE_PTR_AND_SIZE(pData,nDataSize,nBlockSize);

		// get heightmap error data
		AddToPtr(pData, nDataSize, m_pGeomErrors, GetTerrain()->m_nUnitsToSectorBitShift, eEndian);
		//		memcpy(pData, m_pGeomErrors, GetTerrain()->m_nUnitsToSectorBitShift*sizeof(m_pGeomErrors[0]));
		//	UPDATE_PTR_AND_SIZE(pData,nDataSize,GetTerrain()->m_nUnitsToSectorBitShift*sizeof(m_pGeomErrors[0]));

		// get used surf types
		CRY_ASSERT(m_lstSurfaceTypeInfo.Count() <= SRangeInfo::e_max_surface_types);
		uint8 arrTmp[SRangeInfo::e_max_surface_types] = { SRangeInfo::e_index_undefined };
		for (int i = 0; i < SRangeInfo::e_max_surface_types && i < m_lstSurfaceTypeInfo.Count(); i++)
			arrTmp[i] = m_lstSurfaceTypeInfo[i].pSurfaceType->ucThisSurfaceTypeId;

		// For a leaf sector, store its surface type palette.
		if (m_rangeInfo.pSTPalette)
		{
			assert(m_rangeInfo.pSTPalette[SRangeInfo::e_index_hole] == SRangeInfo::e_hole);

			// To ensure correct reconstruction of the sector's palette on loading, all entries present in the palette
			// must be *in the beginning* of arrTmp, *in the same order as in the palette*. The palette should not contain
			// any entries that are not included in arrTmp.

			// Check which palette entries are actually used in the sector. Editing can leave unused palette entries.
			// The check omits the last row and the last column of the sector (m_lstSurfaceTypeInfo does not include them).
			int nDataCount = m_rangeInfo.nSize * (m_rangeInfo.nSize - 1);
			int nUsedPaletteEntrisCount = 0;
			bool UsedPaletteEntries[SRangeInfo::e_palette_size];
			memset(UsedPaletteEntries, 0, sizeof(UsedPaletteEntries));
			for (int i = 0; i < nDataCount; i++)
			{
				if ((i + 1) % m_rangeInfo.nSize) // Ignore sector's last column
					UsedPaletteEntries[m_rangeInfo.GetSurfaceTypeByIndex(i)] = true;
			}

			// Clear any palette entries whose indices do not occur in the data (set them to 127). They are not present in
			// m_lstSurfaceTypeInfo/arrTmp, so they cannot be stored (and there is no reason to store them anyway).
			for (int i = 0; i < SRangeInfo::e_index_undefined; i++)
			{
				if (!UsedPaletteEntries[i])
					m_rangeInfo.pSTPalette[i] = SRangeInfo::e_undefined;
				else
				{
					assert(m_rangeInfo.pSTPalette[i] != SRangeInfo::e_hole);
					nUsedPaletteEntrisCount++;
				}
			}

			assert(nUsedPaletteEntrisCount <= m_lstSurfaceTypeInfo.Count());

			// Reorder arrTmp so that the palette's entries are in the correct order in the beginning of the list.
			int nTargetListEntry = 0;
			for (int i = 0; i < SRangeInfo::e_index_undefined; i++)
			{
				if (UsedPaletteEntries[i])
				{
					assert(m_rangeInfo.pSTPalette[i] != SRangeInfo::e_hole);
					// Take the current palette entry and find the corresponding entry in arrTmp.
					int nCurrentListEntry = -1;
					for (int j = 0; nCurrentListEntry < 0 && j < SRangeInfo::e_index_undefined && j < m_lstSurfaceTypeInfo.Count(); j++)
					{
						if (arrTmp[j] == m_rangeInfo.pSTPalette[i])
							nCurrentListEntry = j;
					}

					// Swap the entries in arrTmp to move the entry where we want it.
					assert(nCurrentListEntry >= 0);
					PREFAST_ASSUME(nCurrentListEntry >= 0);
					if (nCurrentListEntry != nTargetListEntry)
						std::swap(arrTmp[nCurrentListEntry], arrTmp[nTargetListEntry]);
					nTargetListEntry++;
				}
			}
		}

		AddToPtr(pData, nDataSize, arrTmp, m_lstSurfaceTypeInfo.Count(), eEndian, true);
		//		memcpy(pData, arrTmp, m_lstSurfaceTypeInfo.Count()*sizeof(uint8));
		//	UPDATE_PTR_AND_SIZE(pData,nDataSize,m_lstSurfaceTypeInfo.Count()*sizeof(uint8));
	}
	else // just count size
	{
		nDataSize += sizeof(STerrainNodeChunk);
		nDataSize += m_rangeInfo.nSize * m_rangeInfo.nSize * sizeof(m_rangeInfo.pHMData[0]);
		while (nDataSize & 3)
			nDataSize++;
		nDataSize += GetTerrain()->m_nUnitsToSectorBitShift * sizeof(m_pGeomErrors[0]);
		nDataSize += m_lstSurfaceTypeInfo.Count() * sizeof(uint8);
		while (nDataSize & 3)
			nDataSize++;
	}

	// count number of nodes saved
	int nNodesNum = 1;

	// process childs
	for (int i = 0; m_pChilds && i < 4; i++)
		nNodesNum += m_pChilds[i].GetData(pData, nDataSize, eEndian, pExportInfo);

	return nNodesNum;
}
