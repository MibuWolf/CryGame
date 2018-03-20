// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  File name:   terrain_compile.cpp
//  Version:     v1.00
//  Created:     15/04/2005 by Vladimir Kajalin
//  Compilers:   Visual Studio.NET
//  Description: check vis
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"

#include "terrain.h"
#include "StatObj.h"
#include "ObjMan.h"
#include "MatMan.h"
#include "VisAreas.h"
#include "ObjectsTree.h"

#define SIGC_ALIGNTOTERRAIN         BIT(0) // Deprecated
#define SIGC_USETERRAINCOLOR        BIT(1)
#define SIGC_HIDEABILITY            BIT(3)
#define SIGC_HIDEABILITYSECONDARY   BIT(4)
#define SIGC_PICKABLE               BIT(5)
#define SIGC_PROCEDURALLYANIMATED   BIT(6)
#define SIGC_CASTSHADOW             BIT(7) // Deprecated
#define SIGC_GI_MODE                BIT(8)
#define SIGC_DYNAMICDISTANCESHADOWS BIT(9)
#define SIGC_USEALPHABLENDING       BIT(10) // not used
#define SIGC_USESPRITES             BIT(11)
#define SIGC_RANDOMROTATION         BIT(12)
#define SIGC_ALLOWINDOOR            BIT(13)

// Bits 13-14 reserved for player hideability
#define SIGC_PLAYERHIDEABLE_LOWBIT    (13)
#define SIGC_PLAYERHIDEABLE_MASK      BIT(13) | BIT(14)

#define SIGC_CASTSHADOW_MINSPEC_SHIFT (15)
#define SIGC_CASTSHADOW_MINSPEC_MASK  ((END_CONFIG_SPEC_ENUM - 1) << SIGC_CASTSHADOW_MINSPEC_SHIFT)

void CTerrain::GetVegetationMaterials(std::vector<IMaterial*>*& pMatTable)
{
	if (!pMatTable)
		return;

	{
		// get vegetation objects materials
		PodArray<StatInstGroup>& rTable = GetObjManager()->m_lstStaticTypes[0];
		int nObjectsCount = rTable.size();

		// init struct values and load cgf's
		for (uint32 i = 0; i < rTable.size(); i++)
		{
			int nMaterialId = -1;
			if (rTable[i].pMaterial)
			{
				nMaterialId = CObjManager::GetItemId(pMatTable, (IMaterial*)rTable[i].pMaterial, false);
				if (nMaterialId < 0)
				{
					nMaterialId = pMatTable->size();
					pMatTable->push_back(rTable[i].pMaterial);
				}
			}
		}
	}
}

int CTerrain::GetTablesSize(SHotUpdateInfo* pExportInfo, int nSID)
{
	int nDataSize = 0;

	nDataSize += sizeof(int);

	// get brush objects table size
	std::vector<IStatObj*> brushTypes;
	std::vector<IMaterial*> usedMats;
	std::vector<IStatInstGroup*> instGroups;

	std::vector<IMaterial*>* pMatTable = &usedMats;
	GetVegetationMaterials(pMatTable);

	Get3DEngine()->m_pObjectsTree[nSID]->GenerateStatObjAndMatTables(&brushTypes, &usedMats, &instGroups, pExportInfo);
	GetVisAreaManager()->GenerateStatObjAndMatTables(&brushTypes, &usedMats, &instGroups, pExportInfo);

	nDataSize += instGroups.size() * sizeof(StatInstGroupChunk);

	nDataSize += sizeof(int);
	nDataSize += brushTypes.size() * sizeof(SNameChunk);
	brushTypes.clear();

	// get custom materials table size
	nDataSize += sizeof(int);
	nDataSize += usedMats.size() * sizeof(SNameChunk);

	return nDataSize;
}

int CTerrain::GetCompiledDataSize(SHotUpdateInfo* pExportInfo, int nSID)
{
#if !ENGINE_ENABLE_COMPILATION
	CryFatalError("serialization code removed, please enable ENGINE_ENABLE_COMPILATION in Cry3DEngine/StdAfx.h");
	return 0;
#else
	int nDataSize = 0;
	byte* pData = NULL;
	Vec3 segmentOffset(0, 0, 0);

	bool bHMap(!pExportInfo || pExportInfo->nHeigtmap);
	bool bObjs(!pExportInfo || pExportInfo->nObjTypeMask);

	if (bObjs)
	{
		assert(nSID >= 0 && nSID < Get3DEngine()->m_pObjectsTree.Count() && Get3DEngine()->m_pObjectsTree[nSID]);
		Get3DEngine()->m_pObjectsTree[nSID]->CleanUpTree();
		Get3DEngine()->m_pObjectsTree[nSID]->GetData(pData, nDataSize, NULL, NULL, NULL, eLittleEndian, pExportInfo, segmentOffset);
	}

	if (bHMap)
		GetParentNode(nSID)->GetData(pData, nDataSize, GetPlatformEndian(), pExportInfo);

	// get header size
	nDataSize += sizeof(STerrainChunkHeader);

	// get vegetation objects table size
	if (bObjs)
	{
		nDataSize += GetTablesSize(pExportInfo, nSID);
	}

	return nDataSize;
#endif
}

