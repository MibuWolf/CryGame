// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  File name:   terran_edit.cpp
//  Version:     v1.00
//  Created:     28/5/2001 by Vladimir Kajalin
//  Compilers:   Visual Studio.NET
//  Description: add/remove static objects, modify hmap (used by editor)
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"

#include "terrain.h"
#include "terrain_sector.h"
#include "StatObj.h"
#include "ObjMan.h"
#include "3dEngine.h"
#include "Vegetation.h"
#include "PolygonClipContext.h"
#include "RoadRenderNode.h"
#include "MergedMeshRenderNode.h"
#include "MergedMeshGeometry.h"
#include "Brush.h"
#include "DecalRenderNode.h"
#include "WaterVolumeRenderNode.h"
#include <CryEntitySystem/IEntitySystem.h>

#define RAD2BYTE(x) ((x)* 255.0f / float(g_PI2))
#define BYTE2RAD(x) ((x)* float(g_PI2) / 255.0f)

//////////////////////////////////////////////////////////////////////////
IRenderNode* CTerrain::AddVegetationInstance(int nStaticGroupIndex, const Vec3& vPos, const float fScale, uint8 ucBright,
                                             uint8 angle, uint8 angleX, uint8 angleY, int nSID)
{
	if (vPos.x <= 0 || vPos.y <= 0 || vPos.x >= CTerrain::GetTerrainSize() || vPos.y >= CTerrain::GetTerrainSize() || fScale * VEGETATION_CONV_FACTOR < 1.f)
		return 0;
	IRenderNode* renderNode = NULL;

	assert(nSID >= 0 && nSID < GetObjManager()->m_lstStaticTypes.Count());

	if (nStaticGroupIndex < 0 || nStaticGroupIndex >= GetObjManager()->m_lstStaticTypes[nSID].Count())
	{
		return 0;
	}

	StatInstGroup& group = GetObjManager()->m_lstStaticTypes[nSID][nStaticGroupIndex];
	if (!group.GetStatObj())
	{
		Warning("I3DEngine::AddStaticObject: Attempt to add object of undefined type");
		return 0;
	}

	bool bValidForMerging = group.GetStatObj()->GetRenderTrisCount() < GetCVars()->e_MergedMeshesMaxTriangles;

	if (!group.bAutoMerged || !bValidForMerging)
	{
		CVegetation* pEnt = (CVegetation*)Get3DEngine()->CreateRenderNode(eERType_Vegetation);
		pEnt->SetScale(fScale);
		pEnt->m_vPos = vPos;
		pEnt->SetStatObjGroupIndex(nStaticGroupIndex);
		pEnt->m_ucAngle = angle;
		pEnt->m_ucAngleX = angleX;
		pEnt->m_ucAngleY = angleY;
		pEnt->CalcBBox();

		float fEntLengthSquared = pEnt->GetBBox().GetSize().GetLengthSquared();
		if (fEntLengthSquared > MAX_VALID_OBJECT_VOLUME || !_finite(fEntLengthSquared) || fEntLengthSquared <= 0)
		{
			Warning("CTerrain::AddVegetationInstance: Object has invalid bbox: %s,%s, GetRadius() = %.2f",
			        pEnt->GetName(), pEnt->GetEntityClassName(), sqrt_tpl(fEntLengthSquared) * 0.5f);
		}

		if (group.bAutoMerged && !bValidForMerging)
		{
			static int nLastFrameId = 0;
			if (nLastFrameId != gEnv->pRenderer->GetFrameID()) // log spam prevention
			{
				Warning("%s: Vegetation object is not suitable for merging because of too many polygons: %s (%d triangles)", __FUNCTION__, group.GetStatObj()->GetFilePath(), group.GetStatObj()->GetRenderTrisCount());
				nLastFrameId = gEnv->pRenderer->GetFrameID();
			}
		}

		pEnt->Physicalize();
		Get3DEngine()->RegisterEntity(pEnt);
		renderNode = pEnt;
	}
	else
	{
		SProcVegSample sample;
		sample.InstGroupId = nStaticGroupIndex;
		sample.pos = vPos;
		sample.scale = (uint8)SATURATEB(fScale * VEGETATION_CONV_FACTOR);
		Matrix33 mr = Matrix33::CreateRotationXYZ(Ang3(BYTE2RAD(angleX), BYTE2RAD(angleY), BYTE2RAD(angle)));

		if (group.GetAlignToTerrainAmount() != 0.f)
		{
			Matrix33 m33;
			GetTerrain()->GetTerrainAlignmentMatrix(vPos, group.GetAlignToTerrainAmount(), m33);
			sample.q = Quat(m33) * Quat(mr);
		}
		else
		{
			sample.q = Quat(mr);
		}
		sample.q.NormalizeSafe();
		renderNode = m_pMergedMeshesManager->AddInstance(sample);
	}

	return renderNode;
}

void CTerrain::RemoveAllStaticObjects(int nSID)
{
#ifdef SEG_WORLD
	if (nSID < 0)
	{
		for (nSID = 0; nSID < Get3DEngine()->m_pObjectsTree.Count(); ++nSID)
			RemoveAllStaticObjects(nSID);
		return;
	}
#endif

	if (!Get3DEngine()->m_pObjectsTree[nSID] || nSID >= Get3DEngine()->m_pObjectsTree.Count())
		return;

	PodArray<SRNInfo> lstObjects;
	Get3DEngine()->m_pObjectsTree[nSID]->MoveObjectsIntoList(&lstObjects, NULL);

	for (int i = 0; i < lstObjects.Count(); i++)
	{
		IRenderNode* pNode = lstObjects.GetAt(i).pNode;
		switch (pNode->GetRenderNodeType())
		{
		case eERType_Vegetation:
			if (!(pNode->GetRndFlags() & ERF_PROCEDURAL))
				pNode->ReleaseNode();
			break;
		case eERType_MergedMesh:
			pNode->ReleaseNode();
			break;
		}
	}
}

