// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#ifndef __INCLUDE_CRY3DENGINE_OBJECTTREE_H
#define __INCLUDE_CRY3DENGINE_OBJECTTREE_H

#pragma once

#define OCTREENODE_RENDER_FLAG_OBJECTS               1
#define OCTREENODE_RENDER_FLAG_OCCLUDERS             2
#define OCTREENODE_RENDER_FLAG_CASTERS               4
#define OCTREENODE_RENDER_FLAG_OBJECTS_ONLY_ENTITIES 8

#define OCTREENODE_CHUNK_VERSION                     5

enum ELoadObjectsMode { LOM_LOAD_ALL, LOM_LOAD_ONLY_NON_STREAMABLE, LOM_LOAD_ONLY_STREAMABLE };

class CBrush;
class COctreeNode;
template<class T, size_t overAllocBytes> class PodArray;
struct CLightEntity;
struct ILightSource;
struct IParticleEmitter;

///////////////////////////////////////////////////////////////////////////////
// data to be pushed to occlusion culler
struct CRY_ALIGN(16) SCheckOcclusionJobData
{
	enum JobTypeT { QUIT, OCTREE_NODE, TERRAIN_NODE };

	SCheckOcclusionJobData()
	{
	}

	static SCheckOcclusionJobData CreateQuitJobData();
	static SCheckOcclusionJobData CreateOctreeJobData(COctreeNode * pOctTreeNode, int nRenderMask, const Vec3 &rAmbColor, const SRenderingPassInfo &passInfo);
	static SCheckOcclusionJobData CreateTerrainJobData(CTerrainNode * pTerrainNode, const AABB &rAABB, float fDistance);

	JobTypeT type; // type to indicate with which data the union is filled
	union
	{
		// data for octree nodes
		struct
		{
			COctreeNode* pOctTreeNode;
			int          nRenderMask;
			float        vAmbColor[3];
		} octTreeData;

		// data for terrain nodes
		struct
		{
			CTerrainNode* pTerrainNode;
			float         vAABBMin[3];
			float         vAABBMax[3];
			float         fDistance;
		} terrainData;
	};
	// common data
	SRendItemSorter rendItemSorter; // ensure order octree traversal oder even with parallel execution
	const CCamera* pCam;            // store camera to handle vis areas correctly
};

///////////////////////////////////////////////////////////////////////////////
inline SCheckOcclusionJobData SCheckOcclusionJobData::CreateQuitJobData()
{
	SCheckOcclusionJobData jobData;
	jobData.type = QUIT;
	return jobData;
}

///////////////////////////////////////////////////////////////////////////////
inline SCheckOcclusionJobData SCheckOcclusionJobData::CreateOctreeJobData(COctreeNode* pOctTreeNode, int nRenderMask, const Vec3& rAmbColor, const SRenderingPassInfo& passInfo)
{
	SCheckOcclusionJobData jobData;
	jobData.type = OCTREE_NODE;
	jobData.octTreeData.pOctTreeNode = pOctTreeNode;
	jobData.octTreeData.nRenderMask = nRenderMask;
	jobData.octTreeData.vAmbColor[0] = rAmbColor.x;
	jobData.octTreeData.vAmbColor[1] = rAmbColor.y;
	jobData.octTreeData.vAmbColor[2] = rAmbColor.z;
	jobData.rendItemSorter = passInfo.GetRendItemSorter();
	jobData.pCam = &passInfo.GetCamera();
	return jobData;
}

///////////////////////////////////////////////////////////////////////////////
inline SCheckOcclusionJobData SCheckOcclusionJobData::CreateTerrainJobData(CTerrainNode* pTerrainNode, const AABB& rAABB, float fDistance)
{
	SCheckOcclusionJobData jobData;
	jobData.type = TERRAIN_NODE;
	jobData.terrainData.pTerrainNode = pTerrainNode;
	jobData.terrainData.vAABBMin[0] = rAABB.min.x;
	jobData.terrainData.vAABBMin[1] = rAABB.min.y;
	jobData.terrainData.vAABBMin[2] = rAABB.min.z;
	jobData.terrainData.vAABBMax[0] = rAABB.max.x;
	jobData.terrainData.vAABBMax[1] = rAABB.max.y;
	jobData.terrainData.vAABBMax[2] = rAABB.max.z;
	jobData.terrainData.fDistance = fDistance;
	return jobData;
}