void CTerrain::SaveTables(byte*& pData, int& nDataSize, std::vector<struct IStatObj*>*& pStatObjTable, std::vector<IMaterial*>*& pMatTable, std::vector<IStatInstGroup*>*& pStatInstGroupTable, EEndian eEndian, SHotUpdateInfo* pExportInfo, int nSID)
{
	pMatTable = new std::vector<struct IMaterial*>;
	pStatObjTable = new std::vector<struct IStatObj*>;
	pStatInstGroupTable = new std::vector<struct IStatInstGroup*>;

	GetVegetationMaterials(pMatTable);

	Get3DEngine()->m_pObjectsTree[nSID]->GenerateStatObjAndMatTables(pStatObjTable, pMatTable, pStatInstGroupTable, pExportInfo);
	GetVisAreaManager()->GenerateStatObjAndMatTables(pStatObjTable, pMatTable, pStatInstGroupTable, pExportInfo);

	{
		{
			// get vegetation objects count
			std::vector<IStatInstGroup*>& rTable = *pStatInstGroupTable;
			int nObjectsCount = rTable.size();

			// prepare temporary chunks array for saving
			PodArray<StatInstGroupChunk> lstFileChunks;
			lstFileChunks.PreAllocate(nObjectsCount, nObjectsCount);

			// init struct values and load cgf's
			for (uint32 i = 0; i < rTable.size(); i++)
			{
				cry_strcpy(lstFileChunks[i].szFileName, rTable[i]->szFileName);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fBending);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fSpriteDistRatio);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fLodDistRatio);
				if (lstFileChunks[i].fLodDistRatio < 0.001f) // not allow to export value of 0 because it would mean that variable is not set
					lstFileChunks[i].fLodDistRatio = 0.001f;
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fShadowDistRatio);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fMaxViewDistRatio);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fBrightness);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], nRotationRangeToTerrainNormal);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fAlignToTerrainCoefficient);

				lstFileChunks[i].nFlags = 0;
				if (rTable[i]->bUseTerrainColor)
					lstFileChunks[i].nFlags |= SIGC_USETERRAINCOLOR;
				if (rTable[i]->bAllowIndoor)
					lstFileChunks[i].nFlags |= SIGC_ALLOWINDOOR;
				if (rTable[i]->bHideability)
					lstFileChunks[i].nFlags |= SIGC_HIDEABILITY;
				if (rTable[i]->bHideabilitySecondary)
					lstFileChunks[i].nFlags |= SIGC_HIDEABILITYSECONDARY;
				if (rTable[i]->bPickable)
					lstFileChunks[i].nFlags |= SIGC_PICKABLE;
				if (rTable[i]->bAutoMerged)
					lstFileChunks[i].nFlags |= SIGC_PROCEDURALLYANIMATED;
				if (rTable[i]->bGIMode)
					lstFileChunks[i].nFlags |= SIGC_GI_MODE;
				if (rTable[i]->bDynamicDistanceShadows)
					lstFileChunks[i].nFlags |= SIGC_DYNAMICDISTANCESHADOWS;
				if (rTable[i]->bUseSprites)
					lstFileChunks[i].nFlags |= SIGC_USESPRITES;
				if (rTable[i]->bRandomRotation)
					lstFileChunks[i].nFlags |= SIGC_RANDOMROTATION;

				lstFileChunks[i].nFlags |= ((rTable[i]->nCastShadowMinSpec << SIGC_CASTSHADOW_MINSPEC_SHIFT) & SIGC_CASTSHADOW_MINSPEC_MASK);
				lstFileChunks[i].nFlags |= ((rTable[i]->nPlayerHideable << SIGC_PLAYERHIDEABLE_LOWBIT) & SIGC_PLAYERHIDEABLE_MASK);

				lstFileChunks[i].nMaterialId = -1;
				if (rTable[i]->pMaterial)
				{
					lstFileChunks[i].nMaterialId = CObjManager::GetItemId(pMatTable, (IMaterial*)rTable[i]->pMaterial, false);
					if (lstFileChunks[i].nMaterialId < 0)
					{
						lstFileChunks[i].nMaterialId = pMatTable->size();
						pMatTable->push_back(rTable[i]->pMaterial);
					}
				}

				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], nMaterialLayers);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fDensity);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fElevationMax);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fElevationMin);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fSize);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fSizeVar);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fSlopeMax);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fSlopeMin);

				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], m_dwRndFlags);

				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fStiffness);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fDamping);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fVariance);
				COPY_MEMBER_SAVE(&lstFileChunks[i], rTable[i], fAirResistance);

				lstFileChunks[i].nIDPlusOne = rTable[i]->nID + 1;
			}

			// get count
			AddToPtr(pData, nDataSize, nObjectsCount, eEndian);

			// get table content
			for (int i = 0; i < lstFileChunks.Count(); i++)
				AddToPtr(pData, nDataSize, lstFileChunks[i], eEndian);

		}

		{
			// get brush objects count
			int nObjectsCount = pStatObjTable->size();
			AddToPtr(pData, nDataSize, nObjectsCount, eEndian);

			PodArray<SNameChunk> lstFileChunks;
			lstFileChunks.PreAllocate(pStatObjTable->size(), pStatObjTable->size());

			for (uint32 i = 0; i < pStatObjTable->size(); i++)
			{
				if ((*pStatObjTable)[i])
					cry_strcpy(lstFileChunks[i].szFileName, (*pStatObjTable)[i]->GetFilePath());
				else
					lstFileChunks[i].szFileName[0] = 0;
				AddToPtr(pData, nDataSize, lstFileChunks[i], eEndian);
			}
		}

		{
			// get brush materials count
			std::vector<IMaterial*>& rTable = *pMatTable;
			int nObjectsCount = rTable.size();

			// count
			AddToPtr(pData, nDataSize, nObjectsCount, eEndian);

			// get table content
			for (int dwI = 0; dwI < nObjectsCount; ++dwI)
			{
				SNameChunk tmp;
				assert(strlen(rTable[dwI] ? rTable[dwI]->GetName() : "") < sizeof(tmp.szFileName));
				cry_strcpy(tmp.szFileName, rTable[dwI] ? rTable[dwI]->GetName() : "");
				AddToPtr(pData, nDataSize, tmp, eEndian);
			}
		}
	}
}

bool CTerrain::GetCompiledData(byte* pData, int nDataSize, std::vector<struct IStatObj*>** ppStatObjTable, std::vector<IMaterial*>** ppMatTable, std::vector<struct IStatInstGroup*>** ppStatInstGroupTable, EEndian eEndian, SHotUpdateInfo* pExportInfo, int nSID, const Vec3& segmentOffset)
{
#if !ENGINE_ENABLE_COMPILATION
	CryFatalError("serialization code removed, please enable ENGINE_ENABLE_COMPILATION in Cry3DEngine/StdAfx.h");
	return false;
#else

	bool bHMap(!pExportInfo || pExportInfo->nHeigtmap);
	bool bObjs(!pExportInfo || pExportInfo->nObjTypeMask);

	//PrintMessage("Exporting terrain data (%s, %.2f MB) ...",
	//  (bHMap && bObjs) ? "Objects and heightmap" : (bHMap ? "Heightmap" : (bObjs ? "Objects" : "Nothing")), ((float)nDataSize)/1024.f/1024.f);

	// write header
	STerrainChunkHeader* pTerrainChunkHeader = (STerrainChunkHeader*)pData;
	pTerrainChunkHeader->nVersion = TERRAIN_CHUNK_VERSION;
	pTerrainChunkHeader->nDummy = 0;

	pTerrainChunkHeader->nFlags = (eEndian == eBigEndian) ? SERIALIZATION_FLAG_BIG_ENDIAN : 0;
	pTerrainChunkHeader->nFlags |= SERIALIZATION_FLAG_SECTOR_PALETTES | SERIALIZATION_FLAG_INSTANCES_PRESORTED_FOR_STREAMING;

	pTerrainChunkHeader->nFlags2 = (Get3DEngine()->m_bAreaActivationInUse ? TCH_FLAG2_AREA_ACTIVATION_IN_USE : 0);
	pTerrainChunkHeader->nChunkSize = nDataSize;
	pTerrainChunkHeader->TerrainInfo.nHeightMapSize_InUnits = m_nSectorSize * GetSectorsTableSize(nSID) / m_nUnitSize;
	pTerrainChunkHeader->TerrainInfo.nUnitSize_InMeters = m_nUnitSize;
	pTerrainChunkHeader->TerrainInfo.nSectorSize_InMeters = m_nSectorSize;
	pTerrainChunkHeader->TerrainInfo.nSectorsTableSize_InSectors = GetSectorsTableSize(nSID);
	pTerrainChunkHeader->TerrainInfo.fHeightmapZRatio = m_fHeightmapZRatio;
	pTerrainChunkHeader->TerrainInfo.fOceanWaterLevel = (m_fOceanWaterLevel > WATER_LEVEL_UNKNOWN) ? m_fOceanWaterLevel : 0;

	SwapEndian(*pTerrainChunkHeader, eEndian);
	UPDATE_PTR_AND_SIZE(pData, nDataSize, sizeof(STerrainChunkHeader));

	std::vector<struct IStatObj*>* pStatObjTable = NULL;
	std::vector<struct IMaterial*>* pMatTable = NULL;
	std::vector<struct IStatInstGroup*>* pStatInstGroupTable = NULL;

	if (bObjs)
	{
		SaveTables(pData, nDataSize, pStatObjTable, pMatTable, pStatInstGroupTable, eEndian, pExportInfo, nSID);
	}

	// get nodes data
	int nNodesLoaded = bHMap ? GetParentNode(nSID)->GetData(pData, nDataSize, eEndian, pExportInfo) : 1;

	if (bObjs)
	{
		Get3DEngine()->m_pObjectsTree[nSID]->CleanUpTree();

		Get3DEngine()->m_pObjectsTree[nSID]->GetData(pData, nDataSize, pStatObjTable, pMatTable, pStatInstGroupTable, eEndian, pExportInfo, segmentOffset);

		if (ppStatObjTable)
			*ppStatObjTable = pStatObjTable;
		else
			SAFE_DELETE(pStatObjTable);

		if (ppMatTable)
			*ppMatTable = pMatTable;
		else
			SAFE_DELETE(pMatTable);

		if (ppStatInstGroupTable)
			*ppStatInstGroupTable = pStatInstGroupTable;
		else
			SAFE_DELETE(pStatInstGroupTable);
	}

	//PrintMessagePlus(" done in %.2f sec", GetCurAsyncTimeSec()-fStartTime );

	assert(nNodesLoaded && nDataSize == 0);
	return (nNodesLoaded && nDataSize == 0);
#endif
}