#define GET_Z_VAL(_x, _y) pTerrainBlock[(_x) * nTerrainSize + (_y)]

void CTerrain::BuildErrorsTableForArea(float* pLodErrors, int nMaxLods,
                                       int X1, int Y1, int X2, int Y2, float* pTerrainBlock,
                                       uint8* pSurfaceData, int nSurfOffsetX, int nSurfOffsetY,
                                       int nSurfSizeX, int nSurfSizeY, bool& bHasHoleEdges)
{
	memset(pLodErrors, 0, nMaxLods * sizeof(pLodErrors[0]));
	int nSectorSize = CTerrain::GetSectorSize() / CTerrain::GetHeightMapUnitSize();
	int nTerrainSize = CTerrain::GetTerrainSize() / CTerrain::GetHeightMapUnitSize();

	bool bSectorHasHoles = false;
	bool bSectorHasMesh = false;

	{
		int nLodUnitSize = 1;
		int x1 = max(0, X1 - nLodUnitSize);
		int x2 = min(nTerrainSize - nLodUnitSize, X2 + nLodUnitSize);
		int y1 = max(0, Y1 - nLodUnitSize);
		int y2 = min(nTerrainSize - nLodUnitSize, Y2 + nLodUnitSize);

		for (int X = x1; X < x2; X += nLodUnitSize)
		{
			for (int Y = y1; Y < y2; Y += nLodUnitSize)
			{
				int nSurfX = (X - nSurfOffsetX);
				int nSurfY = (Y - nSurfOffsetY);
				if (nSurfX >= 0 && nSurfY >= 0 && nSurfX < nSurfSizeX && nSurfY < nSurfSizeY && pSurfaceData)
				{
					int nSurfCell = nSurfX * nSurfSizeY + nSurfY;
					assert(nSurfCell >= 0 && nSurfCell < nSurfSizeX * nSurfSizeY);
					if (pSurfaceData[nSurfCell] == SRangeInfo::e_hole)
						bSectorHasHoles = true;
					else
						bSectorHasMesh = true;
				}
			}
		}
	}

	bHasHoleEdges = (bSectorHasHoles && bSectorHasMesh);

	for (int nLod = 1; nLod < nMaxLods; nLod++)
	{
		// calculate max difference between detail levels and actual height map
		float fMaxDiff = 0;

		if (bHasHoleEdges)
			fMaxDiff = max(fMaxDiff, GetFloatCVar(e_TerrainLodRatioHolesMin));

		int nLodUnitSize = (1 << nLod);

		assert(nLodUnitSize <= nSectorSize);

		int x1 = max(0, X1 - nLodUnitSize);
		int x2 = min(nTerrainSize - nLodUnitSize, X2 + nLodUnitSize);
		int y1 = max(0, Y1 - nLodUnitSize);
		int y2 = min(nTerrainSize - nLodUnitSize, Y2 + nLodUnitSize);

		float fSurfSwitchError = 0.5f * nLod;

		for (int X = x1; X < x2; X += nLodUnitSize)
		{
			for (int Y = y1; Y < y2; Y += nLodUnitSize)
			{
				uint8 nLodedSurfType = 0;
				{
					int nSurfX = (X - nSurfOffsetX);
					int nSurfY = (Y - nSurfOffsetY);
					if (nSurfX >= 0 && nSurfY >= 0 && nSurfX < nSurfSizeX && nSurfY < nSurfSizeY && pSurfaceData)
					{
						int nSurfCell = nSurfX * nSurfSizeY + nSurfY;
						assert(nSurfCell >= 0 && nSurfCell < nSurfSizeX * nSurfSizeY);
						nLodedSurfType = pSurfaceData[nSurfCell];
					}
				}

				for (int x = 1; x < nLodUnitSize; x++)
				{
					float kx = (float)x / (float)nLodUnitSize;

					float z1 = (1.f - kx) * GET_Z_VAL(X + 0, Y + 0) + (kx) * GET_Z_VAL(X + nLodUnitSize, Y + 0);
					float z2 = (1.f - kx) * GET_Z_VAL(X + 0, Y + nLodUnitSize) + (kx) * GET_Z_VAL(X + nLodUnitSize, Y + nLodUnitSize);

					for (int y = 1; y < nLodUnitSize; y++)
					{
						// skip map borders
						int nBorder = (nSectorSize >> 2);
						if ((X + x) < nBorder || (X + x) > (nTerrainSize - nBorder) ||
						    (Y + y) < nBorder || (Y + y) > (nTerrainSize - nBorder))
							continue;

						float ky = (float)y / nLodUnitSize;
						float fInterpolatedZ = (1.f - ky) * z1 + ky * z2;
						float fRealZ = GET_Z_VAL(X + x, Y + y);
						float fDiff = fabs(fRealZ - fInterpolatedZ);

						if (fMaxDiff < fSurfSwitchError)
						{
							int nSurfX = (X + x - nSurfOffsetX);
							int nSurfY = (Y + y - nSurfOffsetY);
							if (nSurfX >= 0 && nSurfY >= 0 && nSurfX < nSurfSizeX && nSurfY < nSurfSizeY && pSurfaceData)
							{
								int nSurfCell = nSurfX * nSurfSizeY + nSurfY;
								assert(nSurfCell >= 0 && nSurfCell < nSurfSizeX * nSurfSizeY);
								uint8 nRealSurfType = pSurfaceData[nSurfCell];

								// rise error if surface types will pop
								if (nRealSurfType != nLodedSurfType)
									fDiff = max(fDiff, fSurfSwitchError);
							}
						}

						if (fDiff > fMaxDiff)
							fMaxDiff = fDiff;
					}
				}
			}
		}
		// note: values in m_arrGeomErrors table may be non incremental - this is correct
		pLodErrors[nLod] = fMaxDiff;
	}
}