///////////////////////////////////////////////////////////////////////////////
// data written by occlusion culler jobs, to control main thread 3dengine side rendering
struct CRY_ALIGN(16) SCheckOcclusionOutput
{
	enum JobTypeT { ROAD_DECALS, COMMON, TERRAIN, DEFORMABLE_BRUSH };

	static SCheckOcclusionOutput CreateDecalsAndRoadsOutput(IRenderNode * pObj, PodArray<CDLight*>* pAffectingLights, const Vec3 &rAmbColor, const AABB &rObjBox, float fEntDistance, bool bSunOnly, bool bCheckPerObjectOcclusion, const SRenderingPassInfo &passInfo);
	static SCheckOcclusionOutput CreateCommonObjectOutput(IRenderNode * pObj, PodArray<CDLight*>* pAffectingLights, const Vec3 &rAmbColor, const AABB &rObjBox, float fEntDistance, bool bSunOnly, SSectorTextureSet * pTerrainTexInfo, const SRenderingPassInfo &passInfo);
	static SCheckOcclusionOutput CreateTerrainOutput(CTerrainNode * pTerrainNode, const SRenderingPassInfo &passInfo);
	static SCheckOcclusionOutput CreateDeformableBrushOutput(CBrush * pBrush, CRenderObject * pObj, int nLod, const SRenderingPassInfo &passInfo);

	JobTypeT type;
	union
	{
		//VEGETATION,ROAD_DECALS,COMMON Data
		struct
		{
			IRenderNode*        pObj;
			PodArray<CDLight*>* pAffectingLights;
			SSectorTextureSet*  pTerrainTexInfo;
			float               fEntDistance;
			bool                bSunOnly;
			bool                bCheckPerObjectOcclusion;
		} common;

		//TERRAIN Data
		struct
		{
			CTerrainNode* pTerrainNode;
		} terrain;

		//DEFORMABLE_BRUSH Data
		struct
		{
			CBrush*        pBrush;
			CRenderObject* pRenderObject;
			int            nLod;
		} deformable_brush;

		//BRUSH Data
		struct
		{
			CBrush*        pBrush;
			CRenderObject* pRenderObject;
			int16          nLodA;
			int16          nLodB;
			uint8          nDissolveRef;
		} brush;
	};

	Vec3 vAmbColor;
	AABB objBox;
	SRendItemSorter rendItemSorter;
};

///////////////////////////////////////////////////////////////////////////////
inline SCheckOcclusionOutput SCheckOcclusionOutput::CreateDecalsAndRoadsOutput(IRenderNode* pObj, PodArray<CDLight*>* pAffectingLights, const Vec3& rAmbColor, const AABB& rObjBox, float fEntDistance, bool bSunOnly, bool bCheckPerObjectOcclusion, const SRenderingPassInfo& passInfo)
{
	SCheckOcclusionOutput outputData;
	outputData.type = ROAD_DECALS;
	outputData.rendItemSorter = passInfo.GetRendItemSorter();
	outputData.vAmbColor = rAmbColor;
	outputData.objBox = rObjBox;

	outputData.common.pObj = pObj;
	outputData.common.pAffectingLights = pAffectingLights;
	outputData.common.pTerrainTexInfo = NULL;
	outputData.common.fEntDistance = fEntDistance;
	outputData.common.bSunOnly = bSunOnly;
	outputData.common.bCheckPerObjectOcclusion = bCheckPerObjectOcclusion;

	return outputData;
}

///////////////////////////////////////////////////////////////////////////////
inline SCheckOcclusionOutput SCheckOcclusionOutput::CreateCommonObjectOutput(IRenderNode* pObj, PodArray<CDLight*>* pAffectingLights, const Vec3& rAmbColor, const AABB& rObjBox, float fEntDistance, bool bSunOnly, SSectorTextureSet* pTerrainTexInfo, const SRenderingPassInfo& passInfo)
{
	SCheckOcclusionOutput outputData;
	outputData.type = COMMON;
	outputData.rendItemSorter = passInfo.GetRendItemSorter();
	outputData.vAmbColor = rAmbColor;
	outputData.objBox = rObjBox;

	outputData.common.pObj = pObj;
	outputData.common.pAffectingLights = pAffectingLights;
	outputData.common.fEntDistance = fEntDistance;
	outputData.common.bSunOnly = bSunOnly;
	outputData.common.pTerrainTexInfo = pTerrainTexInfo;

	return outputData;
}

