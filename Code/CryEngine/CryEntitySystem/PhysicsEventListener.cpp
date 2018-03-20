// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  File name:   PhysicsEventListener.cpp
//  Version:     v1.00
//  Created:     18/8/2004 by Timur.
//  Compilers:   Visual Studio.NET 2003
//  Description:
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "PhysicsEventListener.h"
#include <CryEntitySystem/IBreakableManager.h>
#include "EntitySlot.h"
#include "Entity.h"
#include "EntitySystem.h"
#include "EntityCVars.h"
#include "BreakableManager.h"

#include "PhysicsProxy.h"

#include <CryPhysics/IPhysics.h>
#include <CryParticleSystem/IParticles.h>
#include <CryParticleSystem/ParticleParams.h>
#include <CryAISystem/IAISystem.h>

#include <CrySystem/ICodeCheckpointMgr.h>

#include <CryThreading/IJobManager_JobDelegator.h>

std::vector<CPhysicsEventListener::PhysVisAreaUpdate> CPhysicsEventListener::m_physVisAreaUpdateVector;
int CPhysicsEventListener::m_jointFxCount = 0, CPhysicsEventListener::m_jointFxFrameId = 0;

//////////////////////////////////////////////////////////////////////////
CPhysicsEventListener::CPhysicsEventListener(CEntitySystem* pEntitySystem, IPhysicalWorld* pPhysics)
{
	assert(pEntitySystem);
	assert(pPhysics);
	m_pEntitySystem = pEntitySystem;

	m_pPhysics = pPhysics;

	RegisterPhysicCallbacks();
}

//////////////////////////////////////////////////////////////////////////
CPhysicsEventListener::~CPhysicsEventListener()
{
	UnregisterPhysicCallbacks();

	m_pPhysics->SetPhysicsEventClient(NULL);
}

//////////////////////////////////////////////////////////////////////////
CEntity* CPhysicsEventListener::GetEntity(IPhysicalEntity* pPhysEntity)
{
	assert(pPhysEntity);
	CEntity* pEntity = (CEntity*)pPhysEntity->GetForeignData(PHYS_FOREIGN_ID_ENTITY);
	return pEntity;
}

//////////////////////////////////////////////////////////////////////////
CEntity* CPhysicsEventListener::GetEntity(void* pForeignData, int iForeignData)
{
	if (PHYS_FOREIGN_ID_ENTITY == iForeignData)
	{
		return (CEntity*)pForeignData;
	}
	return NULL;
}

