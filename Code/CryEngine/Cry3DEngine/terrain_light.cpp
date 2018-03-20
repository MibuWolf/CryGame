// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  File name:   terrain_light.cpp
//  Version:     v1.00
//  Created:     28/5/2001 by Vladimir Kajalin
//  Compilers:   Visual Studio.NET
//  Description: generate geometry for hmap light pass
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"

#include "terrain.h"
#include "terrain_sector.h"
#include "PolygonClipContext.h"
#include "RoadRenderNode.h"
#include <Cry3DEngine/IIndexedMesh.h>

_smart_ptr<IRenderMesh> CTerrain::MakeAreaRenderMesh(const Vec3& vPos, float fRadius,
                                                     IMaterial* pMat, const char* szLSourceName,
                                                     Plane* planes)
{
	PodArray<vtx_idx> lstIndices;
	lstIndices.Clear();
	PodArray<Vec3> posBuffer;
	posBuffer.Clear();
	PodArray<SVF_P3S_C4B_T2S> vertBuffer;
	vertBuffer.Clear();
	PodArray<SPipTangents> tangBasises;
	tangBasises.Clear();

	Vec3 vPt(vPos);
	int nSID = WorldToSegment(vPt, GetDefSID());
	int nUnitSize = GetTerrain()->GetHeightMapUnitSize();

	Vec3i vBoxMin = vPos - Vec3(fRadius, fRadius, fRadius);
	Vec3i vBoxMax = vPos + Vec3(fRadius, fRadius, fRadius);

	vBoxMin.x = vBoxMin.x / nUnitSize * nUnitSize;
	vBoxMin.y = vBoxMin.y / nUnitSize * nUnitSize;
	vBoxMin.z = vBoxMin.z / nUnitSize * nUnitSize;

	vBoxMax.x = vBoxMax.x / nUnitSize * nUnitSize + nUnitSize;
	vBoxMax.y = vBoxMax.y / nUnitSize * nUnitSize + nUnitSize;
	vBoxMax.z = vBoxMax.z / nUnitSize * nUnitSize + nUnitSize;

	int nSizeX = (vBoxMax.x - vBoxMin.x) / nUnitSize;
	int nSizeY = (vBoxMax.y - vBoxMin.y) / nUnitSize;

	int nEstimateVerts = nSizeX * nSizeY;
	nEstimateVerts = clamp_tpl(nEstimateVerts, 100, 10000);
	posBuffer.reserve(nEstimateVerts);
	lstIndices.reserve(nEstimateVerts * 6);

	const CTerrain* pTerrain = GetTerrain();
	for (int x = vBoxMin.x; x <= vBoxMax.x; x += nUnitSize)
	{
		for (int y = vBoxMin.y; y <= vBoxMax.y; y += nUnitSize)
		{
			Vec3 vTmp = Vec3((float)x, (float)y, (float)(pTerrain->GetZ(x, y, nSID)));
			posBuffer.Add(vTmp);
		}
	}

	for (int x = 0; x < nSizeX; x++)
	{
		for (int y = 0; y < nSizeY; y++)
		{
			vtx_idx id0 = (x + 0) * (nSizeY + 1) + (y + 0);
			vtx_idx id1 = (x + 1) * (nSizeY + 1) + (y + 0);
			vtx_idx id2 = (x + 0) * (nSizeY + 1) + (y + 1);
			vtx_idx id3 = (x + 1) * (nSizeY + 1) + (y + 1);

			assert((int)id3 < posBuffer.Count());

			if (pTerrain->GetHole(vBoxMin.x + x, vBoxMin.y + y, nSID)) continue;

			if (pTerrain->IsMeshQuadFlipped(vBoxMin.x + x, vBoxMin.y + y, nUnitSize, nSID))
			{
				lstIndices.Add(id0);
				lstIndices.Add(id1);
				lstIndices.Add(id3);

				lstIndices.Add(id0);
				lstIndices.Add(id3);
				lstIndices.Add(id2);
			}
			else
			{
				lstIndices.Add(id0);
				lstIndices.Add(id1);
				lstIndices.Add(id2);

				lstIndices.Add(id2);
				lstIndices.Add(id1);
				lstIndices.Add(id3);
			}
		}
	}

	// clip triangles
	if (planes)
	{
		int nOrigCount = lstIndices.Count();
		for (int i = 0; i < nOrigCount; i += 3)
		{
			if (CRoadRenderNode::ClipTriangle(posBuffer, lstIndices, i, planes))
			{
				i -= 3;
				nOrigCount -= 3;
			}
		}
	}

	AABB bbox;
	bbox.Reset();
	for (int i = 0, nIndexCount = lstIndices.size(); i < nIndexCount; i++)
		bbox.Add(posBuffer[lstIndices[i]]);

	tangBasises.reserve(posBuffer.size());
	vertBuffer.reserve(posBuffer.size());

	for (int i = 0, nPosCount = posBuffer.size(); i < nPosCount; i++)
	{
		SVF_P3S_C4B_T2S vTmp;
		vTmp.xyz = posBuffer[i] - vPos;
		vTmp.color.dcolor = uint32(-1);
		vTmp.st = Vec2(0, 0);
		vertBuffer.Add(vTmp);

		SPipTangents basis;

		Vec3 vTang = Vec3(0, 1, 0);
		Vec3 vNormal = GetTerrain()->GetTerrainSurfaceNormal_Int(fastround_positive(posBuffer[i].x), fastround_positive(posBuffer[i].y), GetDefSID());

		// Orthonormalize Tangent Frame
		Vec3 vBitang = -vNormal.Cross(vTang);
		vBitang.Normalize();
		vTang = vNormal.Cross(vBitang);
		vTang.Normalize();

		tangBasises.Add(SPipTangents(vTang, vBitang, -1));
	}

	_smart_ptr<IRenderMesh> pMesh = GetRenderer()->CreateRenderMeshInitialized(
	  vertBuffer.GetElements(), vertBuffer.Count(), EDefaultInputLayouts::P3S_C4B_T2S,
	  lstIndices.GetElements(), lstIndices.Count(), prtTriangleList,
	  szLSourceName, szLSourceName, eRMT_Static, 1, 0, NULL, NULL, false, true, tangBasises.GetElements());

	float texelAreaDensity = 1.0f;

	pMesh->SetChunk(pMat, 0, vertBuffer.Count(), 0, lstIndices.Count(), texelAreaDensity);
	pMesh->SetBBox(bbox.min - vPos, bbox.max - vPos);
	if (pMesh->GetChunks()[0].pRE)
		pMesh->GetChunks()[0].pRE->mfUpdateFlags(FCEF_DIRTY);

	return pMesh;
}

bool CTerrain::RenderArea(Vec3 vPos, float fRadius, _smart_ptr<IRenderMesh>& pRenderMesh,
                          CRenderObject* pObj, IMaterial* pMaterial, const char* szComment, float* pCustomData,
                          Plane* planes, const SRenderingPassInfo& passInfo)
{
	if (passInfo.IsRecursivePass())
		return false;

	FUNCTION_PROFILER_3DENGINE;

	bool bREAdded = false;

	if (!pRenderMesh)
		pRenderMesh = MakeAreaRenderMesh(vPos, fRadius, pMaterial, szComment, planes);

	if (pRenderMesh && pRenderMesh->GetIndicesCount())
	{
		pRenderMesh->SetREUserData(pCustomData);
		pRenderMesh->AddRE(pMaterial, pObj, 0, passInfo, EFSLIST_GENERAL, 1);
		bREAdded = true;
	}

	return bREAdded;
}