///////////////////////////////////////////////////////////////////////////////
inline SCheckOcclusionOutput SCheckOcclusionOutput::CreateTerrainOutput(CTerrainNode* pTerrainNode, const SRenderingPassInfo& passInfo)
{
	SCheckOcclusionOutput outputData;
	outputData.type = TERRAIN;
	outputData.rendItemSorter = passInfo.GetRendItemSorter();

	outputData.terrain.pTerrainNode = pTerrainNode;

	return outputData;
}

///////////////////////////////////////////////////////////////////////////////
inline SCheckOcclusionOutput SCheckOcclusionOutput::CreateDeformableBrushOutput(CBrush* pBrush, CRenderObject* pRenderObject, int nLod, const SRenderingPassInfo& passInfo)
{
	SCheckOcclusionOutput outputData;
	outputData.type = DEFORMABLE_BRUSH;
	outputData.rendItemSorter = passInfo.GetRendItemSorter();

	outputData.deformable_brush.pBrush = pBrush;
	outputData.deformable_brush.nLod = nLod;
	outputData.deformable_brush.pRenderObject = pRenderObject;

	return outputData;
}

///////////////////////////////////////////////////////////////////////////////
enum EOcTeeNodeListType
{
	eMain,
	eCasters,
	eSprites,
	eLights,
};

template<class T> struct TDoublyLinkedList
{
	T* m_pFirstNode, * m_pLastNode;

	TDoublyLinkedList()
	{
		m_pFirstNode = m_pLastNode = NULL;
	}

	~TDoublyLinkedList()
	{
		assert(!m_pFirstNode && !m_pLastNode);
	}

	void insertAfter(T* pAfter, T* pObj)
	{
		pObj->m_pPrev = pAfter;
		pObj->m_pNext = pAfter->m_pNext;
		if (pAfter->m_pNext == NULL)
			m_pLastNode = pObj;
		else
			pAfter->m_pNext->m_pPrev = pObj;
		pAfter->m_pNext = pObj;
	}

	void insertBefore(T* pBefore, T* pObj)
	{
		pObj->m_pPrev = pBefore->m_pPrev;
		pObj->m_pNext = pBefore;
		if (pBefore->m_pPrev == NULL)
			m_pFirstNode = pObj;
		else
			pBefore->m_pPrev->m_pNext = pObj;
		pBefore->m_pPrev = pObj;
	}

	void insertBeginning(T* pObj)
	{
		if (m_pFirstNode == NULL)
		{
			m_pFirstNode = pObj;
			m_pLastNode = pObj;
			pObj->m_pPrev = NULL;
			pObj->m_pNext = NULL;
		}
		else
			insertBefore(m_pFirstNode, pObj);
	}

	void insertEnd(T* pObj)
	{
		if (m_pLastNode == NULL)
			insertBeginning(pObj);
		else
			insertAfter(m_pLastNode, pObj);
	}

	void remove(T* pObj)
	{
		if (pObj->m_pPrev == NULL)
			m_pFirstNode = pObj->m_pNext;
		else
			pObj->m_pPrev->m_pNext = pObj->m_pNext;

		if (pObj->m_pNext == NULL)
			m_pLastNode = pObj->m_pPrev;
		else
			pObj->m_pNext->m_pPrev = pObj->m_pPrev;

		pObj->m_pPrev = pObj->m_pNext = NULL;
	}

	bool empty() const { return m_pFirstNode == NULL; }
};