//////////////////////////////////////////////////////////////////////////
int CPhysicsEventListener::OnPostStep(const EventPhys* pEvent)
{
	EventPhysPostStep* pPostStep = (EventPhysPostStep*)pEvent;
	CEntity* pCEntity = GetEntity(pPostStep->pForeignData, pPostStep->iForeignData);
	IRenderNode* pRndNode = 0;
	if (pCEntity)
	{
		CEntityPhysics* pPhysProxy = pCEntity->GetPhysicalProxy();
		if (pPhysProxy)// && pPhysProxy->GetPhysicalEntity())
			pPhysProxy->OnPhysicsPostStep(pPostStep);
		pRndNode = (pCEntity->GetEntityRender()) ? pCEntity->GetEntityRender()->GetRenderNode() : nullptr;
	}
	else if (pPostStep->iForeignData == PHYS_FOREIGN_ID_ROPE)
	{
		IRopeRenderNode* pRenderNode = (IRopeRenderNode*)pPostStep->pForeignData;
		if (pRenderNode)
		{
			pRenderNode->OnPhysicsPostStep();
		}
		pRndNode = pRenderNode;
	}

	if (pRndNode)
	{
		pe_params_flags pf;
		int bInvisible = 0, bFaraway = 0;
		int bEnableOpt = CVar::es_UsePhysVisibilityChecks;
		float dist = (pRndNode->GetBBox().GetCenter() - GetISystem()->GetViewCamera().GetPosition()).len2();
		float maxDist = pPostStep->pEntity->GetType() != PE_SOFT ? CVar::es_MaxPhysDist : CVar::es_MaxPhysDistCloth;
		bInvisible = bEnableOpt & (isneg(pRndNode->GetDrawFrame() + 10 - static_cast<int>(gEnv->nMainFrameID)) | isneg(sqr(maxDist) - dist));
		if (pRndNode->m_nInternalFlags & IRenderNode::WAS_INVISIBLE ^ (-bInvisible & IRenderNode::WAS_INVISIBLE))
		{
			pf.flagsAND = ~pef_invisible;
			pf.flagsOR = -bInvisible & pef_invisible;
			pPostStep->pEntity->SetParams(&pf);
			(pRndNode->m_nInternalFlags &= ~IRenderNode::WAS_INVISIBLE) |= -bInvisible & IRenderNode::WAS_INVISIBLE;
		}
		if (gEnv->p3DEngine->GetWaterLevel() != WATER_LEVEL_UNKNOWN)
		{
			// Deferred updating ignore ocean flag as the Jobs are busy updating the octree at this point
			m_physVisAreaUpdateVector.push_back(PhysVisAreaUpdate(pRndNode, pPostStep->pEntity));
		}
		bFaraway = bEnableOpt & isneg(sqr(maxDist) +
		                              bInvisible * (sqr(CVar::es_MaxPhysDistInvisible) - sqr(maxDist)) - dist);
		if (bFaraway && !(pRndNode->m_nInternalFlags & IRenderNode::WAS_FARAWAY))
		{
			pe_params_foreign_data pfd;
			pPostStep->pEntity->GetParams(&pfd);
			bFaraway &= -(pfd.iForeignFlags & PFF_UNIMPORTANT) >> 31;
		}
		if ((-bFaraway & IRenderNode::WAS_FARAWAY) != (pRndNode->m_nInternalFlags & IRenderNode::WAS_FARAWAY))
		{
			pe_params_timeout pto;
			pto.timeIdle = 0;
			pto.maxTimeIdle = bFaraway * CVar::es_FarPhysTimeout;
			pPostStep->pEntity->SetParams(&pto);
			(pRndNode->m_nInternalFlags &= ~IRenderNode::WAS_FARAWAY) |= -bFaraway & IRenderNode::WAS_FARAWAY;
		}
	}

	return 1;
}

//////////////////////////////////////////////////////////////////////////
int CPhysicsEventListener::OnPostStepImmediate(const EventPhys* pEvent)
{
	EventPhysPostStep* pPostStep = (EventPhysPostStep*)pEvent;
	if (CEntity* pEntity = GetEntity(pPostStep->pForeignData, pPostStep->iForeignData))
	{
		SEntityEvent event(ENTITY_EVENT_PHYS_POSTSTEP);
		event.fParam[0] = pPostStep->dt;
		pEntity->SendEvent(event);
	}

	return 1;
}

int CPhysicsEventListener::OnPostPump(const EventPhys* pEvent)
{
	if (gEnv->p3DEngine->GetWaterLevel() != WATER_LEVEL_UNKNOWN)
	{
		for (std::vector<PhysVisAreaUpdate>::iterator it = m_physVisAreaUpdateVector.begin(), end = m_physVisAreaUpdateVector.end(); it != end; ++it)
		{
			IRenderNode* pRndNode = it->m_pRndNode;
			int bInsideVisarea = pRndNode->GetEntityVisArea() != 0;
			if (pRndNode->m_nInternalFlags & IRenderNode::WAS_IN_VISAREA ^ (-bInsideVisarea & IRenderNode::WAS_IN_VISAREA))
			{
				pe_params_flags pf;
				pf.flagsAND = ~pef_ignore_ocean;
				pf.flagsOR = -bInsideVisarea & pef_ignore_ocean;
				it->m_pEntity->SetParams(&pf);
				(pRndNode->m_nInternalFlags &= ~IRenderNode::WAS_IN_VISAREA) |= -bInsideVisarea & IRenderNode::WAS_IN_VISAREA;
			}
		}
		m_physVisAreaUpdateVector.clear();
	}
	return 1;
}

