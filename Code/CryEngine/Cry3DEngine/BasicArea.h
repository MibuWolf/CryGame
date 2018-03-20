// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#ifndef _BASICAREA_H_
#define _BASICAREA_H_

#define COPY_MEMBER_SAVE(_dst, _src, _name) { (_dst)->_name = (_src)->_name; }
#define COPY_MEMBER_LOAD(_dst, _src, _name) { (_dst)->_name = (_src)->_name; }

enum EObjList
{
	DYNAMIC_OBJECTS = 0,
	STATIC_OBJECTS,
	PROC_OBJECTS,
	ENTITY_LISTS_NUM
};

struct SRNInfo
{
	SRNInfo()
	{
		memset(this, 0, sizeof(*this));
	}

	SRNInfo(IRenderNode* _pNode)
	{
		fMaxViewDist = _pNode->m_fWSMaxViewDist;
		AABB aabbBox = _pNode->GetBBox();
		objSphere.center = aabbBox.GetCenter();
		objSphere.radius = aabbBox.GetRadius();
		pNode = _pNode;
		nRType = _pNode->GetRenderNodeType();

		/*#ifdef _DEBUG
		    erType = _pNode->GetRenderNodeType();
		    cry_strcpy(szName, _pNode->GetName());
		 #endif*/
	}

	bool operator==(const IRenderNode* _pNode) const { return (pNode == _pNode); }
	bool operator==(const SRNInfo& rOther) const     { return (pNode == rOther.pNode); }

	float        fMaxViewDist;
	Sphere       objSphere;
	IRenderNode* pNode;
	EERType      nRType;
	/*#ifdef _DEBUG
	   EERType erType;
	   char szName[32];
	 #endif*/
};

struct SCasterInfo
{
	SCasterInfo()
	{
		memset(this, 0, sizeof(*this));
	}

	SCasterInfo(IRenderNode* _pNode, float fMaxDist)
	{
		fMaxCastingDist = fMaxDist;
		objBox = _pNode->GetBBox();
		objSphere.center = objBox.GetCenter();
		objSphere.radius = objBox.GetRadius();
		pNode = _pNode;
		nRType = _pNode->GetRenderNodeType();
		nRenderNodeFlags = _pNode->GetRndFlags();
		bCanExecuteAsRenderJob = _pNode->CanExecuteRenderAsJob();
	}

	SCasterInfo(IRenderNode* _pNode, float fMaxDist, EERType renderNodeType)
	{
		fMaxCastingDist = fMaxDist;
		_pNode->FillBBox(objBox);
		objSphere.center = objBox.GetCenter();
		objSphere.radius = objBox.GetRadius();
		pNode = _pNode;
		nRType = renderNodeType;
		nRenderNodeFlags = _pNode->GetRndFlags();
		bCanExecuteAsRenderJob = _pNode->CanExecuteRenderAsJob();
	}

	bool operator==(const IRenderNode* _pNode) const { return (pNode == _pNode); }
	bool operator==(const SCasterInfo& rOther) const { return (pNode == rOther.pNode); }

	void GetMemoryUsage(ICrySizer* pSizer) const     { /*nothing*/ }

	float        fMaxCastingDist;
	Sphere       objSphere;
	AABB         objBox;
	IRenderNode* pNode;
	uint32       nGSMFrameId;
	EERType      nRType;
	bool         bCanExecuteAsRenderJob;
	uint64       nRenderNodeFlags;
};

#define UPDATE_PTR_AND_SIZE(_pData, _nDataSize, _SIZE_PLUS) \
  {                                                         \
    _pData += (_SIZE_PLUS);                                 \
    _nDataSize -= (_SIZE_PLUS);                             \
    assert(_nDataSize >= 0);                                \
  }                                                         \

enum EAreaType
{
	eAreaType_Undefined,
	eAreaType_OcNode,
	eAreaType_VisArea
};

struct CBasicArea : public Cry3DEngineBase
{
	CBasicArea()
	{
		m_boxArea.min = m_boxArea.max = Vec3(0, 0, 0);
		m_pObjectsTree = NULL;
	}

	~CBasicArea();

	void CompileObjects(int nListId); // optimize objects lists for rendering

	class COctreeNode* m_pObjectsTree;

	AABB               m_boxArea;    // bbox containing everything in sector including child sectors
	AABB               m_boxStatics; // bbox containing only objects in STATIC_OBJECTS list of this node and height-map

};

#endif // _BASICAREA_H_