void CTerrain::HighlightTerrain(int x1, int y1, int x2, int y2, int nSID)
{
	// Input dimensions are in units.
	int iHMUnit = GetHeightMapUnitSize();
	float x1u = (float)x1 * iHMUnit, x2u = (float)x2 * iHMUnit;
	float y1u = (float)y1 * iHMUnit, y2u = (float)y2 * iHMUnit;

	ColorB clrRed(255, 0, 0);
	IRenderAuxGeom* prag = GetISystem()->GetIRenderer()->GetIRenderAuxGeom();

	for (int x = x1; x < x2; x += 1)
	{
		float xu = (float)x * iHMUnit;
		prag->DrawLine(Vec3(y1u, xu, GetZfromUnits(y1, x, nSID) + 0.5f), clrRed, Vec3(y1u, xu + iHMUnit, GetZfromUnits(y1, x + 1, nSID) + 0.5f), clrRed, 5.0f);
		prag->DrawLine(Vec3(y2u, xu, GetZfromUnits(y2, x, nSID) + 0.5f), clrRed, Vec3(y2u, xu + iHMUnit, GetZfromUnits(y2, x + 1, nSID) + 0.5f), clrRed, 5.0f);
	}
	for (int y = y1; y < y2; y += 1)
	{
		float yu = (float)y * iHMUnit;
		prag->DrawLine(Vec3(yu, x1u, GetZfromUnits(y, x1, nSID) + 0.5f), clrRed, Vec3(yu + iHMUnit, x1u, GetZfromUnits(y + 1, x1, nSID) + 0.5f), clrRed, 5.0f);
		prag->DrawLine(Vec3(yu, x2u, GetZfromUnits(y, x2, nSID) + 0.5f), clrRed, Vec3(yu + iHMUnit, x2u, GetZfromUnits(y + 1, x2, nSID) + 0.5f), clrRed, 5.0f);
	}
}