//////////////////////////////////////////////////////////////////////////
int CPhysicsEventListener::OnBBoxOverlap(const EventPhys* pEvent)
{
	EventPhysBBoxOverlap* pOverlap = (EventPhysBBoxOverlap*)pEvent;

	CEntity* pCEntity = GetEntity(pOverlap->pForeignData[0], pOverlap->iForeignData[0]);
	CEntity* pCEntityTrg = GetEntity(pOverlap->pForeignData[1], pOverlap->iForeignData[1]);
	if (pCEntity && pCEntityTrg)
	{
		CEntityPhysics* pPhysProxySrc = pCEntity->GetPhysicalProxy();
		if (pPhysProxySrc)
			pPhysProxySrc->OnContactWithEntity(pCEntityTrg);

		CEntityPhysics* pPhysProxyTrg = pCEntityTrg->GetPhysicalProxy();
		if (pPhysProxyTrg)
			pPhysProxyTrg->OnContactWithEntity(pCEntity);

	}
	return 1;
}

//////////////////////////////////////////////////////////////////////////
int CPhysicsEventListener::OnStateChange(const EventPhys* pEvent)
{
	EventPhysStateChange* pStateChange = (EventPhysStateChange*)pEvent;
	CEntity* pCEntity = GetEntity(pStateChange->pForeignData, pStateChange->iForeignData);
	if (pCEntity)
	{
		{
			int nNewSymClass = pStateChange->iSimClass[1];
			//			int nOldSymClass = pStateChange->iSimClass[0];
			if (nNewSymClass == SC_ACTIVE_RIGID)
			{
				// Should activate entity if physics is awaken.
				pCEntity->GetPhysicalProxy()->SetActive(true);
			}
			else if (nNewSymClass == SC_SLEEPING_RIGID)
			{
				// Entity must go to sleep.
				pCEntity->GetPhysicalProxy()->SetActive(false);
				//CallStateFunction(ScriptState_OnStopRollSlideContact, "roll");
				//CallStateFunction(ScriptState_OnStopRollSlideContact, "slide");
			}
		}
		int nOldSymClass = pStateChange->iSimClass[0];
		if (nOldSymClass == SC_ACTIVE_RIGID)
		{
			SEntityEvent event(ENTITY_EVENT_PHYSICS_CHANGE_STATE);
			event.nParam[0] = 1;
			pCEntity->SendEvent(event);
			if (pStateChange->timeIdle >= CVar::es_FarPhysTimeout)
			{
				pe_status_dynamics sd;
				if (pStateChange->pEntity->GetStatus(&sd) && sd.submergedFraction > 0)
				{
					pCEntity->SetFlags(pCEntity->GetFlags() | ENTITY_FLAG_SEND_RENDER_EVENT);
					pCEntity->GetPhysicalProxy()->SetFlags(pCEntity->GetPhysicalProxy()->GetFlags() | CEntityPhysics::FLAG_PHYS_AWAKE_WHEN_VISIBLE);
				}
			}
		}
		else if (nOldSymClass == SC_SLEEPING_RIGID)
		{
			SEntityEvent event(ENTITY_EVENT_PHYSICS_CHANGE_STATE);
			event.nParam[0] = 0;
			pCEntity->SendEvent(event);
		}
	}
	return 1;
}

//////////////////////////////////////////////////////////////////////////
#include <CryPhysics/IDeferredCollisionEvent.h>

class CDeferredMeshUpdatePrep : public IDeferredPhysicsEvent
{
public:
	CDeferredMeshUpdatePrep()
		: m_status(0)
		, m_pStatObj(nullptr)
		, m_id(-1)
		, m_pMesh(nullptr)
		, m_pMeshSkel(nullptr)
		, m_jobState()
	{}

	virtual void              Start()            {}
	virtual void              ExecuteAsJob();
	virtual int               Result(EventPhys*) { return 0; }
	virtual void              Sync()             { gEnv->GetJobManager()->WaitForJob(m_jobState); }
	virtual bool              HasFinished()      { return m_status > 1; }
	virtual DeferredEventType GetType() const    { return PhysCallBack_OnCollision; }
	virtual EventPhys*        PhysicsEvent()     { return 0; }