void CTerrain::GetStatObjAndMatTables(DynArray<IStatObj*>* pStatObjTable, DynArray<IMaterial*>* pMatTable, DynArray<IStatInstGroup*>* pStatInstGroupTable, uint32 nObjTypeMask, int nSID)
{
	SHotUpdateInfo exportInfo;
	exportInfo.nObjTypeMask = nObjTypeMask;

	std::vector<IStatObj*> statObjTable;
	std::vector<IMaterial*> matTable;
	std::vector<IStatInstGroup*> statInstGroupTable;

	if (Get3DEngine() && Get3DEngine()->m_pObjectsTree[nSID])
		Get3DEngine()->m_pObjectsTree[nSID]->GenerateStatObjAndMatTables((pStatObjTable != NULL) ? &statObjTable : NULL,
		                                                                 (pMatTable != NULL) ? &matTable : NULL,
		                                                                 (pStatInstGroupTable != NULL) ? &statInstGroupTable : NULL,
		                                                                 &exportInfo);

	if (GetVisAreaManager())
		GetVisAreaManager()->GenerateStatObjAndMatTables((pStatObjTable != NULL) ? &statObjTable : NULL,
		                                                 (pMatTable != NULL) ? &matTable : NULL,
		                                                 (pStatInstGroupTable != NULL) ? &statInstGroupTable : NULL,
		                                                 &exportInfo);

	if (pStatObjTable)
	{
		pStatObjTable->resize(statObjTable.size());
		for (size_t i = 0; i < statObjTable.size(); ++i)
		{
			(*pStatObjTable)[i] = statObjTable[i];
		}

		statObjTable.clear();
	}

	if (pMatTable)
	{
		pMatTable->resize(matTable.size());
		for (size_t i = 0; i < matTable.size(); ++i)
		{
			(*pMatTable)[i] = matTable[i];
		}

		matTable.clear();
	}

	if (pStatInstGroupTable)
	{
		pStatInstGroupTable->resize(statInstGroupTable.size());
		for (size_t i = 0; i < statInstGroupTable.size(); ++i)
		{
			(*pStatInstGroupTable)[i] = statInstGroupTable[i];
		}

		statInstGroupTable.clear();
	}
}

void CTerrain::LoadVegetationData(PodArray<StatInstGroup>& rTable, PodArray<StatInstGroupChunk>& lstFileChunks, int i)
{
	cry_strcpy(rTable[i].szFileName, lstFileChunks[i].szFileName);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fBending);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fSpriteDistRatio);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fLodDistRatio);
	if (rTable[i].fLodDistRatio == 0)
		rTable[i].fLodDistRatio = 1.f; // set default value if it was not exported
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fShadowDistRatio);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fMaxViewDistRatio);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fBrightness);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], nRotationRangeToTerrainNormal);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fAlignToTerrainCoefficient);

	rTable[i].bUseTerrainColor = (lstFileChunks[i].nFlags & SIGC_USETERRAINCOLOR) != 0;
	rTable[i].bAllowIndoor = (lstFileChunks[i].nFlags & SIGC_ALLOWINDOOR) != 0;
	rTable[i].bHideability = (lstFileChunks[i].nFlags & SIGC_HIDEABILITY) != 0;
	rTable[i].bHideabilitySecondary = (lstFileChunks[i].nFlags & SIGC_HIDEABILITYSECONDARY) != 0;
	rTable[i].bPickable = (lstFileChunks[i].nFlags & SIGC_PICKABLE) != 0;
	rTable[i].bAutoMerged = (lstFileChunks[i].nFlags & SIGC_PROCEDURALLYANIMATED) != 0;  // && GetCVars()->e_MergedMeshes;
	rTable[i].bGIMode = (lstFileChunks[i].nFlags & SIGC_GI_MODE) != 0;
	rTable[i].bDynamicDistanceShadows = (lstFileChunks[i].nFlags & SIGC_DYNAMICDISTANCESHADOWS) != 0;
	rTable[i].bUseSprites = (lstFileChunks[i].nFlags & SIGC_USESPRITES) != 0;
	rTable[i].bRandomRotation = (lstFileChunks[i].nFlags & SIGC_RANDOMROTATION) != 0;

	int nCastShadowMinSpec = (lstFileChunks[i].nFlags & SIGC_CASTSHADOW_MINSPEC_MASK) >> SIGC_CASTSHADOW_MINSPEC_SHIFT;

	bool bCastShadowLegacy = (lstFileChunks[i].nFlags & SIGC_CASTSHADOW) != 0;     // deprecated, should be always false on re-export
	nCastShadowMinSpec = (bCastShadowLegacy && !nCastShadowMinSpec) ? CONFIG_LOW_SPEC : nCastShadowMinSpec;

	rTable[i].nCastShadowMinSpec = (nCastShadowMinSpec) ? nCastShadowMinSpec : END_CONFIG_SPEC_ENUM;

	rTable[i].nPlayerHideable = ((lstFileChunks[i].nFlags & SIGC_PLAYERHIDEABLE_MASK) >> SIGC_PLAYERHIDEABLE_LOWBIT);

	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], nMaterialLayers);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fDensity);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fElevationMax);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fElevationMin);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fSize);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fSizeVar);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fSlopeMax);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fSlopeMin);

	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fStiffness);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fDamping);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fVariance);
	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], fAirResistance);

	COPY_MEMBER_LOAD(&rTable[i], &lstFileChunks[i], m_dwRndFlags);
	rTable[i].nID = lstFileChunks[i].nIDPlusOne - 1;

	int nMinSpec = (rTable[i].m_dwRndFlags & ERF_SPEC_BITS_MASK) >> ERF_SPEC_BITS_SHIFT;
	rTable[i].minConfigSpec = (ESystemConfigSpec)nMinSpec;

	if (rTable[i].szFileName[0])
	{
		rTable[i].pStatObj.ReleaseOwnership();
		rTable[i].pStatObj = GetObjManager()->LoadStatObj(rTable[i].szFileName, NULL, NULL, !rTable[i].bAutoMerged); //,NULL,NULL,false);
	}

	rTable[i].Update(GetCVars(), Get3DEngine()->GetGeomDetailScreenRes());