void CTerrain::SetTerrainElevation(int X1, int Y1, int nSizeX, int nSizeY, float* pTerrainBlock,
	unsigned char* pSurfaceData, int nSurfOrgX, int nSurfOrgY, int nSurfSizeX, int nSurfSizeY,
	uint32* pResolMap, int nResolMapSizeX, int nResolMapSizeY, int nSID)
{
#ifndef _RELEASE

	//LOADING_TIME_PROFILE_SECTION;
	FUNCTION_PROFILER_3DENGINE;

	float fStartTime = GetCurAsyncTimeSec();
	int nUnitSize = CTerrain::GetHeightMapUnitSize();
	int nHmapSize = CTerrain::GetTerrainSize() / nUnitSize;

	ResetHeightMapCache();

	// everything is in units in this function

	assert(nSizeX == nSizeY);
	assert(X1 == ((X1 >> m_nUnitsToSectorBitShift) << m_nUnitsToSectorBitShift));
	assert(Y1 == ((Y1 >> m_nUnitsToSectorBitShift) << m_nUnitsToSectorBitShift));
	assert(nSizeX == ((nSizeX >> m_nUnitsToSectorBitShift) << m_nUnitsToSectorBitShift));
	assert(nSizeY == ((nSizeY >> m_nUnitsToSectorBitShift) << m_nUnitsToSectorBitShift));

	if (X1 < 0 || Y1 < 0 || X1 + nSizeX > nHmapSize || Y1 + nSizeY > nHmapSize)
	{
		Warning("CTerrain::SetTerrainHeightMapBlock: (X1,Y1) values out of range");
		return;
	}

#ifdef SEG_WORLD
	if (nSID < 0)
	{
		int X2 = X1 + nSizeX;
		int Y2 = Y1 + nSizeY;
		int nEndSID = GetMaxSegmentsCount();
		for (nSID = 0; nSID < nEndSID; ++nSID)
		{
			AABB aabb;
			if (!GetSegmentBounds(nSID, aabb))
				continue;
			int sx1 = ((int)aabb.min.x) >> m_nBitShift;
			int sy1 = ((int)aabb.min.y) >> m_nBitShift;
			int sx2 = ((int)aabb.max.x) >> m_nBitShift;
			int sy2 = ((int)aabb.max.y) >> m_nBitShift;

			int xMin = max(X1, sx1);
			int xMax = min(X2, sx2);
			if (xMin >= xMax)
				continue;
			int yMin = max(Y1, sy1);
			int yMax = min(Y2, sy2);
			if (yMin >= yMax)
				continue;
			SetTerrainElevation(xMin - sx1, yMin - sy1, xMax - xMin, yMax - yMin,
				pTerrainBlock, pSurfaceData, nSurfOrgX, nSurfOrgY, nSurfSizeX, nSurfSizeY,
				pResolMap, nResolMapSizeX, nResolMapSizeY, nSID);
		}
		return;
	}
#endif

	AABB aabb;
	if (!GetSegmentBounds(nSID, aabb))
	{
		Warning("CTerrain::SetTerrainElevation: invalid segment id specified!");
		return;
	}
	int x0 = ((int)aabb.min.x) >> m_nBitShift;
	int y0 = ((int)aabb.min.y) >> m_nBitShift;

	if (!GetParentNode(nSID))
		BuildSectorsTree(false, nSID);

	Array2d<struct CTerrainNode*>& sectorLayer = m_arrSecInfoPyramid[nSID][0];

	int rangeX1 = max(0, X1 >> m_nUnitsToSectorBitShift);
	int rangeY1 = max(0, Y1 >> m_nUnitsToSectorBitShift);
	int rangeX2 = min(sectorLayer.GetSize(), (X1 + nSizeX) >> m_nUnitsToSectorBitShift);
	int rangeY2 = min(sectorLayer.GetSize(), (Y1 + nSizeY) >> m_nUnitsToSectorBitShift);

	std::vector<float> rawHeightmap;

	AABB modifiedArea;
	modifiedArea.Reset();

	for (int rangeX = rangeX1; rangeX < rangeX2; rangeX++)
	{
		for (int rangeY = rangeY1; rangeY < rangeY2; rangeY++)
		{
			CTerrainNode* pTerrainNode = sectorLayer[rangeX][rangeY];

			int x1 = x0 + (rangeX << m_nUnitsToSectorBitShift);
			int y1 = y0 + (rangeY << m_nUnitsToSectorBitShift);
			int x2 = x0 + ((rangeX + 1) << m_nUnitsToSectorBitShift);
			int y2 = y0 + ((rangeY + 1) << m_nUnitsToSectorBitShift);

			// find min/max
			float fMin = pTerrainBlock[x1 * nHmapSize + y1];
			float fMax = fMin;

			for (int x = x1; x <= x2; x++)
			{
				for (int y = y1; y <= y2; y++)
				{
					float fHeight = pTerrainBlock[CLAMP(x, 0, nHmapSize - 1) * nHmapSize + CLAMP(y, 0, nHmapSize - 1)];

					if (fHeight > fMax) fMax = fHeight;
					if (fHeight < fMin) fMin = fHeight;
				}
			}

			// reserve some space for in-game deformations
			fMin = max(0.f, fMin - TERRAIN_DEFORMATION_MAX_DEPTH);

			float fMaxTexelSizeMeters = -1;

			if (pResolMap)
			{
				// get max allowed texture resolution here
				int nResMapX = (nResolMapSizeX * (x1 / 2 + x2 / 2) / nHmapSize);
				int nResMapY = (nResolMapSizeY * (y1 / 2 + y2 / 2) / nHmapSize);
				nResMapX = CLAMP(nResMapX, 0, nResolMapSizeX - 1);
				nResMapY = CLAMP(nResMapY, 0, nResolMapSizeY - 1);
				int nTexRes = pResolMap[nResMapY + nResMapX * nResolMapSizeY];
				int nResTileSizeMeters = GetTerrainSize() / nResolMapSizeX;
				fMaxTexelSizeMeters = (float)nResTileSizeMeters / (float)nTexRes;
			}

			// find error metrics
			if (!pTerrainNode->m_pGeomErrors)
				pTerrainNode->m_pGeomErrors = new float[m_nUnitsToSectorBitShift];

			bool bHasHoleEdges = false;

			{
				BuildErrorsTableForArea(pTerrainNode->m_pGeomErrors, m_nUnitsToSectorBitShift, x1, y1, x2, y2, pTerrainBlock, pSurfaceData, nSurfOrgX, nSurfOrgY, nSurfSizeX, nSurfSizeY, bHasHoleEdges);
			}

			assert(pTerrainNode->m_pGeomErrors[0] == 0);

			pTerrainNode->m_boxHeigtmapLocal.min.Set((float)((x1 - x0) * nUnitSize), (float)((y1 - y0) * nUnitSize), fMin);
			pTerrainNode->m_boxHeigtmapLocal.max.Set((float)((x2 - x0) * nUnitSize), (float)((y2 - y0) * nUnitSize), max(fMax, GetWaterLevel()));

			// same as in SetLOD()
			float fAllowedError = 0.05f; // ( GetCVars()->e_TerrainLodRatio * 32.f ) / 180.f * 2.5f;
			int nGeomMML = m_nUnitsToSectorBitShift - 1;

			for (; nGeomMML > 0; nGeomMML--)
			{
				float fStepMeters = (float)((1 << nGeomMML) * GetHeightMapUnitSize());

				if (fStepMeters <= fMaxTexelSizeMeters)
					break;

				if (pTerrainNode->m_pGeomErrors[nGeomMML] < fAllowedError)
					break;
			}

			if (nGeomMML == m_nUnitsToSectorBitShift - 1 && (fMax - fMin) < 0.05f)
				nGeomMML = m_nUnitsToSectorBitShift;

			if (bHasHoleEdges)
				nGeomMML = 0;

			SRangeInfo& ri = pTerrainNode->m_rangeInfo;

			// TODO: add proper fp16/fp32 support
			/*
					ri.fOffset = fMin = CryConvertHalfToFloat(CryConvertFloatToHalf(fMin));
					ri.fRange = powf(2.0f, ceilf(logf((fMax - fMin) / 65535.f)/logf(2.0f)));
			 */
			 /*		ri.fOffset = fMin;
					 ri.fRange	 = (fMax - fMin) / 65535.f;
				*/

			int nStep = 1 << nGeomMML;
			int nMaxStep = 1 << m_nUnitsToSectorBitShift;

			if (ri.nSize != nMaxStep / nStep + 1)
			{
				delete[] ri.pHMData;
				ri.nSize = nMaxStep / nStep + 1;
				ri.pHMData = new uint16[ri.nSize * ri.nSize];
				ri.UpdateBitShift(m_nUnitsToSectorBitShift);
			}

			if (rawHeightmap.size() < (size_t)ri.nSize * ri.nSize)
			{
				rawHeightmap.resize(ri.nSize * ri.nSize);
			}

			{
				int ix = CLAMP(x1, 1, nHmapSize - 1);
				int iy = CLAMP(y1, 1, nHmapSize - 1);

				fMin = pTerrainBlock[ix * nHmapSize + iy];
				fMax = fMin;
			}

			// fill height map data array in terrain node, all in units
			for (int x = x1; x <= x2; x += nStep)
			{
				for (int y = y1; y <= y2; y += nStep)
				{
					int ix = min(nHmapSize - 1, x);
					int iy = min(nHmapSize - 1, y);

					float fHeight = pTerrainBlock[ix * nHmapSize + iy];

					if (fHeight > fMax) fMax = fHeight;
					if (fHeight < fMin) fMin = fHeight;

					// TODO: add proper fp16/fp32 support

					int x_local = (x - x1) / nStep;
					int y_local = (y - y1) / nStep;

					rawHeightmap[x_local * ri.nSize + y_local] = fHeight;
				}
			}

			ri.fOffset = fMin;
			ri.fRange = (fMax - fMin) / float(0xFFF0);

			const unsigned mask12bit = (1 << 12) - 1;
			const int inv5cm = 20;

			int iOffset = int(fMin * inv5cm);
			int iRange = int((fMax - fMin) * inv5cm);
			int iStep = iRange ? (iRange + mask12bit - 1) / mask12bit : 1;

			iRange /= iStep;

			ri.iOffset = iOffset;
			ri.iRange = iRange;
			ri.iStep = iStep;

			bool bHMDataIsModified = false;

			for (int x = x1; x <= x2; x += nStep)
			{
				for (int y = y1; y <= y2; y += nStep)
				{
					int xlocal = (x - x1) / nStep;
					int ylocal = (y - y1) / nStep;
					int nCellLocal = xlocal * ri.nSize + ylocal;

					unsigned hdec = int((rawHeightmap[nCellLocal] - fMin) * inv5cm) / iStep;

					uint16 usSurfType = ri.GetSurfaceTypeByIndex(nCellLocal); // BAD CODE!!! we deleted this info and new(ed) it => invalid data here

					assert(x >= x0 + X1 && y >= y0 + Y1 && x <= x0 + X1 + nSurfSizeX && y <= y0 + Y1 + nSurfSizeY && pSurfaceData);

					int nSurfX = (x - nSurfOrgX);
					int nSurfY = (y - nSurfOrgY);

					if (nSurfX >= 0 && nSurfY >= 0 && nSurfX < nSurfSizeX && nSurfY < nSurfSizeY && pSurfaceData)
					{
						int nSurfCell = nSurfX * nSurfSizeY + nSurfY;
						assert(nSurfCell >= 0 && nSurfCell < nSurfSizeX * nSurfSizeY);
						usSurfType = ri.GetLocalSurfaceTypeID(pSurfaceData[nSurfCell]);
					}

					uint16 nNewValue = (usSurfType& SRangeInfo::e_index_hole) | (hdec << 4);

					if (nNewValue != ri.pHMData[nCellLocal])
					{
						ri.pHMData[nCellLocal] = nNewValue;

						bHMDataIsModified = true;
					}
				}
			}

			// re-init surface types info and update vert buffers in entire brunch
			if (GetParentNode(nSID))
			{
				CTerrainNode* pNode = pTerrainNode;
				while (pNode)
				{
					pNode->ReleaseHeightMapGeometry();
					pNode->RemoveProcObjects(false, false);
					pNode->UpdateDetailLayersInfo(false);

					// propagate bounding boxes and error metrics to parents
					if (pNode != pTerrainNode)
					{
						for (int i = 0; i < m_nUnitsToSectorBitShift; i++)
						{
							pNode->m_pGeomErrors[i] = max(max(
								pNode->m_pChilds[0].m_pGeomErrors[i],
								pNode->m_pChilds[1].m_pGeomErrors[i]), max(
									pNode->m_pChilds[2].m_pGeomErrors[i],
									pNode->m_pChilds[3].m_pGeomErrors[i]));
						}

						pNode->m_boxHeigtmapLocal.min = SetMaxBB();
						pNode->m_boxHeigtmapLocal.max = SetMinBB();

						for (int nChild = 0; nChild < 4; nChild++)
						{
							pNode->m_boxHeigtmapLocal.min.CheckMin(pNode->m_pChilds[nChild].m_boxHeigtmapLocal.min);
							pNode->m_boxHeigtmapLocal.max.CheckMax(pNode->m_pChilds[nChild].m_boxHeigtmapLocal.max);
						}
					}

					// request elevation texture update
					if (pNode->m_nNodeTexSet.nSlot0 != 0xffff && pNode->m_nNodeTexSet.nSlot0 < m_pTerrain->m_texCache[2].GetPoolSize())
					{
						if(bHMDataIsModified)
							pNode->m_eElevTexEditingState = eTES_SectorIsModified_AtlasIsDirty;
					}

					pNode = pNode->m_pParent;
				}
			}

			modifiedArea.Add(pTerrainNode->GetBBox());
		}
	}

	if (GetCurAsyncTimeSec() - fStartTime > 1)
		PrintMessage("CTerrain::SetTerrainElevation took %.2f sec", GetCurAsyncTimeSec() - fStartTime);

	if (Get3DEngine()->m_pObjectsTree[nSID])
		Get3DEngine()->m_pObjectsTree[nSID]->UpdateTerrainNodes();

	// update roads
	if (Get3DEngine()->m_pObjectsTree[nSID] && (GetCVars()->e_TerrainDeformations || m_bEditor))
	{
		PodArray<IRenderNode*> lstRoads;

		aabb = AABB(
		  Vec3((float)(x0 + X1) * (float)nUnitSize, (float)(y0 + Y1) * (float)nUnitSize, 0.f),
		  Vec3((float)(x0 + X1) * (float)nUnitSize + (float)nSizeX * (float)nUnitSize, (float)(y0 + Y1) * (float)nUnitSize + (float)nSizeY * (float)nUnitSize, 1024.f));

		Get3DEngine()->m_pObjectsTree[nSID]->GetObjectsByType(lstRoads, eERType_Road, &aabb);
		for (int i = 0; i < lstRoads.Count(); i++)
		{
			CRoadRenderNode* pRoad = (CRoadRenderNode*)lstRoads[i];
			pRoad->OnTerrainChanged();
		}
	}

	if (GetParentNode(nSID))
		GetParentNode(nSID)->UpdateRangeInfoShift();

	if (!modifiedArea.IsReset())
	{
		modifiedArea.Expand(Vec3(2.f * GetHeightMapUnitSize()));
		ResetTerrainVertBuffers(&modifiedArea, 0);
	}

	m_bHeightMapModified = 0;

#endif // _RELEASE
}