struct SInstancingInfo
{
	SInstancingInfo() { pStatInstGroup = 0; aabb.Reset(); fMinSpriteDistance = 10000.f; bInstancingInUse = 0; }
	StatInstGroup*                                        pStatInstGroup;
	DynArray<CVegetation*>                                arrInstances;
	stl::aligned_vector<CRenderObject::SInstanceInfo, 16> arrMats;
	stl::aligned_vector<SVegetationSpriteInfo, 16>        arrSprites;
	AABB  aabb;
	float fMinSpriteDistance;
	bool  bInstancingInUse;
};

struct SLayerVisibility
{
	const uint8*  pLayerVisibilityMask;
	const uint16* pLayerIdTranslation;
};

struct SOctreeLoadObjectsData
{
	COctreeNode*             pNode;
	ptrdiff_t                offset;
	size_t                   size;
	_smart_ptr<IMemoryBlock> pMemBlock;
	byte*                    pObjPtr;
	byte*                    pEndObjPtr;
};

class COctreeNode : public IOctreeNode, Cry3DEngineBase, IStreamCallback
{
public:

	struct ShadowMapFrustumParams
	{
		CDLight*                  pLight;
		struct ShadowMapFrustum*  pFr;
		PodArray<SPlaneObject>*   pShadowHull;
		const SRenderingPassInfo* passInfo;
		Vec3                      vCamPos;
		uint32                    nRenderNodeFlags;
		bool                      bSun;
	};

	~COctreeNode();
	void                ResetStaticInstancing();
	bool                HasChildNodes();
	int                 CountChildNodes();
	void                InsertObject(IRenderNode* pObj, const AABB& objBox, const float fObjRadiusSqr, const Vec3& vObjCenter);
	bool                DeleteObject(IRenderNode* pObj);
	void                Render_Object_Nodes(bool bNodeCompletelyInFrustum, int nRenderMask, const Vec3& vAmbColor, const SRenderingPassInfo& passInfo);
	void                CheckUpdateStaticInstancing();
	void                RenderDebug();
	static void         DeallocateRenderContentQueue();
	void                RenderContent(int nRenderMask, const Vec3& vAmbColor, const SRenderingPassInfo& passInfo);
	void                RenderContentJobEntry(int nRenderMask, Vec3 vAmbColor, SRenderingPassInfo passInfo);
	void                RenderVegetations(TDoublyLinkedList<IRenderNode>* lstObjects, int nRenderMask, bool bNodeCompletelyInFrustum, PodArray<CDLight*>* pAffectingLights, bool bSunOnly, SSectorTextureSet* pTerrainTexInfo, const SRenderingPassInfo& passInfo);
	void                RenderCommonObjects(TDoublyLinkedList<IRenderNode>* lstObjects, int nRenderMask, const Vec3& vAmbColor, bool bNodeCompletelyInFrustum, PodArray<CDLight*>* pAffectingLights, bool bSunOnly, SSectorTextureSet* pTerrainTexInfo, const SRenderingPassInfo& passInfo);
	void                RenderDecalsAndRoads(TDoublyLinkedList<IRenderNode>* lstObjects, int nRenderMask, const Vec3& vAmbColor, bool bNodeCompletelyInFrustum, PodArray<CDLight*>* pAffectingLights, bool bSunOnly, const SRenderingPassInfo& passInfo);
	void                RenderBrushes(TDoublyLinkedList<IRenderNode>* lstObjects, bool bNodeCompletelyInFrustum, PodArray<CDLight*>* pAffectingLights, bool bSunOnly, SSectorTextureSet* pTerrainTexInfo, const SRenderingPassInfo& passInfo);
	PodArray<CDLight*>* GetAffectingLights(const SRenderingPassInfo& passInfo);
	void                AddLightSource(CDLight* pSource, const SRenderingPassInfo& passInfo);
	void                CheckInitAffectingLights(const SRenderingPassInfo& passInfo);
	void                FillShadowCastersList(bool bNodeCompletellyInFrustum, CDLight* pLight, struct ShadowMapFrustum* pFr, PodArray<SPlaneObject>* pShadowHull, uint32 nRenderNodeFlags, const SRenderingPassInfo& passInfo);
	void                FillShadowMapCastersList(const ShadowMapFrustumParams& params, bool bNodeCompletellyInFrustum);
	void                ActivateObjectsLayer(uint16 nLayerId, bool bActivate, bool bPhys, IGeneralMemoryHeap* pHeap, const AABB& layerBox);
	void                GetLayerMemoryUsage(uint16 nLayerId, ICrySizer* pSizer, int* pNumBrushes, int* pNumDecals);