#ifndef SEG_WORLD
	SLICE_AND_SLEEP();
#endif
}

template<class T>
bool CTerrain::Load_T(T*& f, int& nDataSize, STerrainChunkHeader* pTerrainChunkHeader, std::vector<struct IStatObj*>** ppStatObjTable, std::vector<IMaterial*>** ppMatTable, bool bHotUpdate, SHotUpdateInfo* pExportInfo, int nSID, Vec3 vSegmentOrigin)
{
	LOADING_TIME_PROFILE_SECTION;

	assert(pTerrainChunkHeader->nVersion == TERRAIN_CHUNK_VERSION);
	if (pTerrainChunkHeader->nVersion != TERRAIN_CHUNK_VERSION)
	{
		Error("CTerrain::SetCompiledData: version of file is %d, expected version is %d", pTerrainChunkHeader->nVersion, (int)TERRAIN_CHUNK_VERSION);
		return 0;
	}

	EEndian eEndian = (pTerrainChunkHeader->nFlags & SERIALIZATION_FLAG_BIG_ENDIAN) ? eBigEndian : eLittleEndian;
	bool bSectorPalettes = (pTerrainChunkHeader->nFlags & SERIALIZATION_FLAG_SECTOR_PALETTES) != 0;

	bool bSW = Get3DEngine()->IsSegmentOperationInProgress();
	bool bHMap(!pExportInfo || pExportInfo->nHeigtmap);
	bool bObjs(!pExportInfo || pExportInfo->nObjTypeMask);
	AABB* pBox = (pExportInfo && !pExportInfo->areaBox.IsReset()) ? &pExportInfo->areaBox : NULL;

	if (bHotUpdate)
		PrintMessage("Importing outdoor data (%s, %.2f MB) ...",
		             (bHMap && bObjs) ? "Objects and heightmap" : (bHMap ? "Heightmap" : (bObjs ? "Objects" : "Nothing")), ((float)nDataSize) / 1024.f / 1024.f);

	if (pTerrainChunkHeader->nChunkSize != nDataSize + sizeof(STerrainChunkHeader))
		return 0;

	// get terrain settings
	m_nUnitSize = pTerrainChunkHeader->TerrainInfo.nUnitSize_InMeters;
	m_fInvUnitSize = 1.f / m_nUnitSize;
	if (!Get3DEngine()->m_pSegmentsManager)
		m_nTerrainSize = pTerrainChunkHeader->TerrainInfo.nHeightMapSize_InUnits * pTerrainChunkHeader->TerrainInfo.nUnitSize_InMeters;

	m_nTerrainSizeDiv = (m_nTerrainSize >> m_nBitShift) - 1;
	m_nSectorSize = pTerrainChunkHeader->TerrainInfo.nSectorSize_InMeters;
#ifndef SEG_WORLD
	m_nSectorsTableSize = pTerrainChunkHeader->TerrainInfo.nSectorsTableSize_InSectors;
#endif
	m_fHeightmapZRatio = pTerrainChunkHeader->TerrainInfo.fHeightmapZRatio;
	m_fOceanWaterLevel = pTerrainChunkHeader->TerrainInfo.fOceanWaterLevel ? pTerrainChunkHeader->TerrainInfo.fOceanWaterLevel : WATER_LEVEL_UNKNOWN;

	if (bHotUpdate)
		Get3DEngine()->m_bAreaActivationInUse = false;
	else
		Get3DEngine()->m_bAreaActivationInUse = (pTerrainChunkHeader->nFlags2 & TCH_FLAG2_AREA_ACTIVATION_IN_USE) != 0;

	if (Get3DEngine()->IsAreaActivationInUse())
		PrintMessage("Object layers control in use");

	m_nUnitsToSectorBitShift = 0;
	while (m_nSectorSize >> m_nUnitsToSectorBitShift > m_nUnitSize)
		m_nUnitsToSectorBitShift++;

	if (bHMap && !m_pParentNodes[nSID])
	{
		LOADING_TIME_PROFILE_SECTION_NAMED("BuildSectorsTree");

		m_arrSegmentOrigns[nSID] = vSegmentOrigin;

		// build nodes tree in fast way
		BuildSectorsTree(false, nSID);

		// pass heightmap to the physics
		InitHeightfieldPhysics(nSID);
	}

	// setup physics grid
	if (!m_bEditor && !bHotUpdate && !bSW)
	{
		LOADING_TIME_PROFILE_SECTION_NAMED("SetupEntityGrid");

		int nCellSize = CTerrain::GetTerrainSize() > 2048 ? CTerrain::GetTerrainSize() >> 10 : 2;
		nCellSize = max(nCellSize, GetCVars()->e_PhysMinCellSize);
		int log2PODGridSize = 0;
		if (nCellSize == 2)
			log2PODGridSize = 2;
		else if (nCellSize == 4)
			log2PODGridSize = 1;
		GetPhysicalWorld()->SetupEntityGrid(2, Vec3(0, 0, 0), // this call will destroy all physicalized stuff
		                                    CTerrain::GetTerrainSize() / nCellSize, CTerrain::GetTerrainSize() / nCellSize, (float)nCellSize, (float)nCellSize, log2PODGridSize);
	}

	std::vector<IMaterial*>* pMatTable = NULL;
	std::vector<IStatObj*>* pStatObjTable = NULL;

	if (bObjs)
	{
		PodArray<StatInstGroupChunk> lstStatInstGroupChunkFileChunks;

		{
			// get vegetation objects count
			MEMSTAT_CONTEXT(EMemStatContextTypes::MSC_Terrain, 0, "Vegetation");
			LOADING_TIME_PROFILE_SECTION_NAMED("Vegetation");

			int nObjectsCount = 0;
			if (!LoadDataFromFile(&nObjectsCount, 1, f, nDataSize, eEndian))
				return 0;

			if (!bHotUpdate)
				PrintMessage("===== Loading %d vegetation models =====", nObjectsCount);

			// load chunks into temporary array
			PodArray<StatInstGroupChunk>& lstFileChunks = lstStatInstGroupChunkFileChunks;
			lstFileChunks.PreAllocate(nObjectsCount, nObjectsCount);
			if (!LoadDataFromFile(lstFileChunks.GetElements(), lstFileChunks.Count(), f, nDataSize, eEndian))
				return 0;

			// get vegetation objects table
			if (!m_bEditor || bHotUpdate)
			{
				// preallocate real array
				PodArray<StatInstGroup>& rTable = GetObjManager()->m_lstStaticTypes[nSID];
				rTable.resize(nObjectsCount);//,nObjectsCount);

				// init struct values and load cgf's
				for (uint32 i = 0; i < rTable.size(); i++)
				{
					LoadVegetationData(rTable, lstFileChunks, i);
				}
			}
		}

		pStatObjTable = new std::vector<IStatObj*>;

		{
			// get brush objects count
			MEMSTAT_CONTEXT(EMemStatContextTypes::MSC_Terrain, 0, "Brushes");
			LOADING_TIME_PROFILE_SECTION_NAMED("Brushes");

			int nObjectsCount = 0;
			if (!LoadDataFromFile(&nObjectsCount, 1, f, nDataSize, eEndian))
				return 0;

			if (!bHotUpdate)
				PrintMessage("===== Loading %d brush models ===== ", nObjectsCount);

			PodArray<SNameChunk> lstFileChunks;
			lstFileChunks.PreAllocate(nObjectsCount, nObjectsCount);
			if (!LoadDataFromFile(lstFileChunks.GetElements(), lstFileChunks.Count(), f, nDataSize, eEndian))
				return 0;

			// get brush objects table
			if (!m_bEditor || bHotUpdate)
			{
				pStatObjTable->resize(nObjectsCount);//PreAllocate(nObjectsCount,nObjectsCount);

				// load cgf's
				for (uint32 i = 0; i < pStatObjTable->size(); i++)
				{
					if (lstFileChunks[i].szFileName[0])
					{
						(*pStatObjTable)[i] = GetObjManager()->LoadStatObj(lstFileChunks[i].szFileName);
						(*pStatObjTable)[i]->AddRef();
					}

					SLICE_AND_SLEEP();
				}
			}
		}

		pMatTable = new std::vector<IMaterial*>;

		{
			// get brush materials count
			LOADING_TIME_PROFILE_SECTION_NAMED("BrushMaterials");

			int nObjectsCount = 0;
			if (!LoadDataFromFile(&nObjectsCount, 1, f, nDataSize, eEndian))
				return 0;

			// get vegetation objects table
			if (!m_bEditor || bHotUpdate)
			{
				if (!bHotUpdate)
					PrintMessage("===== Loading %d brush materials ===== ", nObjectsCount);

				std::vector<IMaterial*>& rTable = *pMatTable;
				rTable.clear();
				rTable.resize(nObjectsCount);//PreAllocate(nObjectsCount,nObjectsCount);

				const uint32 cTableCount = rTable.size();
				for (uint32 tableIndex = 0; tableIndex < cTableCount; ++tableIndex)
				{
					SNameChunk matName;
					if (!LoadDataFromFile(&matName, 1, f, nDataSize, eEndian))
						return 0;
					CryPathString sMtlName = matName.szFileName;
					sMtlName = PathUtil::MakeGamePath(sMtlName);
					rTable[tableIndex] = matName.szFileName[0] ? GetMatMan()->LoadMaterial(sMtlName) : NULL;
					if (rTable[tableIndex])
						rTable[tableIndex]->AddRef();

					SLICE_AND_SLEEP();
				}

				// assign real material to vegetation group
				PodArray<StatInstGroup>& rStaticTypes = GetObjManager()->m_lstStaticTypes[nSID];
				for (uint32 i = 0; i < rStaticTypes.size(); i++)
				{
					if (lstStatInstGroupChunkFileChunks[i].nMaterialId >= 0)
						rStaticTypes[i].pMaterial = rTable[lstStatInstGroupChunkFileChunks[i].nMaterialId];
					else
						rStaticTypes[i].pMaterial = NULL;
				}
			}
			else
			{
				// skip data
				SNameChunk matInfoTmp;
				for (int tableIndex = 0; tableIndex < nObjectsCount; ++tableIndex)
				{
					if (!LoadDataFromFile(&matInfoTmp, 1, f, nDataSize, eEndian))
						return 0;
				}
			}
		}
	}

	// set nodes data
	int nNodesLoaded = 0;
	{
		LOADING_TIME_PROFILE_SECTION_NAMED("TerrainNodes");

		if (!bHotUpdate)
			PrintMessage("===== Initializing terrain nodes ===== ");
		nNodesLoaded = bHMap ? GetParentNode(nSID)->Load(f, nDataSize, eEndian, bSectorPalettes, pExportInfo) : 1;
	}

	if (bObjs)
	{
		if (!bHotUpdate)
			PrintMessage("===== Loading outdoor instances ===== ");

		LOADING_TIME_PROFILE_SECTION_NAMED("ObjectInstances");

		PodArray<IRenderNode*> arrUnregisteredObjects;
		Get3DEngine()->m_pObjectsTree[nSID]->UnregisterEngineObjectsInArea(pExportInfo, arrUnregisteredObjects, true);

		// load object instances (in case of editor just check input data do no create object instances)
		int nOcNodesLoaded = 0;
		if (NULL != pExportInfo && NULL != pExportInfo->pVisibleLayerMask && NULL != pExportInfo->pLayerIdTranslation)
		{
			SLayerVisibility visInfo;
			visInfo.pLayerIdTranslation = pExportInfo->pLayerIdTranslation;
			visInfo.pLayerVisibilityMask = pExportInfo->pVisibleLayerMask;
			nOcNodesLoaded = Get3DEngine()->m_pObjectsTree[nSID]->Load(f, nDataSize, pStatObjTable, pMatTable, eEndian, pBox, &visInfo, vSegmentOrigin);
		}
		else
		{
			nOcNodesLoaded = Get3DEngine()->m_pObjectsTree[nSID]->Load(f, nDataSize, pStatObjTable, pMatTable, eEndian, pBox, NULL, vSegmentOrigin);
		}

		for (int i = 0; i < arrUnregisteredObjects.Count(); i++)
			arrUnregisteredObjects[i]->ReleaseNode();
		arrUnregisteredObjects.Reset();
	}

	if (m_bEditor && !bHotUpdate && !bSW && Get3DEngine()->m_pObjectsTree[nSID])
	{
		// editor will re-insert all objects
		AABB aabb = Get3DEngine()->m_pObjectsTree[nSID]->GetNodeBox();
		delete Get3DEngine()->m_pObjectsTree[nSID];
		Get3DEngine()->m_pObjectsTree[nSID] = COctreeNode::Create(nSID, aabb, 0);
	}

	if (bObjs)
	{
		LOADING_TIME_PROFILE_SECTION_NAMED("PostObj");

		if (ppStatObjTable)
			*ppStatObjTable = pStatObjTable;
		else
			SAFE_DELETE(pStatObjTable);

		if (ppMatTable)
			*ppMatTable = pMatTable;
		else
			SAFE_DELETE(pMatTable);
	}

	if (bHMap)
	{
		LOADING_TIME_PROFILE_SECTION_NAMED("PostTerrain");

		GetParentNode(nSID)->UpdateRangeInfoShift();
		ResetTerrainVertBuffers(NULL, nSID);
	}

	Get3DEngine()->m_bAreaActivationInUse = (pTerrainChunkHeader->nFlags2 & TCH_FLAG2_AREA_ACTIVATION_IN_USE) != 0;

	// delete neighbours corner nodes
	//UpdateSectorMeshes();

	ReleaseHeightmapGeometryAroundSegment(nSID);

	assert(nNodesLoaded && nDataSize == 0);
	return (nNodesLoaded && nDataSize == 0);
}