void CTerrain::SetTerrainSectorTexture(int nTexSectorX, int nTexSectorY, unsigned int textureId, bool bMergeNotAllowed, int nSID)
{
#ifdef SEG_WORLD
	nSID = WorldToSegment(nTexSectorX, nTexSectorY, m_nBitShift + m_nUnitsToSectorBitShift, nSID);
	if (nSID < 0)
	{
		Warning("CTerrain::LockSectorTexture: (nTexSectorX, nTexSectorY) values out of range");
		return;
	}
#endif
	int nDiffTexTreeLevelOffset = 0;

	if (nTexSectorX < 0 ||
	    nTexSectorY < 0 ||
	    nTexSectorX >= CTerrain::GetSectorsTableSize(nSID) >> nDiffTexTreeLevelOffset ||
	    nTexSectorY >= CTerrain::GetSectorsTableSize(nSID) >> nDiffTexTreeLevelOffset)
	{
		Warning("CTerrain::LockSectorTexture: (nTexSectorX, nTexSectorY) values out of range");
		return;
	}

#ifdef SEG_WORLD
	if (!GetParentNode(nSID))
		BuildSectorsTree(false, nSID);

	// open terrain texture now, otherwise it will be opened later and unlock all sectors
	if (Get3DEngine()->IsSegmentOperationInProgress() && !m_arrBaseTexInfos[nSID].m_nDiffTexIndexTableSize)
		OpenTerrainTextureFile(m_arrBaseTexInfos[nSID].m_hdrDiffTexHdr, m_arrBaseTexInfos[nSID].m_hdrDiffTexInfo,
		                       m_arrSegmentPaths[nSID] + COMPILED_TERRAIN_TEXTURE_FILE_NAME, m_arrBaseTexInfos[nSID].m_ucpDiffTexTmpBuffer, m_arrBaseTexInfos[nSID].m_nDiffTexIndexTableSize, nSID);
#endif

	CTerrainNode* pNode = m_arrSecInfoPyramid[nSID][nDiffTexTreeLevelOffset][nTexSectorX][nTexSectorY];

#ifdef SEG_WORLD
	if (!idOld == !textureId || textureId == (unsigned int)-1)
		return;
#endif

	while (pNode)
	{
		pNode->EnableTextureEditingMode(textureId);
		pNode = pNode->m_pParent;
	}
}