	virtual void              Execute()
	{
		if (m_id)
			m_pStatObj->GetIndexedMesh(true);
		else
		{
			mesh_data* md = (mesh_data*)m_pMeshSkel->GetData();
			IStatObj* pDeformedStatObj = m_pStatObj->SkinVertices(md->pVertices, m_mtxSkelToMesh);
			m_pMesh->SetForeignData(pDeformedStatObj, 0);
		}
		m_pStatObj->Release();
		m_status = 2;
	}

	IStatObj*             m_pStatObj;
	int                   m_id;
	IGeometry*            m_pMesh, * m_pMeshSkel;
	volatile int          m_status;
	Matrix34              m_mtxSkelToMesh;
	JobManager::SJobState m_jobState;
};

DECLARE_JOB("DefMeshUpdatePrep", TDeferredMeshUpdatePrep, CDeferredMeshUpdatePrep::Execute);

void CDeferredMeshUpdatePrep::ExecuteAsJob()
{
	TDeferredMeshUpdatePrep job;
	job.SetClassInstance(this);
	job.RegisterJobState(&m_jobState);
	job.SetPriorityLevel(JobManager::eRegularPriority);
	job.Run();
}

int g_lastReceivedEventId = 1, g_lastExecutedEventId = 1;
CDeferredMeshUpdatePrep g_meshPreps[16];

int CPhysicsEventListener::OnPreUpdateMesh(const EventPhys* pEvent)
{
	EventPhysUpdateMesh* pepum = (EventPhysUpdateMesh*)pEvent;
	IStatObj* pStatObj;
	if (pepum->iReason == EventPhysUpdateMesh::ReasonDeform || !(pStatObj = (IStatObj*)pepum->pMesh->GetForeignData()))
		return 1;

	if (pepum->idx < 0)
		pepum->idx = pepum->iReason == EventPhysUpdateMesh::ReasonDeform ? 0 : ++g_lastReceivedEventId;

	bool bDefer = false;
	if (g_lastExecutedEventId < pepum->idx - 1)
		bDefer = true;
	else
	{
		int i, j;
		for (i = CRY_ARRAY_COUNT(g_meshPreps) - 1, j = -1;
		     i >= 0 && (!g_meshPreps[i].m_status || (pepum->idx ? (g_meshPreps[i].m_id != pepum->idx) : (g_meshPreps[i].m_pMesh != pepum->pMesh))); i--)
			j += i + 1 & (g_meshPreps[i].m_status - 1 & j) >> 31;
		if (i >= 0)
		{
			if (g_meshPreps[i].m_status == 2)
				g_meshPreps[i].m_status = 0;
			else
				bDefer = true;
		}
		else if (pepum->iReason == EventPhysUpdateMesh::ReasonDeform || !pStatObj->GetIndexedMesh(false))
		{
			if (j >= 0)
			{
				(g_meshPreps[j].m_pStatObj = pStatObj)->AddRef();
				g_meshPreps[j].m_pMesh = pepum->pMesh;
				g_meshPreps[j].m_pMeshSkel = pepum->pMeshSkel;
				g_meshPreps[j].m_mtxSkelToMesh = pepum->mtxSkelToMesh;
				g_meshPreps[j].m_id = pepum->idx;
				g_meshPreps[j].m_status = 1;
				gEnv->p3DEngine->GetDeferredPhysicsEventManager()->DispatchDeferredEvent(&g_meshPreps[j]);
			}
			bDefer = true;
		}
	}
	if (bDefer)
	{
		gEnv->pPhysicalWorld->AddDeferredEvent(EventPhysUpdateMesh::id, pepum);
		return 0;
	}

	if (pepum->idx > 0)
		g_lastExecutedEventId = pepum->idx;
	return 1;
};

int CPhysicsEventListener::OnPreCreatePhysEntityPart(const EventPhys* pEvent)
{
	EventPhysCreateEntityPart* pepcep = (EventPhysCreateEntityPart*)pEvent;
	if (!pepcep->pMeshNew)
		return 1;
	if (pepcep->idx < 0)
		pepcep->idx = ++g_lastReceivedEventId;
	if (g_lastExecutedEventId < pepcep->idx - 1)
	{
		gEnv->pPhysicalWorld->AddDeferredEvent(EventPhysCreateEntityPart::id, pepcep);
		return 0;
	}
	g_lastExecutedEventId = pepcep->idx;
	return 1;
}

