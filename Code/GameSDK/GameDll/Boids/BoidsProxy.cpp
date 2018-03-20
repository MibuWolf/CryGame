// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

// -------------------------------------------------------------------------
//  File name:   BoidsProxy.cpp
//  Version:     v1.00
//  Created:     2/10/2004 by Timur.
//  Compilers:   Visual Studio.NET
//  Description: 
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <CryEntitySystem/IEntitySystem.h>
#include "BoidsProxy.h"
#include "Flock.h"
#include <CryNetwork/ISerialize.h>

CRYREGISTER_CLASS(CBoidsProxy)
CRYREGISTER_CLASS(CBoidObjectProxy)

//////////////////////////////////////////////////////////////////////////
CBoidsProxy::CBoidsProxy()
	: m_pFlock(NULL)
	, m_playersInCount(0)
{
}

//////////////////////////////////////////////////////////////////////////
CBoidsProxy::~CBoidsProxy()
{
	if (m_pFlock)
		delete m_pFlock;
}

//////////////////////////////////////////////////////////////////////////
void CBoidsProxy::Initialize()
{
}

//////////////////////////////////////////////////////////////////////////
void CBoidsProxy::Reload(IEntity *pEntity, SEntitySpawnParams &params)
{
	m_pEntity = pEntity;

	if (m_pFlock)
		m_pFlock->SetPos(pEntity->GetWorldPos());

	m_playersInCount = 0;
}

//////////////////////////////////////////////////////////////////////////
void CBoidsProxy::Release()
{
	delete this;
}

//////////////////////////////////////////////////////////////////////////
void CBoidsProxy::ProcessEvent(SEntityEvent &event)
{
	switch (event.event) {
	case ENTITY_EVENT_XFORM:
		if (m_pFlock)
			m_pFlock->SetPos(m_pEntity->GetWorldPos());
		break;
	case ENTITY_EVENT_PRE_SERIALIZE:
		if (m_pFlock)
			m_pFlock->DeleteEntities(true);
		break;
	case ENTITY_EVENT_ENTERAREA:
		OnTrigger(true, event);
		break;
	case ENTITY_EVENT_LEAVEAREA:
		OnTrigger(false, event);
		break;
	case ENTITY_EVENT_RESET:
		if (m_pFlock)
			m_pFlock->Reset();
		break;
	case ENTITY_EVENT_UPDATE:
		if (m_pFlock)
			m_pFlock->Update(&gEnv->pSystem->GetViewCamera());
		break;
	}
}

uint64 CBoidsProxy::GetEventMask() const
{
	uint64 eventMask = BIT64(ENTITY_EVENT_XFORM) |
		BIT64(ENTITY_EVENT_PRE_SERIALIZE) |
		BIT64(ENTITY_EVENT_ENTERAREA) |
		BIT64(ENTITY_EVENT_LEAVEAREA) |
		BIT64(ENTITY_EVENT_RESET);

	if (m_playersInCount > 0)
	{
		eventMask |= BIT64(ENTITY_EVENT_UPDATE);
	}

	return eventMask;
}

//////////////////////////////////////////////////////////////////////////
void CBoidsProxy::GameSerialize(TSerialize ser)
{
}

//////////////////////////////////////////////////////////////////////////
void CBoidsProxy::SetFlock(CFlock *pFlock)
{
	m_pFlock = pFlock;

	if (!pFlock)
		return;

	// Update trigger based on new visibility distance settings.
	float fMaxDist = pFlock->GetMaxVisibilityDistance();

	/*
	CTriggerProxy *pTrigger = (CTriggerProxy*)m_pEntity->CreateProxy( ENTITY_PROXY_TRIGGER );
	if (!pTrigger)
	return;

	AABB bbox;
	bbox.min = -Vec3(fMaxDist,fMaxDist,fMaxDist);
	bbox.max = Vec3(fMaxDist,fMaxDist,fMaxDist);
	pTrigger->SetTriggerBounds( bbox );
	*/

	IEntityAreaComponent* pArea = crycomponent_cast<IEntityAreaComponent*>(m_pEntity->CreateProxy(ENTITY_PROXY_AREA));
	if (!pArea)
		return;

	pArea->SetFlags(pArea->GetFlags() & IEntityAreaComponent::FLAG_NOT_SERIALIZE);
	pArea->SetSphere(Vec3(0, 0, 0), fMaxDist);
	if (gEnv->pEntitySystem->EntitiesUseGUIDs())
		pArea->AddEntity(m_pEntity->GetGuid());
	else
		pArea->AddEntity(m_pEntity->GetId()); // add itself.

}

//////////////////////////////////////////////////////////////////////////
void CBoidsProxy::OnTrigger(bool bEnter, SEntityEvent &event)
{
	EntityId whoId = (EntityId)event.nParam[0];
	IEntity *pEntity = gEnv->pEntitySystem->GetEntity(whoId);

	if (pEntity)
	{
		if (pEntity->GetFlags() & ENTITY_FLAG_LOCAL_PLAYER)
		{
			if (bEnter)
				m_playersInCount++;
			else
				m_playersInCount--;

			// Make sure GetEventMask is called to check whether we want updates
			m_pEntity->UpdateComponentEventMask(this);

			if (m_playersInCount == 1)
			{
				// Activates entity when player is nearby.
				if (m_pFlock)
					m_pFlock->SetEnabled(true);
			}
			if (m_playersInCount <= 0)
			{
				// Activates entity when player is nearby.
				m_playersInCount = 0;
				if (m_pFlock)
					m_pFlock->SetEnabled(false);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
CBoidObjectProxy::CBoidObjectProxy()
	: m_pBoid(NULL)
	, m_pEntity(NULL)
{
}

//////////////////////////////////////////////////////////////////////////
void CBoidObjectProxy::Initialize()
{
}

//////////////////////////////////////////////////////////////////////////
void CBoidObjectProxy::ProcessEvent(SEntityEvent &event)
{
	if (m_pBoid)
		m_pBoid->OnEntityEvent(event);
}

uint64 CBoidObjectProxy::GetEventMask() const
{
	return
		BIT64(ENTITY_EVENT_DONE) |
		BIT64(ENTITY_EVENT_COLLISION);
}

//////////////////////////////////////////////////////////////////////////
void CBoidObjectProxy::GameSerialize(TSerialize ser)
{

}