void CTerrain::ResetTerrainVertBuffers(const AABB* pBox, int nSID)
{
	if (nSID == -1)
	{
		for (int id = 0; id < m_pParentNodes.Count(); id++)
		{
			if (Get3DEngine()->IsSegmentSafeToUse(id) && GetParentNode(id))
				GetParentNode(id)->ReleaseHeightMapGeometry(true, pBox);
		}
	}
	else if (GetParentNode(nSID))
		GetParentNode(nSID)->ReleaseHeightMapGeometry(true, pBox);
}

void CTerrain::SetOceanWaterLevel(float fOceanWaterLevel)
{
	SetWaterLevel(fOceanWaterLevel);
	pe_params_buoyancy pb;
	pb.waterPlane.origin.Set(0, 0, fOceanWaterLevel);
	if (gEnv->pPhysicalWorld)
		gEnv->pPhysicalWorld->AddGlobalArea()->SetParams(&pb);
	extern float g_oceanStep;
	g_oceanStep = -1; // if e_PhysOceanCell is used, make it re-apply the params on Update
}

bool CTerrain::CanPaintSurfaceType(int x, int y, int r, uint16 usGlobalSurfaceType)
{
	// Checks if the palettes of all sectors touching the given brush square can accept usGlobalSurfaceType.

	if (usGlobalSurfaceType == SRangeInfo::e_hole)
		return true;

	int nTerrainSizeSectors = CTerrain::GetTerrainSize() / CTerrain::GetSectorSize();

	int rangeX1 = max(0, (x - r) >> m_nUnitsToSectorBitShift);
	int rangeY1 = max(0, (y - r) >> m_nUnitsToSectorBitShift);
	int rangeX2 = min(nTerrainSizeSectors, ((x + r) >> m_nUnitsToSectorBitShift) + 1);
	int rangeY2 = min(nTerrainSizeSectors, ((y + r) >> m_nUnitsToSectorBitShift) + 1);

	// Can we fit this surface type into the palettes of all sectors involved?
	for (int rangeX = rangeX1; rangeX < rangeX2; rangeX++)
		for (int rangeY = rangeY1; rangeY < rangeY2; rangeY++)
		{
			int rx = rangeX, ry = rangeY;
			int nSID = WorldToSegment(ry, rx, m_nBitShift + m_nUnitsToSectorBitShift);
			if (nSID < 0)
				continue;

			Array2d<struct CTerrainNode*>& sectorLayer = m_arrSecInfoPyramid[nSID][0];
			CTerrainNode* pTerrainNode = sectorLayer[ry][rx];
			SRangeInfo& ri = pTerrainNode->m_rangeInfo;

			if (ri.GetLocalSurfaceTypeID(usGlobalSurfaceType) == SRangeInfo::e_index_hole)
			{
				IRenderAuxText::DrawLabel(Vec3((float)y, (float)x, GetZfromUnits(y, x, -1) + 1.0f), 2.0f, "SECTOR PALETTE FULL!");
				HighlightTerrain(
				  rangeX << m_nUnitsToSectorBitShift, rangeY << m_nUnitsToSectorBitShift,
				  (rangeX + 1) << m_nUnitsToSectorBitShift, (rangeY + 1) << m_nUnitsToSectorBitShift);
				return false;
			}
		}

	return true;
}