bool CTerrain::SetCompiledData(byte* pData, int nDataSize, std::vector<struct IStatObj*>** ppStatObjTable, std::vector<IMaterial*>** ppMatTable, bool bHotUpdate, SHotUpdateInfo* pExportInfo, int nSID, Vec3 vSegmentOrigin)
{
	STerrainChunkHeader* pTerrainChunkHeader = (STerrainChunkHeader*)pData;
	SwapEndian(*pTerrainChunkHeader, eLittleEndian);

	pData += sizeof(STerrainChunkHeader);
	nDataSize -= sizeof(STerrainChunkHeader);
	return Load_T(pData, nDataSize, pTerrainChunkHeader, ppStatObjTable, ppMatTable, bHotUpdate, pExportInfo, nSID, vSegmentOrigin);
}

bool CTerrain::Load(FILE* f, int nDataSize, STerrainChunkHeader* pTerrainChunkHeader, std::vector<struct IStatObj*>** ppStatObjTable, std::vector<IMaterial*>** ppMatTable, int nSID, Vec3 vSegmentOrigin)
{
	bool bRes;

	// in case of small data amount (console game) load entire file into memory in single operation
	if (nDataSize < 4 * 1024 * 1024 && !GetCVars()->e_StreamInstances)
	{
		_smart_ptr<IMemoryBlock> pMemBlock = gEnv->pCryPak->PoolAllocMemoryBlock(nDataSize + 8, "LoadTerrain");
		byte* pPtr = (byte*)pMemBlock->GetData();
		while (UINT_PTR(pPtr) & 3)
			pPtr++;

		if (GetPak()->FReadRaw(pPtr, 1, nDataSize, f) != nDataSize)
			return false;

		bRes = Load_T(pPtr, nDataSize, pTerrainChunkHeader, ppStatObjTable, ppMatTable, 0, 0, nSID, vSegmentOrigin);
	}
	else
	{
		// in case of big data files - load data in many small blocks
		bRes = Load_T(f, nDataSize, pTerrainChunkHeader, ppStatObjTable, ppMatTable, 0, 0, nSID, vSegmentOrigin);
	}

	if (GetParentNode(nSID))
	{
		// reopen texture file if needed, texture pack may be randomly closed by editor so automatic reopening used
		if (!m_arrBaseTexInfos[nSID].m_nDiffTexIndexTableSize)
			OpenTerrainTextureFile(m_arrBaseTexInfos[nSID].m_hdrDiffTexHdr, m_arrBaseTexInfos[nSID].m_hdrDiffTexInfo,
			                       COMPILED_TERRAIN_TEXTURE_FILE_NAME, m_arrBaseTexInfos[nSID].m_ucpDiffTexTmpBuffer, m_arrBaseTexInfos[nSID].m_nDiffTexIndexTableSize, nSID);
	}

	return bRes;
}