	void                MarkAsUncompiled(const IRenderNode* pRenderNode = NULL);
	COctreeNode*        FindNodeContainingBox(const AABB& objBox);
	void                MoveObjectsIntoList(PodArray<SRNInfo>* plstResultEntities, const AABB* pAreaBox, bool bRemoveObjects = false, bool bSkipDecals = false, bool bSkip_ERF_NO_DECALNODE_DECALS = false, bool bSkipDynamicObjects = false, EERType eRNType = eERType_TypesNum);
	int                 PhysicalizeInBox(const AABB& bbox);
	int                 DephysicalizeInBox(const AABB& bbox);
	int                 PhysicalizeOfType(ERNListType listType, bool bInstant);
	int                 DePhysicalizeOfType(ERNListType listType, bool bInstant);

#if ENGINE_ENABLE_COMPILATION
	int GetData(byte*& pData, int& nDataSize, std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, std::vector<IStatInstGroup*>* pStatInstGroupTable, EEndian eEndian, SHotUpdateInfo* pExportInfo, const Vec3& segmentOffset);
#endif

	const AABB&  GetObjectsBBox() { return m_objectsBox; }
	AABB         GetShadowCastersBox(const AABB* pBBox, const Matrix34* pShadowSpaceTransform);
	void         DeleteObjectsByFlag(int nRndFlag);
	void         UnregisterEngineObjectsInArea(const SHotUpdateInfo* pExportInfo, PodArray<IRenderNode*>& arrUnregisteredObjects, bool bOnlyEngineObjects);
	uint32       GetLastVisFrameId() { return m_nLastVisFrameId; }
	void         GetObjectsByType(PodArray<IRenderNode*>& lstObjects, EERType objType, const AABB* pBBox, bool* pInstStreamCheckReady = NULL, uint64 dwFlags = ~0, bool bRecursive = true);
	void         GetObjectsByFlags(uint dwFlags, PodArray<IRenderNode*>& lstObjects);

	void         GetNearestCubeProbe(float& fMinDistance, int& nMaxPriority, CLightEntity*& pNearestLight, const AABB* pBBox);
	void         GetObjects(PodArray<IRenderNode*>& lstObjects, const AABB* pBBox);
	bool         GetShadowCastersTimeSliced(IRenderNode* pIgnoreNode, ShadowMapFrustum* pFrustum, const PodArray<struct SPlaneObject>* pShadowHull, int renderNodeExcludeFlags, int& totalRemainingNodes, int nCurLevel, const SRenderingPassInfo& passInfo);
	bool         IsObjectTypeInTheBox(EERType objType, const AABB& WSBBox);
	bool         CleanUpTree();
	int          GetObjectsCount(EOcTeeNodeListType eListType);
	static int32 SaveObjects_CompareRenderNodes(const void* v1, const void* v2);
	int SaveObjects(class CMemoryBlock* pMemBlock, std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, std::vector<IStatInstGroup*>* pStatInstGroupTable, EEndian eEndian, const SHotUpdateInfo * pExportInfo, const Vec3 &segmentOffest);
	int          LoadObjects(byte* pPtr, byte* pEndPtr, std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, EEndian eEndian, int nChunkVersion, const SLayerVisibility* pLayerVisibility, const Vec3& segmentOffest, ELoadObjectsMode eLoadMode);
	static int   GetSingleObjectFileDataSize(IRenderNode* pObj, const SHotUpdateInfo* pExportInfo);
	static void  SaveSingleObject(byte*& pPtr, int& nDatanSize, IRenderNode* pObj, std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, std::vector<IStatInstGroup*>* pStatInstGroupTable, EEndian eEndian, const SHotUpdateInfo* pExportInfo, const Vec3& segmentOffset);
	static void  LoadSingleObject(byte*& pPtr, std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, EEndian eEndian, int nChunkVersion, const SLayerVisibility* pLayerVisibility, int nSID, const Vec3& segmentOffset, ELoadObjectsMode eLoadMode, IRenderNode*& pRN);
	static bool  IsObjectStreamable(EERType eType, uint64 dwRndFlags);
	static bool  CheckSkipLoadObject(EERType eType, uint64 dwRndFlags, ELoadObjectsMode eLoadMode);
	bool         IsRightNode(const AABB& objBox, const float fObjRadius, float fObjMaxViewDist);
	void         GetMemoryUsage(ICrySizer* pSizer) const;
	void         UpdateTerrainNodes(CTerrainNode* pParentNode = 0);