ILINE bool CTerrain::IsRenderNodeIncluded(IRenderNode* pNode, const AABB& region, const uint16* pIncludeLayers, int numIncludeLayers)
{
	EERType type = pNode->GetRenderNodeType();

	switch (type)
	{
	case eERType_Brush:
	case eERType_Vegetation:
	case eERType_Decal:
	case eERType_WaterVolume:
	case eERType_MergedMesh:
		break;

	default:
		CRY_ASSERT_TRACE(0, ("Need to support cloning terrain object type %d", type));
		return false;
	}

	if (type == eERType_MergedMesh)
	{
		CMergedMeshRenderNode* pMergedMesh = (CMergedMeshRenderNode*)pNode;

		// The bounding box for a merged mesh can slop over into a neighboring grid
		// chunk, so only clone it if the center is contained in the region we're cloning.
		if (!region.IsContainPoint(pMergedMesh->m_pos))
			return false;
	}

	if (numIncludeLayers == 0)
		return true;

	const uint16 layerId = pNode->GetLayerId();

	// Some terrain objects don't have layer ids (like vegetation), so we have to include
	// them even if we're filtering.
	if (layerId == 0)
	{
		return true;
	}
	else
	{
		for (int e = 0; e < numIncludeLayers; e++)
		{
			if (pIncludeLayers[e] == layerId)
			{
				return true;
			}
		}
	}

	// If we made it to this point none of the include layers matched, so it's not excluded
	return false;
}

void CTerrain::MarkAndOffsetCloneRegion(const AABB& region, const Vec3& offset)
{
	C3DEngine* p3DEngine = Get3DEngine();
	int numObjects = p3DEngine->GetObjectsInBox(region, NULL);

	if (numObjects == 0)
		return;

	PodArray<IRenderNode*> objects;
	objects.resize(numObjects);
	p3DEngine->GetObjectsInBox(region, &objects[0]);

	int skyLayer = gEnv->pEntitySystem->GetLayerId("Sky");

	for (int i = 0; i < numObjects; i++)
	{
		IRenderNode* pNode = objects[i];
		EERType type = pNode->GetRenderNodeType();

		// HACK: Need a way for certain layers to not get offset, like the skybox
		if (type == eERType_Brush && skyLayer != -1)
		{
			CBrush* pBrush = (CBrush*)pNode;
			if (pBrush->GetLayerId() == skyLayer)
				continue;
		}

		if (IsRenderNodeIncluded(pNode, region, NULL, 0))
		{
			CRY_ASSERT_MESSAGE((pNode->GetRndFlags() & ERF_CLONE_SOURCE) == 0, "Marking already marked node, is an object overlapping multiple regions?");

			pNode->SetRndFlags(ERF_CLONE_SOURCE, true);

			pNode->OffsetPosition(offset);
			p3DEngine->RegisterEntity(pNode);
		}
	}
}