//////////////////////////////////////////////////////////////////////
// Segmented World
void CTerrain::GetTables(std::vector<struct IStatObj*>*& pStatObjTable, std::vector<IMaterial*>*& pMatTable, std::vector<struct IStatInstGroup*>*& pStatInstGroupTable, int nSID)
{
	SAFE_DELETE(pStatObjTable);
	SAFE_DELETE(pMatTable);
	SAFE_DELETE(pStatInstGroupTable);

	pStatObjTable = new std::vector<struct IStatObj*>;
	pMatTable = new std::vector<struct IMaterial*>;
	pStatInstGroupTable = new std::vector<struct IStatInstGroup*>;

	if (m_bEditor)
	{
		if (nSID < 0 || nSID >= Get3DEngine()->m_pObjectsTree.Count())
		{
			return;
		}

		GetVegetationMaterials(pMatTable);

		Get3DEngine()->m_pObjectsTree[nSID]->GenerateStatObjAndMatTables(pStatObjTable, pMatTable, pStatInstGroupTable, NULL);
		GetVisAreaManager()->GenerateStatObjAndMatTables(pStatObjTable, pMatTable, pStatInstGroupTable, NULL);
	}
	else
	{
		if (nSID < 0 || nSID >= (int)m_arrLoadStatuses.size())
		{
			return;
		}

		STerrainDataLoadStatus& status = m_arrLoadStatuses[nSID];
		for (size_t j = 0; j < status.statObjTable.size(); j++)
			pStatObjTable->push_back(status.statObjTable[j]);
		for (size_t k = 0; k < status.matTable.size(); k++)
			pMatTable->push_back(status.matTable[k]);
	}
}

void CTerrain::ReleaseTables(std::vector<struct IStatObj*>*& pStatObjTable, std::vector<IMaterial*>*& pMatTable, std::vector<struct IStatInstGroup*>*& pStatInstGroupTable)
{
	SAFE_DELETE(pStatObjTable);
	SAFE_DELETE(pMatTable);
	SAFE_DELETE(pStatInstGroupTable);
}