//////////////////////////////////////////////////////////////////////////
int CPhysicsEventListener::OnUpdateMesh(const EventPhys* pEvent)
{
	((CBreakableManager*)GetIEntitySystem()->GetBreakableManager())->HandlePhysics_UpdateMeshEvent((EventPhysUpdateMesh*)pEvent);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
int CPhysicsEventListener::OnCreatePhysEntityPart(const EventPhys* pEvent)
{
	EventPhysCreateEntityPart* pCreateEvent = (EventPhysCreateEntityPart*)pEvent;

	//////////////////////////////////////////////////////////////////////////
	// Let Breakable manager handle creation of the new entity part.
	//////////////////////////////////////////////////////////////////////////
	CBreakableManager* pBreakMgr = (CBreakableManager*)GetIEntitySystem()->GetBreakableManager();
	pBreakMgr->HandlePhysicsCreateEntityPartEvent(pCreateEvent);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
int CPhysicsEventListener::OnRemovePhysEntityParts(const EventPhys* pEvent)
{
	EventPhysRemoveEntityParts* pRemoveEvent = (EventPhysRemoveEntityParts*)pEvent;

	CBreakableManager* pBreakMgr = (CBreakableManager*)GetIEntitySystem()->GetBreakableManager();
	pBreakMgr->HandlePhysicsRemoveSubPartsEvent(pRemoveEvent);

	return 1;
}

//////////////////////////////////////////////////////////////////////////
int CPhysicsEventListener::OnRevealPhysEntityPart(const EventPhys* pEvent)
{
	EventPhysRevealEntityPart* pRevealEvent = (EventPhysRevealEntityPart*)pEvent;

	CBreakableManager* pBreakMgr = (CBreakableManager*)GetIEntitySystem()->GetBreakableManager();
	pBreakMgr->HandlePhysicsRevealSubPartEvent(pRevealEvent);

	return 1;
}

//////////////////////////////////////////////////////////////////////////
CEntity* FindAttachId(CEntity* pent, int attachId)
{
	if (pent->GetPhysicalProxy() && pent->GetPhysicalProxy()->GetPhysAttachId() == attachId)
		return pent;
	CEntity* pMatch;
	for (int i = pent->GetChildCount() - 1; i >= 0; i--)
		if (pMatch = FindAttachId((CEntity*)pent->GetChild(i), attachId))
			return pMatch;
	return 0;
}

IEntity* CEntity::UnmapAttachedChild(int& partId)
{
	CEntity* pChild;
	int nLevels, nBits;
	EntityPhysicsUtils::ParsePartId(partId, nLevels, nBits);
	if (partId >= EntityPhysicsUtils::PARTID_LINKED && (pChild = FindAttachId(this, partId >> nBits & EntityPhysicsUtils::PARTID_MAX_ATTACHMENTS - 1)))
	{
		partId &= (1 << nBits) - 1;
		return pChild;
	}
	return this;
}

int CPhysicsEventListener::OnCollision(const EventPhys* pEvent)
{
	EventPhysCollision* pCollision = (EventPhysCollision*)pEvent;
	SEntityEvent event;
	CEntity* pEntitySrc = GetEntity(pCollision->pForeignData[0], pCollision->iForeignData[0]), * pChild,
	       * pEntityTrg = GetEntity(pCollision->pForeignData[1], pCollision->iForeignData[1]);

	int nLevels, nBits;
	EntityPhysicsUtils::ParsePartId(pCollision->partid[0], nLevels, nBits);
	if (pEntitySrc && pCollision->partid[0] >= EntityPhysicsUtils::PARTID_LINKED && (pChild = FindAttachId(pEntitySrc, pCollision->partid[0] >> nBits & EntityPhysicsUtils::PARTID_MAX_ATTACHMENTS - 1)))
	{
		pEntitySrc = pChild;
		pCollision->pForeignData[0] = pEntitySrc;
		pCollision->iForeignData[0] = PHYS_FOREIGN_ID_ENTITY;
		pCollision->partid[0] &= (1 << nBits) - 1;
	}

	EntityPhysicsUtils::ParsePartId(pCollision->partid[1], nLevels, nBits);
	if (pEntitySrc)
	{
		if (pEntityTrg && pCollision->partid[1] >= EntityPhysicsUtils::PARTID_LINKED && (pChild = FindAttachId(pEntityTrg, pCollision->partid[1] >> nBits & EntityPhysicsUtils::PARTID_MAX_ATTACHMENTS - 1)))
		{
			pEntityTrg = pChild;
			pCollision->pForeignData[1] = pEntityTrg;
			pCollision->iForeignData[1] = PHYS_FOREIGN_ID_ENTITY;
			pCollision->partid[1] &= (1 << nBits) - 1;
		}

		CEntityPhysics* pPhysProxySrc = pEntitySrc->GetPhysicalProxy();
		if (pPhysProxySrc)
			pPhysProxySrc->OnCollision(pEntityTrg, pCollision->idmat[1], pCollision->pt, pCollision->n, pCollision->vloc[0], pCollision->vloc[1], pCollision->partid[1], pCollision->mass[1]);

		if (pEntityTrg)
		{
			CEntityPhysics* pPhysProxyTrg = pEntityTrg->GetPhysicalProxy();
			if (pPhysProxyTrg)
				pPhysProxyTrg->OnCollision(pEntitySrc, pCollision->idmat[0], pCollision->pt, -pCollision->n, pCollision->vloc[1], pCollision->vloc[0], pCollision->partid[0], pCollision->mass[0]);
		}
	}

	event.event = ENTITY_EVENT_COLLISION;
	event.nParam[0] = (INT_PTR)pEvent;
	event.nParam[1] = 0;
	if (pEntitySrc)
		pEntitySrc->SendEvent(event);
	event.nParam[1] = 1;
	if (pEntityTrg)
		pEntityTrg->SendEvent(event);

	return 1;
}

//////////////////////////////////////////////////////////////////////////
int CPhysicsEventListener::OnJointBreak(const EventPhys* pEvent)
{
	EventPhysJointBroken* pBreakEvent = (EventPhysJointBroken*)pEvent;
	CEntity* pCEntity = 0;
	IStatObj* pStatObj = 0;
	IRenderNode* pRenderNode = 0;
	Matrix34A nodeTM;

	// Counter for feature test setup
	CODECHECKPOINT(physics_on_joint_break);

	bool bShatter = false;
	switch (pBreakEvent->iForeignData[0])
	{
	case PHYS_FOREIGN_ID_ROPE:
		{
			IRopeRenderNode* pRopeRenderNode = (IRopeRenderNode*)pBreakEvent->pForeignData[0];
			if (pRopeRenderNode)
			{
				pCEntity = static_cast<CEntity*>(pRopeRenderNode->GetOwnerEntity());
			}
		}
		break;
	case PHYS_FOREIGN_ID_ENTITY:
		pCEntity = (CEntity*)pBreakEvent->pForeignData[0];
		break;
	case PHYS_FOREIGN_ID_STATIC:
		{
			pRenderNode = ((IRenderNode*)pBreakEvent->pForeignData[0]);
			pStatObj = pRenderNode->GetEntityStatObj(0, &nodeTM);
			//bShatter = pRenderNode->GetMaterialLayers() & MTL_LAYER_FROZEN;
		}
	}
	//GetEntity(pBreakEvent->pForeignData[0],pBreakEvent->iForeignData[0]);
	if (pCEntity)
	{
		SEntityEvent event;
		event.event = ENTITY_EVENT_PHYS_BREAK;
		event.nParam[0] = (INT_PTR)pEvent;
		event.nParam[1] = 0;
		pCEntity->SendEvent(event);
		pStatObj = pCEntity->GetStatObj(ENTITY_SLOT_ACTUAL);

		//if (pCEntity->GetEntityRender())
		{
			//bShatter = pCEntity->GetEntityRender()->GetMaterialLayersMask() & MTL_LAYER_FROZEN;
		}
	}

	IStatObj* pStatObjEnt = pStatObj;
	if (pStatObj)
		while (pStatObj->GetCloneSourceObject())
			pStatObj = pStatObj->GetCloneSourceObject();

	if (pStatObj && pStatObj->GetFlags() & STATIC_OBJECT_COMPOUND)
	{
		Matrix34 tm = Matrix34::CreateTranslationMat(pBreakEvent->pt);
		IStatObj* pObj1 = 0;
		//		IStatObj *pObj2=0;
		Vec3 axisx = pBreakEvent->n.GetOrthogonal().normalized();
		tm.SetRotation33(Matrix33(axisx, pBreakEvent->n ^ axisx, pBreakEvent->n).T());

		IStatObj::SSubObject* pSubObject1 = pStatObj->GetSubObject(pBreakEvent->partid[0]);
		if (pSubObject1)
			pObj1 = pSubObject1->pStatObj;
		//IStatObj::SSubObject *pSubObject2 = pStatObj->GetSubObject(pBreakEvent->partid[1]);
		//if (pSubObject2)
		//pObj2 = pSubObject2->pStatObj;

		const char* sEffectType = (!bShatter) ? SURFACE_BREAKAGE_TYPE("joint_break") : SURFACE_BREAKAGE_TYPE("joint_shatter");
		CBreakableManager* pBreakMgr = (CBreakableManager*)GetIEntitySystem()->GetBreakableManager();
		if (pObj1 && FxAllowed())
			pBreakMgr->CreateSurfaceEffect(pObj1, tm, sEffectType);
		//if (pObj2)
		//pBreakMgr->CreateSurfaceEffect( pObj2,tm,sEffectType );
	}

	IStatObj::SSubObject* pSubObj;

	if (pStatObj && (pSubObj = pStatObj->GetSubObject(pBreakEvent->idJoint)) &&
	    pSubObj->nType == STATIC_SUB_OBJECT_DUMMY && !strncmp(pSubObj->name, "$joint", 6))
	{
		const char* ptr;

		if (ptr = strstr(pSubObj->properties, "effect"))
		{

			for (ptr += 6; *ptr && (*ptr == ' ' || *ptr == '='); ptr++)
				;
			if (*ptr && FxAllowed())
			{
				char strEff[256];
				const char* const peff = ptr;
				while (*ptr && *ptr != '\n')
					++ptr;
				cry_strcpy(strEff, peff, (size_t)(ptr - peff));
				IParticleEffect* pEffect = gEnv->pParticleManager->FindEffect(strEff);
				pEffect->Spawn(true, IParticleEffect::ParticleLoc(pBreakEvent->pt, pBreakEvent->n));
			}
		}

		if (ptr = strstr(pSubObj->properties, "breaker"))
		{
			if (!(pStatObjEnt->GetFlags() & STATIC_OBJECT_GENERATED))
			{
				pStatObjEnt = pStatObjEnt->Clone(false, false, true);
				pStatObjEnt->SetFlags(pStatObjEnt->GetFlags() | STATIC_OBJECT_GENERATED);
				if (pCEntity)
					pCEntity->SetStatObj(pStatObjEnt, ENTITY_SLOT_ACTUAL, false);
				else if (pRenderNode)
				{
					IBreakableManager::SCreateParams createParams;
					createParams.pSrcStaticRenderNode = pRenderNode;
					createParams.fScale = nodeTM.GetColumn(0).len();
					createParams.nSlotIndex = 0;
					createParams.worldTM = nodeTM;
					createParams.nMatLayers = pRenderNode->GetMaterialLayers();
					createParams.nRenderNodeFlags = pRenderNode->GetRndFlags();
					createParams.pCustomMtl = pRenderNode->GetMaterial();
					createParams.nEntityFlagsAdd = ENTITY_FLAG_MODIFIED_BY_PHYSICS;
					createParams.pName = pRenderNode->GetName();
					((CBreakableManager*)GetIEntitySystem()->GetBreakableManager())->CreateObjectAsEntity(
					  pStatObjEnt, pBreakEvent->pEntity[0], pBreakEvent->pEntity[0], createParams, true);
				}
			}

			IStatObj::SSubObject* pSubObj1;
			const char* piecesStr;
			for (int i = 0; i < 2; i++)
				if ((pSubObj = pStatObjEnt->GetSubObject(pBreakEvent->partid[i])) && pSubObj->pStatObj &&
				    (piecesStr = strstr(pSubObj->properties, "pieces=")) && (pSubObj1 = pStatObj->FindSubObject(piecesStr + 7)) &&
				    pSubObj1->pStatObj)
				{
					pSubObj->pStatObj->Release();
					(pSubObj->pStatObj = pSubObj1->pStatObj)->AddRef();
				}
			pStatObjEnt->Invalidate(false);
		}
	}

	return 1;
}

void CPhysicsEventListener::RegisterPhysicCallbacks()
{
	if (m_pPhysics)
	{
		m_pPhysics->AddEventClient(EventPhysStateChange::id, OnStateChange, 1);
		m_pPhysics->AddEventClient(EventPhysBBoxOverlap::id, OnBBoxOverlap, 1);
		m_pPhysics->AddEventClient(EventPhysPostStep::id, OnPostStep, 1);
		m_pPhysics->AddEventClient(EventPhysPostStep::id, OnPostStepImmediate, 0);
		m_pPhysics->AddEventClient(EventPhysUpdateMesh::id, OnUpdateMesh, 1);
		m_pPhysics->AddEventClient(EventPhysUpdateMesh::id, OnUpdateMesh, 0);
		m_pPhysics->AddEventClient(EventPhysCreateEntityPart::id, OnCreatePhysEntityPart, 1);
		m_pPhysics->AddEventClient(EventPhysCreateEntityPart::id, OnCreatePhysEntityPart, 0);
		m_pPhysics->AddEventClient(EventPhysRemoveEntityParts::id, OnRemovePhysEntityParts, 1);
		m_pPhysics->AddEventClient(EventPhysRevealEntityPart::id, OnRevealPhysEntityPart, 1);
		m_pPhysics->AddEventClient(EventPhysCollision::id, OnCollision, 1, 1000.0f);
		m_pPhysics->AddEventClient(EventPhysJointBroken::id, OnJointBreak, 1);
		m_pPhysics->AddEventClient(EventPhysPostPump::id, OnPostPump, 1);
	}
}

void CPhysicsEventListener::UnregisterPhysicCallbacks()
{
	if (m_pPhysics)
	{
		m_pPhysics->RemoveEventClient(EventPhysStateChange::id, OnStateChange, 1);
		m_pPhysics->RemoveEventClient(EventPhysBBoxOverlap::id, OnBBoxOverlap, 1);
		m_pPhysics->RemoveEventClient(EventPhysPostStep::id, OnPostStep, 1);
		m_pPhysics->RemoveEventClient(EventPhysPostStep::id, OnPostStepImmediate, 0);
		m_pPhysics->RemoveEventClient(EventPhysUpdateMesh::id, OnUpdateMesh, 1);
		m_pPhysics->RemoveEventClient(EventPhysUpdateMesh::id, OnUpdateMesh, 0);
		m_pPhysics->RemoveEventClient(EventPhysCreateEntityPart::id, OnCreatePhysEntityPart, 1);
		m_pPhysics->RemoveEventClient(EventPhysCreateEntityPart::id, OnCreatePhysEntityPart, 0);
		m_pPhysics->RemoveEventClient(EventPhysRemoveEntityParts::id, OnRemovePhysEntityParts, 1);
		m_pPhysics->RemoveEventClient(EventPhysRevealEntityPart::id, OnRevealPhysEntityPart, 1);
		m_pPhysics->RemoveEventClient(EventPhysCollision::id, OnCollision, 1);
		m_pPhysics->RemoveEventClient(EventPhysJointBroken::id, OnJointBreak, 1);
		m_pPhysics->RemoveEventClient(EventPhysPostPump::id, OnPostPump, 1);
		stl::free_container(m_physVisAreaUpdateVector);
	}
}