void CTerrain::CloneRegion(const AABB& region, const Vec3& offset, float zRotation, const uint16* pIncludeLayers, int numIncludeLayers)
{
	C3DEngine* p3DEngine = Get3DEngine();
	int numObjects = p3DEngine->GetObjectsInBox(region, NULL);

	if (numObjects == 0)
		return;

	PodArray<IRenderNode*> objects;
	objects.resize(numObjects);
	p3DEngine->GetObjectsInBox(region, &objects[0]);

	Vec3 localOrigin = region.GetCenter();
	Matrix34 l2w(Matrix33::CreateRotationZ(zRotation));
	l2w.SetTranslation(offset);

	for (int i = 0; i < numObjects; i++)
	{
		IRenderNode* pNode = objects[i];

		// If this wasn't flagged as a clone source, don't include it
		if ((pNode->GetRndFlags() & ERF_CLONE_SOURCE) == 0)
			continue;

		if (!IsRenderNodeIncluded(pNode, region, pIncludeLayers, numIncludeLayers))
			continue;

		IRenderNode* pNodeClone = NULL;

		EERType type = pNode->GetRenderNodeType();
		if (type == eERType_Brush)
		{
			CBrush* pSrcBrush = (CBrush*)pNode;

			pNodeClone = pSrcBrush->Clone();
			CBrush* pBrush = (CBrush*)pNodeClone;

			pBrush->m_Matrix.SetTranslation(pBrush->m_Matrix.GetTranslation() - localOrigin);
			pBrush->m_Matrix = l2w * pBrush->m_Matrix;

			pBrush->CalcBBox();

			pBrush->m_pOcNode = NULL;

			// to get correct indirect lighting the registration must be done before checking if this object is inside a VisArea
			Get3DEngine()->RegisterEntity(pBrush, 0);

			//if (gEnv->IsEditorGameMode())
			{
				//pBrush->Physicalize();
			}
			//else
			{
				// keep everything deactivated, game will activate it later
				//if(Get3DEngine()->IsAreaActivationInUse())
				pBrush->SetRndFlags(ERF_HIDDEN, false);

				if (!(Get3DEngine()->IsAreaActivationInUse() && GetCVars()->e_ObjectLayersActivationPhysics == 1))// && !(pChunk->m_dwRndFlags&ERF_NO_PHYSICS))
					pBrush->Physicalize();
			}
		}
		else if (type == eERType_Vegetation)
		{
			pNodeClone = pNode->Clone();
			CVegetation* pVeg = (CVegetation*)pNodeClone;

			Matrix34 matrix;
			matrix.SetRotationZ(pVeg->GetZAngle(), pVeg->m_vPos - localOrigin);

			matrix = l2w * matrix;

			pVeg->m_vPos = matrix.GetTranslation();

			// Vegetation stores rotation as a byte representing degrees, so we have to remap it back
			CryQuat rot(matrix);
			float zRot = rot.GetRotZ();
			pVeg->m_ucAngle = (byte)((RAD2DEG(zRot) / 360.0f) * 255.0f);

			pVeg->CalcBBox();

			pVeg->Physicalize();

			Get3DEngine()->RegisterEntity(pVeg, 0);
		}
		else if (type == eERType_Decal)
		{
			pNodeClone = pNode->Clone();
			CDecalRenderNode* pDecal = (CDecalRenderNode*)pNodeClone;

			Matrix34 mat = pDecal->GetMatrix();
			mat.SetTranslation(mat.GetTranslation() - localOrigin);
			mat = l2w * mat;

			pDecal->SetMatrixFull(mat);

			Get3DEngine()->RegisterEntity(pDecal);
			GetObjManager()->m_decalsToPrecreate.push_back(pDecal);
		}
		else if (type == eERType_WaterVolume)
		{
			pNodeClone = pNode->Clone();
			CWaterVolumeRenderNode* pWaterVol = (CWaterVolumeRenderNode*)pNodeClone;

			pWaterVol->Transform(localOrigin, l2w);

			pWaterVol->Physicalize();

			Get3DEngine()->RegisterEntity(pWaterVol);
		}
		else if (type == eERType_MergedMesh)
		{
			CMergedMeshRenderNode* pSrcMergedMesh = (CMergedMeshRenderNode*)pNode;

			// Transform the position of the source merged mesh to our destination location
			Vec3 dstPos;
			{
				Matrix34 dst;
				dst.SetTranslationMat(pSrcMergedMesh->m_pos - localOrigin);
				dst = l2w * dst;
				dstPos = dst.GetTranslation();
			}

			// Get the merged mesh node for this coordinate.
			CMergedMeshRenderNode* pDstMergedMesh = m_pMergedMeshesManager->GetNode(dstPos);
			CRY_ASSERT_MESSAGE(pDstMergedMesh->m_pos == dstPos, "Cloned destination position isn't on grid");
			CRY_ASSERT_MESSAGE(pDstMergedMesh->m_nGroups == 0, "Cloning into a used chunk");

			if (pDstMergedMesh->m_nGroups == 0)
			{
				pDstMergedMesh->m_initPos = pSrcMergedMesh->m_initPos;
				pDstMergedMesh->m_zRotation = zRotation;

				// Transform the AABB's
				AABB visibleAABB = pSrcMergedMesh->m_visibleAABB;
				visibleAABB.Move(-localOrigin);
				visibleAABB.SetTransformedAABB(l2w, visibleAABB);
				pDstMergedMesh->m_visibleAABB = visibleAABB;

				AABB internalAABB = pSrcMergedMesh->m_internalAABB;
				internalAABB.Move(-localOrigin);
				internalAABB.SetTransformedAABB(l2w, internalAABB);
				pDstMergedMesh->m_internalAABB = internalAABB;

				pSrcMergedMesh->CopyIRenderNodeData(pDstMergedMesh);

				if (!gEnv->IsDedicated())
				{
					// In the editor the merged meshes have extra proxy data.  We don't
					// clear out the source merged meshes in the editor, so the easiest
					// solution is to just share the source groups and not delete them
					// on destruction.
					if (gEnv->IsEditorGameMode())
					{
						pDstMergedMesh->m_ownsGroups = 0;
						pDstMergedMesh->m_groups = pSrcMergedMesh->m_groups;
						pDstMergedMesh->m_nGroups = pSrcMergedMesh->m_nGroups;
					}
					else
					{
						for (size_t i = 0; i < pSrcMergedMesh->m_nGroups; ++i)
						{
							SMMRMGroupHeader& pGroup = pSrcMergedMesh->m_groups[i];
							pDstMergedMesh->AddGroup(pGroup.instGroupId, pGroup.numSamples);
						}
					}
				}

				pNodeClone = pDstMergedMesh;

				Get3DEngine()->RegisterEntity(pDstMergedMesh);
			}
		}

		if (pNodeClone)
		{
			pNodeClone->SetRndFlags(ERF_CLONE_SOURCE, false);
			pNodeClone->SetLayerId(pNode->GetLayerId());
		}
	}
}

void CTerrain::ClearCloneSources()
{
	I3DEngine* p3DEngine = Get3DEngine();

	int numObjects = p3DEngine->GetObjectsByFlags(ERF_CLONE_SOURCE, NULL);

	if (numObjects == 0)
		return;

	PodArray<IRenderNode*> objects;
	objects.resize(numObjects);
	p3DEngine->GetObjectsByFlags(ERF_CLONE_SOURCE, &objects[0]);

	for (int i = 0; i < numObjects; i++)
	{
		IRenderNode* pNode = objects[i];

		Get3DEngine()->DeleteRenderNode(pNode);
	}
}