	template<class T>
	int         Load_T(T*& f, int& nDataSize, std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, EEndian eEndian, AABB* pBox, const SLayerVisibility* pLayerVisibility, const Vec3& segmentOffset);
	int         Load(FILE*& f, int& nDataSize, std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, EEndian eEndian, AABB* pBox, const SLayerVisibility* pLayerVisibility, const Vec3& segmentOffset);
	int         Load(uint8*& f, int& nDataSize, std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, EEndian eEndian, AABB* pBox, const SLayerVisibility* pLayerVisibility, const Vec3& segmentOffset);
	bool        StreamLoad(uint8* pData, int nDataSize, std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, EEndian eEndian, AABB* pBox);

	static void FreeLoadingCache();
	void        GenerateStatObjAndMatTables(std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, std::vector<IStatInstGroup*>* pStatInstGroupTable, SHotUpdateInfo* pExportInfo);
	static void ReleaseEmptyNodes();
	static void StaticReset();
	bool        IsEmpty();
	bool        HasObjects();

	// used for streaming
	bool                   UpdateStreamingPriority(PodArray<COctreeNode*>& arrRecursion, float fMinDist, float fMaxDist, bool bFullUpdate, const SObjManPrecacheCamera* pPrecacheCams, size_t nPrecacheCams, const SRenderingPassInfo& passInfo);
	float                  GetNodeStreamingDistance(const SObjManPrecacheCamera* pPrecacheCams, AABB objectsBox, size_t nPrecacheCams, const SRenderingPassInfo& passInfo);
	void                   ReleaseStreamableContent();
	bool                   CheckStartStreaming(bool bFullUpdate);
	virtual void           StreamOnComplete(IReadStream* pStream, unsigned nError);
	template<class T> void StreamOnCompleteReadObjects(T* f, int nDataSize);
	void                   StartStreaming(bool bFinishNow, IReadStream_AutoPtr* ppStream);
	template<class T> int  ReadObjects(T*& f, int& nDataSize, EEndian eEndian, std::vector<IStatObj*>* pStatObjTable, std::vector<IMaterial*>* pMatTable, const SLayerVisibility* pLayerVisibilityMask, const Vec3& segmentOffset, SOcTreeNodeChunk& chunk, ELoadObjectsMode eLoadMode);
	void                   ReleaseObjects(bool bReleaseOnlyStreamable = false);
	void                   GetStreamedInNodesNum(int& nAllStreamable, int& nReady);
	static int             GetStreamingTasksNum()  { return m_nInstStreamTasksInProgress; }
	static int             GetStreamedInNodesNum() { return m_arrStreamedInNodes.Count(); }
	static void            StreamingCheckUnload(const SRenderingPassInfo& passInfo, PodArray<SObjManPrecacheCamera>& rStreamPreCacheCameras);

	void                   CheckManageVegetationSprites(float fNodeDistance, int nMaxFrames, const SRenderingPassInfo& passInfo);
	AABB                   GetNodeBox() const
	{
		return AABB(
		  m_vNodeCenter - m_vNodeAxisRadius,
		  m_vNodeCenter + m_vNodeAxisRadius);
	}

	void OffsetObjects(const Vec3& offset);
	void SetVisArea(CVisArea* pVisArea);
	void UpdateVisAreaSID(CVisArea* pVisArea, int nSID)
	{
		assert(pVisArea);
		m_nSID = nSID;
	}