bool CTerrain::StreamCompiledData(byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	//SetCompiledData(pData, nDataSize, 0, 0, false, 0, nSID, vSegmentOrigin);
	//return false;

	if (nSID < 0 || nSID >= (int)m_arrLoadStatuses.size())
		return false;

	bool result = true;

	//Timer timer;

	STerrainDataLoadStatus& status = m_arrLoadStatuses[nSID];
	int step = status.loadingStep;
	switch (step)
	{
	case eTDLS_NotStarted:
		status.loadingStep = eTDLS_Initialize;
		break;
	case eTDLS_Initialize:
		StreamStep_Initialize(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_BuildSectorsTree:
		StreamStep_BuildSectorsTree(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_SetupEntityGrid:
		StreamStep_SetupEntityGrid(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_ReadTables:
		StreamStep_ReadTables(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_LoadTerrainNodes:
		StreamStep_LoadTerrainNodes(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_LoadVegetationStatObjs:
		StreamStep_LoadVegetationStatObjs(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_LoadBrushStatObjs:
		StreamStep_LoadBrushStatObjs(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_LoadMaterials:
		StreamStep_LoadMaterials(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_StartLoadObjectTree:
		StreamStep_StartLoadObjectTree(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_LoadObjectTree:
		StreamStep_LoadObjectTree(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_FinishUp:
		StreamStep_FinishUp(status, pData, nDataSize, nSID, vSegmentOrigin);
		break;
	case eTDLS_Complete:
	case eTDLS_Error:
	default:
		result = false;
		break;
	}
	//const char* stepNames[] =
	//{
	//	"eTDLS_NotStarted",
	//	"eTDLS_Initialize",
	//	"eTDLS_BuildSectorsTree",
	//	"eTDLS_SetupEntityGrid",
	//	"eTDLS_ReadTables",
	//	"eTDLS_LoadTerrainNodes",
	//	"eTDLS_LoadVegetationStatObjs",
	//	"eTDLS_LoadBrushStatObjs",
	//	"eTDLS_LoadMaterials",
	//	"eTDLS_StartLoadObjectTree",
	//	"eTDLS_LoadObjectTree",
	//	"eTDLS_FinishUp",
	//	"eTDLS_Complete",
	//	"eTDLS_Error"
	//};

	//if (timer.TotalElapsed() > 5)
	//	(void)0;//timer.Dumpf("%s", stepNames[step]);

	return result;
}

void CTerrain::CancelStreamCompiledData(int nSID)
{
}

void CTerrain::StreamStep_Initialize(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	byte* pOrigData = pData;

	STerrainChunkHeader* pTerrainChunkHeader = (STerrainChunkHeader*)pData;
	pData += sizeof(STerrainChunkHeader);
	nDataSize -= sizeof(STerrainChunkHeader);

	assert(pTerrainChunkHeader->nVersion == TERRAIN_CHUNK_VERSION);
	if (pTerrainChunkHeader->nVersion != TERRAIN_CHUNK_VERSION)
	{
		Error("CTerrain::SetCompiledData: version of file is %d, expected version is %d", pTerrainChunkHeader->nVersion, (int)TERRAIN_CHUNK_VERSION);
		status.loadingStep = eTDLS_Error;
		return;
	}

	EEndian eEndian = (pTerrainChunkHeader->nFlags & SERIALIZATION_FLAG_BIG_ENDIAN) ? eBigEndian : eLittleEndian;
	bool bSectorPalettes = (pTerrainChunkHeader->nFlags & SERIALIZATION_FLAG_SECTOR_PALETTES) != 0;

	if (pTerrainChunkHeader->nChunkSize != nDataSize + sizeof(STerrainChunkHeader))
	{
		status.loadingStep = eTDLS_Error;
		return;
	}

	// get terrain settings
	m_nUnitSize = pTerrainChunkHeader->TerrainInfo.nUnitSize_InMeters;
	m_fInvUnitSize = 1.f / m_nUnitSize;
	if (!Get3DEngine()->m_pSegmentsManager)
		m_nTerrainSize = pTerrainChunkHeader->TerrainInfo.nHeightMapSize_InUnits * pTerrainChunkHeader->TerrainInfo.nUnitSize_InMeters;

	m_nTerrainSizeDiv = (m_nTerrainSize >> m_nBitShift) - 1;
	m_nSectorSize = pTerrainChunkHeader->TerrainInfo.nSectorSize_InMeters;
	//m_nSectorsTableSize = pTerrainChunkHeader->TerrainInfo.nSectorsTableSize_InSectors;
	m_fHeightmapZRatio = pTerrainChunkHeader->TerrainInfo.fHeightmapZRatio;
	m_fOceanWaterLevel = pTerrainChunkHeader->TerrainInfo.fOceanWaterLevel ? pTerrainChunkHeader->TerrainInfo.fOceanWaterLevel : WATER_LEVEL_UNKNOWN;

	Get3DEngine()->m_bAreaActivationInUse = (pTerrainChunkHeader->nFlags2 & TCH_FLAG2_AREA_ACTIVATION_IN_USE) != 0;

	if (Get3DEngine()->IsAreaActivationInUse())
		PrintMessage("Object layers control in use");

	m_nUnitsToSectorBitShift = 0;
	while (m_nSectorSize >> m_nUnitsToSectorBitShift > m_nUnitSize)
		m_nUnitsToSectorBitShift++;

	status.loadingStep = eTDLS_BuildSectorsTree;
}
void CTerrain::StreamStep_BuildSectorsTree(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	if (!m_pParentNodes[nSID])
	{
		LOADING_TIME_PROFILE_SECTION_NAMED("BuildSectorsTree");

		m_arrSegmentOrigns[nSID] = vSegmentOrigin;

		// build nodes tree in fast way
		BuildSectorsTree(false, nSID);

		// pass heightmap to the physics
		SetMaterialMapping(nSID);
	}

	status.loadingStep = eTDLS_ReadTables;
}
void CTerrain::StreamStep_SetupEntityGrid(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	// setup physics grid
	if (!m_bEditor && !gEnv->p3DEngine->IsSegmentOperationInProgress())
	{
		LOADING_TIME_PROFILE_SECTION_NAMED("SetupEntityGrid");

		int nCellSize = CTerrain::GetTerrainSize() > 2048 ? CTerrain::GetTerrainSize() >> 10 : 2;
		nCellSize = max(nCellSize, GetCVars()->e_PhysMinCellSize);
		int log2PODGridSize = 0;
		if (nCellSize == 2)
			log2PODGridSize = 2;
		else if (nCellSize == 4)
			log2PODGridSize = 1;
		GetPhysicalWorld()->SetupEntityGrid(2, Vec3(0, 0, 0), // this call will destroy all physicalized stuff
		                                    CTerrain::GetTerrainSize() / nCellSize, CTerrain::GetTerrainSize() / nCellSize, (float)nCellSize, (float)nCellSize, log2PODGridSize);
	}

	status.loadingStep = eTDLS_ReadTables;
}
void CTerrain::StreamStep_ReadTables(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	STerrainChunkHeader* pTerrainChunkHeader = (STerrainChunkHeader*)pData;
	pData += sizeof(STerrainChunkHeader);
	nDataSize -= sizeof(STerrainChunkHeader);
	EEndian eEndian = (pTerrainChunkHeader->nFlags & SERIALIZATION_FLAG_BIG_ENDIAN) ? eBigEndian : eLittleEndian;

	byte* pDataStart = pData;

	// -- load vegetation table
	int nObjectsCount = 0;
	if (!LoadDataFromFile(&nObjectsCount, 1, pData, nDataSize, eEndian))
	{
		status.loadingStep = eTDLS_Error;
		return;
	}

	status.lstVegetation.PreAllocate(nObjectsCount, nObjectsCount);
	if (!LoadDataFromFile(status.lstVegetation.GetElements(), status.lstVegetation.Count(), pData, nDataSize, eEndian))
	{
		status.loadingStep = eTDLS_Error;
		return;
	}
	if (nObjectsCount)
		GetObjManager()->m_lstStaticTypes[nSID].resize(nObjectsCount);

	status.vegetationIndex = 0;

	// -- load brushes table
	if (!LoadDataFromFile(&nObjectsCount, 1, pData, nDataSize, eEndian))
	{
		status.loadingStep = eTDLS_Error;
		return;
	}

	status.lstBrushes.PreAllocate(nObjectsCount, nObjectsCount);
	if (!LoadDataFromFile(status.lstBrushes.GetElements(), status.lstBrushes.Count(), pData, nDataSize, eEndian))
	{
		status.loadingStep = eTDLS_Error;
		return;
	}
	if (nObjectsCount)
		status.statObjTable.resize(nObjectsCount);

	status.brushesIndex = 0;

	// -- load materials table
	if (!LoadDataFromFile(&nObjectsCount, 1, pData, nDataSize, eEndian))
	{
		status.loadingStep = eTDLS_Error;
		return;
	}

	status.lstMaterials.PreAllocate(nObjectsCount, nObjectsCount);
	if (!LoadDataFromFile(status.lstMaterials.GetElements(), status.lstMaterials.Count(), pData, nDataSize, eEndian))
	{
		status.loadingStep = eTDLS_Error;
		return;
	}
	if (nObjectsCount)
		status.matTable.resize(nObjectsCount);

	status.materialsIndex = 0;

	status.loadingStep = eTDLS_LoadTerrainNodes;
	status.offset = pData - pDataStart;
}
void CTerrain::StreamStep_LoadTerrainNodes(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	STerrainChunkHeader* pTerrainChunkHeader = (STerrainChunkHeader*)pData;
	pData += sizeof(STerrainChunkHeader);
	nDataSize -= sizeof(STerrainChunkHeader);
	EEndian eEndian = (pTerrainChunkHeader->nFlags & SERIALIZATION_FLAG_BIG_ENDIAN) ? eBigEndian : eLittleEndian;
	bool bSectorPalettes = (pTerrainChunkHeader->nFlags & SERIALIZATION_FLAG_SECTOR_PALETTES) != 0;

	byte* pDataStart = pData;
	pData += status.offset;

	ReleaseHeightmapGeometryAroundSegment(nSID);

	GetParentNode(nSID)->Load(pData, nDataSize, eEndian, bSectorPalettes, 0);
	GetParentNode(nSID)->UpdateRangeInfoShift();
	ResetTerrainVertBuffers(NULL, nSID);

	status.offset = pData - pDataStart;
	status.loadingStep = eTDLS_LoadVegetationStatObjs;
}
void CTerrain::StreamStep_LoadVegetationStatObjs(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	if (status.vegetationIndex >= status.lstVegetation.Count())
	{
		status.loadingStep = eTDLS_LoadBrushStatObjs;
		return;
	}

	int i = status.vegetationIndex;
	PodArray<StatInstGroup>& rTable = GetObjManager()->m_lstStaticTypes[nSID];

	LoadVegetationData(rTable, status.lstVegetation, i);

	++status.vegetationIndex;
}
void CTerrain::StreamStep_LoadBrushStatObjs(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	if (status.brushesIndex >= status.lstBrushes.Count())
	{
		status.loadingStep = eTDLS_LoadMaterials;
		return;
	}

	if (status.lstBrushes[status.brushesIndex].szFileName[0])
		status.statObjTable[status.brushesIndex] = GetObjManager()->LoadStatObj(status.lstBrushes[status.brushesIndex].szFileName, NULL, NULL, true, 0, 0, 0, m_arrSegmentPaths[nSID]);
	++status.brushesIndex;
}
void CTerrain::StreamStep_LoadMaterials(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	if (status.materialsIndex >= status.lstMaterials.Count())
	{
		status.loadingStep = eTDLS_StartLoadObjectTree;
		return;
	}

	SNameChunk& matName = status.lstMaterials[status.materialsIndex];
	status.matTable[status.materialsIndex] = matName.szFileName[0] ? GetMatMan()->LoadMaterial(matName.szFileName) : NULL;
	if (status.matTable[status.materialsIndex])
		status.matTable[status.materialsIndex]->AddRef();
	++status.materialsIndex;

	if (status.materialsIndex >= status.lstMaterials.Count())
	{
		// assign real material to vegetation group
		PodArray<StatInstGroup>& rStaticTypes = GetObjManager()->m_lstStaticTypes[nSID];
		for (uint32 i = 0; i < rStaticTypes.size(); i++)
		{
			if (status.lstVegetation[i].nMaterialId >= 0)
				rStaticTypes[i].pMaterial = status.matTable[status.lstVegetation[i].nMaterialId];
			else
				rStaticTypes[i].pMaterial = NULL;
		}
	}
}
void CTerrain::StreamStep_StartLoadObjectTree(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	Get3DEngine()->m_pObjectsTree[nSID]->UnregisterEngineObjectsInArea(0, status.arrUnregisteredObjects, true);
	status.loadingStep = eTDLS_LoadObjectTree;
}

void CTerrain::StreamStep_LoadObjectTree(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	STerrainChunkHeader* pTerrainChunkHeader = (STerrainChunkHeader*)pData;
	pData += sizeof(STerrainChunkHeader);
	nDataSize -= sizeof(STerrainChunkHeader);
	EEndian eEndian = (pTerrainChunkHeader->nFlags & SERIALIZATION_FLAG_BIG_ENDIAN) ? eBigEndian : eLittleEndian;
	bool bSectorPalettes = (pTerrainChunkHeader->nFlags & SERIALIZATION_FLAG_SECTOR_PALETTES) != 0;

	byte* pDataStart = pData;
	pData += status.offset;

	// load object instances (in case of editor just check input data do no create object instances)
	bool stillStreaming = Get3DEngine()->m_pObjectsTree[nSID]->StreamLoad(pData, nDataSize, &status.statObjTable, &status.matTable, eEndian, 0);

	if (!stillStreaming)
	{
		for (int i = 0; i < status.arrUnregisteredObjects.Count(); i++)
			status.arrUnregisteredObjects[i]->ReleaseNode();
		status.arrUnregisteredObjects.Reset();

		for (int i = 0; i < (int)status.matTable.size(); ++i)
			if (status.matTable[i])
				status.matTable[i]->Release();

		status.loadingStep = eTDLS_FinishUp;
	}
}
void CTerrain::StreamStep_FinishUp(STerrainDataLoadStatus& status, byte* pData, int nDataSize, int nSID, const Vec3& vSegmentOrigin)
{
	if (GetParentNode(nSID))
	{
		// reopen texture file if needed, texture pack may be randomly closed by editor so automatic reopening used
		if (!m_arrBaseTexInfos[nSID].m_nDiffTexIndexTableSize)
			OpenTerrainTextureFile(m_arrBaseTexInfos[nSID].m_hdrDiffTexHdr, m_arrBaseTexInfos[nSID].m_hdrDiffTexInfo,
			                       m_arrSegmentPaths[nSID] + COMPILED_TERRAIN_TEXTURE_FILE_NAME, m_arrBaseTexInfos[nSID].m_ucpDiffTexTmpBuffer, m_arrBaseTexInfos[nSID].m_nDiffTexIndexTableSize, nSID);
	}

	status.Clear();
	status.loadingStep = eTDLS_Complete;
}

#include <CryCore/TypeInfo_impl.h>
#include "terrain_compile_info.h"