	static COctreeNode* Create(int nSID, const AABB& box, struct CVisArea* pVisArea, COctreeNode* pParent = NULL);

protected:
	AABB  GetChildBBox(int nChildId);
	void  CompileObjects();
	void  UpdateStaticInstancing();
	void  UpdateObjects(IRenderNode* pObj);
	void  CompileCharacter(ICharacterInstance* pChar, uint32& nInternalFlags);
	void  CompileObjectsBrightness();
	float GetNodeObjectsMaxViewDistance();

	// Check if min spec specified in render node passes current server config spec.
	static bool CheckRenderFlagsMinSpec(uint32 dwRndFlags);

	void        LinkObject(IRenderNode* pObj, EERType eERType, bool bPushFront = true);
	void        UnlinkObject(IRenderNode* pObj);

	static int  Cmp_OctreeNodeSize(const void* v1, const void* v2);

private:
	COctreeNode(int nSID, const AABB& box, struct CVisArea* pVisArea, COctreeNode* pParent);

	float        GetNodeRadius2() const { return m_vNodeAxisRadius.Dot(m_vNodeAxisRadius); }
	COctreeNode* FindChildFor(IRenderNode* pObj, const AABB& objBox, const float fObjRadius, const Vec3& vObjCenter);
	bool         HasAnyRenderableCandidates(const SRenderingPassInfo& passInfo) const;
	void         BuildLoadingDatas(PodArray<SOctreeLoadObjectsData>* pQueue, byte* pOrigData, byte*& pData, int& nDataSize, EEndian eEndian);
	PodArray<SOctreeLoadObjectsData> m_loadingDatas;

	static const float               fMinShadowCasterViewDist;

	bool                             m_streamComplete;

	uint32                           m_nOccludedFrameId;
	uint32                           m_renderFlags;
	uint32                           m_errTypesBitField;
	AABB                             m_objectsBox;
	float                            m_fObjectsMaxViewDist;
	uint32                           m_nLastVisFrameId;

	COctreeNode*                     m_arrChilds[8];
	TDoublyLinkedList<IRenderNode>   m_arrObjects[eRNListType_ListsNum];
	PodArray<SCasterInfo>            m_lstCasters;
	Vec3                             m_vNodeCenter;
	Vec3                             m_vNodeAxisRadius;
	PodArray<CDLight*>               m_lstAffectingLights;
	uint32                           m_nLightMaskFrameId;
	COctreeNode*                     m_pParent;
	uint32                           nFillShadowCastersSkipFrameId;
	float                            m_fNodeDistance;
	int                              m_nManageVegetationsFrameId;
	int                              m_nSID;

	OcclusionTestClient              m_occlusionTestClient;

	uint32                           m_bHasLights               : 1;
	uint32                           m_bHasRoads                : 1;
	uint32                           m_bNodeCompletelyInFrustum : 1;
	uint32                           m_fpSunDirX                : 7;
	uint32                           m_fpSunDirZ                : 7;
	uint32                           m_fpSunDirYs               : 1;

	// used for streaming
	int                           m_nFileDataOffset; // TODO: make it 64bit
	int                           m_nFileDataSize;
	EFileStreamingStatus          m_eStreamingStatus;
	IReadStreamPtr                m_pReadStream;
	int                           m_nUpdateStreamingPrioriryRoundId;
	static int                    m_nInstStreamTasksInProgress;
	static FILE*                  m_pFileForSyncRead;
	static PodArray<COctreeNode*> m_arrStreamedInNodes;
	uint32                        m_bStaticInstancingIsDirty : 1;

	struct SNodeInstancingInfo
	{
		SNodeInstancingInfo() { pRNode = 0; nodeMatrix.IsIdentity(); }
		Matrix34           nodeMatrix;
		class CVegetation* pRNode;
	};
	std::map<std::pair<IStatObj*, IMaterial*>, PodArray<SNodeInstancingInfo>*>* m_pStaticInstancingInfo;

	float                         m_fPrevTerrainTexScale; // used to detect terrain texturing change and refresh info in object instances

	static void*                  m_pRenderContentJobQueue;
public:
	static PodArray<COctreeNode*> m_arrEmptyNodes;
	static int                    m_nNodesCounterAll;
	static int                    m_nNodesCounterStreamable;
	static int                    m_nInstCounterLoaded;

	volatile int                  m_updateStaticInstancingLock;
};

#endif
