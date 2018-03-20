// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 
#include "stdafx.h"

#include "NetEntity.h"
#include "Entity.h"
#include <CryGame/IGameFramework.h>

#include <set>

class CNetEntity;
static std::set<CNetEntity*> g_updateSchedulingProfile;
static CryCriticalSection g_updateSchedulingProfileCritSec;

CNetEntity::CNetEntity(IEntity *entity_)
	: m_pEntity(entity_)
	, m_channelId(0)
	, m_enabledAspects(NET_ASPECT_ALL)
	, m_delegatableAspects(NET_ASPECT_ALL)
	, m_isBoundToNetwork(false)
	, m_bNoSyncPhysics(false)
	, m_pProfileManager(0)
	, m_cachedParentId(0)
	, m_schedulingProfiles(gEnv->pGameFramework->GetEntitySchedulerProfiles(entity_))
	, m_hasAuthority(false)
{
	for (int i = 0; i < NUM_ASPECTS; i++)
		m_profiles[i] = 255;
};

CNetEntity::~CNetEntity()
{
	g_updateSchedulingProfile.erase(this);
}

bool CNetEntity::BindToNetwork(EBindToNetworkMode mode)
{
	return BindToNetworkWithParent(mode, 0);
}

NetworkAspectType CNetEntity::CombineAspects()
{
	static const NetworkAspectType gameObjectAspects =
		eEA_GameClientDynamic |
		eEA_GameServerDynamic |
		eEA_GameClientStatic |
		eEA_GameServerStatic |
		eEA_Aspect31 |
		eEA_GameClientA |
		eEA_GameServerA |
		eEA_GameClientB |
		eEA_GameServerB |
		eEA_GameClientC |
		eEA_GameServerC |
		eEA_GameClientD |
		eEA_GameClientE |
		eEA_GameClientF |
		eEA_GameClientG |
		eEA_GameClientH |
		eEA_GameClientI |
		eEA_GameClientJ |
		eEA_GameClientK |
		eEA_GameServerD |
		eEA_GameClientL |
		eEA_GameClientM |
		eEA_GameClientN |
		eEA_GameClientO |
		eEA_GameClientP |
		eEA_GameServerE;

	NetworkAspectType aspects = 0;
	((CEntity *)m_pEntity)->m_components.ForEachSorted([&aspects](const SEntityComponentRecord& componentRecord) {
		aspects |= componentRecord.pComponent->GetNetSerializeAspectMask();
	});
	aspects &= gameObjectAspects;

	if (!m_bNoSyncPhysics && (m_pEntity->GetPhysicalEntity() || m_pProfileManager))
	{
		aspects |= eEA_Physics;
	}

	if (m_pEntity->GetProxy(ENTITY_PROXY_SCRIPT))
	{
		aspects |= eEA_Script;
	}

	return aspects;
}

bool CNetEntity::BindToNetworkWithParent(EBindToNetworkMode mode, EntityId parentId)
{
	if (!gEnv->pNetContext)
		return false;

	if (m_isBoundToNetwork)
	{
		switch (mode)
		{
		case eBTNM_NowInitialized:
			if (!m_isBoundToNetwork)
				return false;
			CRY_ASSERT(parentId == 0);
			parentId = m_cachedParentId;
			// fall through
		case eBTNM_Force:
			m_isBoundToNetwork = false;
			break;
		case eBTNM_Normal:
			return true;
		}
	}
	else if (mode == eBTNM_NowInitialized)
		return false;

	if (m_pEntity->GetFlags() & (ENTITY_FLAG_CLIENT_ONLY | ENTITY_FLAG_SERVER_ONLY))
		return false;

	if (!m_pEntity->IsInitialized())
	{
		m_cachedParentId = parentId;
		m_isBoundToNetwork = true;
		return true;
	}

	if (gEnv->bServer)
	{
		// Obscure bit: if GameObject's m_channelId was set before binding,
		// it indicates that this entity is a player (one and only) for that channel.
		if (GetChannelId())
		{
			gEnv->pGameFramework->SetServerChannelPlayerId(GetChannelId(), m_pEntity->GetId());
		}

		bool isStatic = gEnv->pGameFramework->IsInLevelLoad();
		if (m_pEntity->GetFlags() & ENTITY_FLAG_NEVER_NETWORK_STATIC)
			isStatic = false;

		auto eid = m_pEntity->GetId();
		gEnv->pNetContext->BindObject(eid, parentId, CombineAspects(), isStatic);
		gEnv->pNetContext->SetDelegatableMask(eid, m_delegatableAspects);
	}

	m_isBoundToNetwork = true;
	{
		AUTO_LOCK(g_updateSchedulingProfileCritSec);
		g_updateSchedulingProfile.insert(this);
	}

	return true;
}

bool CNetEntity::IsAspectDelegatable(NetworkAspectType aspect) const
{
	return (m_delegatableAspects & aspect) ? true : false;
}

bool CNetEntity::CaptureProfileManager(IGameObjectProfileManager* pPM)
{
	if (m_pProfileManager || !pPM)
		return false;
	m_pProfileManager = pPM;
	return true;
}
void CNetEntity::ReleaseProfileManager(IGameObjectProfileManager* pPM)
{
	if (m_pProfileManager != pPM)
		return;
	m_pProfileManager = 0;
}

void CNetEntity::ClearProfileManager()
{
	m_pProfileManager = 0;
}
bool CNetEntity::HasProfileManager()
{
	return m_pProfileManager != nullptr;
}

bool CNetEntity::NetSerializeEntity(TSerialize ser, EEntityAspects aspect, uint8 profile, int flags)
{
	NetworkAspectType aspects = 0;
	((CEntity *)m_pEntity)->m_components.ForEachSorted([&](const SEntityComponentRecord& componentRecord) {
		componentRecord.pComponent->NetSerialize(ser, aspect, profile, flags);
	});

	// #netentity: compare to GameContext::SynchObject, physics aspect. what happens there and here?
	if (aspect == eEA_Physics && !m_pProfileManager)
	{
		m_pEntity->PhysicsNetSerialize(ser);
	}

	return true;
}

void CNetEntity::RmiRegister(SRmiHandler& handler)
{
	auto found = std::find_if(m_rmiHandlers.begin(), m_rmiHandlers.end(),
		[&handler](SRmiHandler &p) { return p.decoder == handler.decoder; });
	CRY_ASSERT_MESSAGE(found == m_rmiHandlers.end(), "Registering a duplicate RMI message.");

	CRY_ASSERT_MESSAGE(m_rmiHandlers.size() < std::numeric_limits<decltype(SRmiIndex::value)>::max(),
		"Too many RMIs registered for the entity %s (%d)",
		m_pEntity->GetName(), m_pEntity->GetId());

	m_rmiHandlers.push_back(handler);
}

INetEntity::SRmiIndex CNetEntity::RmiByDecoder(SRmiHandler::DecoderF decoder, SRmiHandler **handler)
{
	auto found = std::find_if(m_rmiHandlers.begin(), m_rmiHandlers.end(),
		[&decoder](SRmiHandler &p) { return p.decoder == decoder; });
	CRY_ASSERT_MESSAGE(found != m_rmiHandlers.end(), "Sending an unregistered RMI message.");

	*handler = &*found;
	return SRmiIndex(found - m_rmiHandlers.begin());
}

INetEntity::SRmiHandler::DecoderF CNetEntity::RmiByIndex(const SRmiIndex idx)
{
	return m_rmiHandlers[idx.value].decoder;
}

void CNetEntity::SetAuthority(bool auth)
{
	m_hasAuthority = auth;

	{
		AUTO_LOCK(g_updateSchedulingProfileCritSec);
		g_updateSchedulingProfile.insert(this);
	}

	SEntityEvent ev(ENTITY_EVENT_SET_AUTHORITY);
	ev.nParam[0] = auth;
	m_pEntity->SendEvent(ev);

	// Physics authority previously ignored delegatableMask.
	if (m_delegatableAspects & eEA_Physics)
	{
		if (IPhysicalEntity* pPhysicalEntity = m_pEntity->GetPhysics())
		{
			pPhysicalEntity->SetNetworkAuthority(auth ? 1 : 0);
		}
	}

}

bool CNetEntity::HasAuthority() const
{
	return m_hasAuthority;
}

void CNetEntity::MarkAspectsDirty(NetworkAspectType aspects)
{
	if (gEnv->pNetContext)
		gEnv->pNetContext->ChangedAspects(m_pEntity->GetId(), aspects);
}

void CNetEntity::EnableAspect(NetworkAspectType aspects, bool enable)
{
	if (!gEnv->pNetContext)
		return;

	if (enable)
		m_enabledAspects |= aspects;
	else
		m_enabledAspects &= ~aspects;

	gEnv->pNetContext->EnableAspects(m_pEntity->GetId(), aspects, enable);
}

void CNetEntity::EnableDelegatableAspect(NetworkAspectType aspects, bool enable)
{
	if (enable)
	{
		m_delegatableAspects |= aspects;
	}
	else
	{
		m_delegatableAspects &= ~aspects;
	}
}

void CNetEntity::SetChannelId(uint16 id)
{
	m_channelId = id;
}
uint8 CNetEntity::GetAspectProfile(EEntityAspects aspect)
{
	uint8 profile = 255;
	if (m_isBoundToNetwork)
	{
		if (gEnv->pNetContext)
			profile = gEnv->pNetContext->GetAspectProfile(m_pEntity->GetId(), aspect);
	}
	else
		profile = m_profiles[BitIndex(NetworkAspectType(aspect))];

	return profile;
}
bool CNetEntity::SetAspectProfile(EEntityAspects aspect, uint8 profile, bool fromNetwork)
{
	bool ok = DoSetAspectProfile(aspect, profile, fromNetwork);

	if (ok && aspect == eEA_Physics && m_isBoundToNetwork && gEnv->bMultiplayer)
	{
		if (IPhysicalEntity* pEnt = m_pEntity->GetPhysics())
		{
			pe_params_flags flags;
			flags.flagsOR = pef_log_collisions;
			pEnt->SetParams(&flags);
		}
	}

	return ok;
}

bool CNetEntity::DoSetAspectProfile(EEntityAspects aspect, uint8 profile, bool fromNetwork)
{
	if (m_isBoundToNetwork)
	{
		if (fromNetwork)
		{
			if (m_pProfileManager)
			{
				if (m_pProfileManager->SetAspectProfile(aspect, profile))
				{
					m_profiles[BitIndex(NetworkAspectType(aspect))] = profile;
					return true;
				}
			}
			else
				return false;
		}
		else
		{
			if (gEnv->pNetContext)
			{
				gEnv->pNetContext->SetAspectProfile(m_pEntity->GetId(), aspect, profile);
				m_profiles[BitIndex(NetworkAspectType(aspect))] = profile;
				return true;
			}
			return false;
		}
	}
	else if (m_pProfileManager)
	{
		//CRY_ASSERT( !fromNetwork );
		if (m_pProfileManager->SetAspectProfile(aspect, profile))
		{
			m_profiles[BitIndex(NetworkAspectType(aspect))] = profile;
			return true;
		}
	}
	return false;
}

void CNetEntity::SetNetworkParent(EntityId id)
{
	if (!m_pEntity->IsInitialized())
	{
		m_cachedParentId = id;
		return;
	}

	if (gEnv->pNetContext)
	{
		gEnv->pNetContext->SetParentObject(m_pEntity->GetId(), id);
	}
}

NetworkAspectType CNetEntity::GetEnabledAspects() const
{
	return m_enabledAspects;
}

uint8 CNetEntity::GetDefaultProfile(EEntityAspects aspect)
{
	if (m_pProfileManager)
		return m_pProfileManager->GetDefaultProfile(aspect);
	else
		return 0;
}

void CNetEntity::UpdateSchedulingProfiles()
{
	// We need to check NetContext here, because it's NULL in a dummy editor game session (or at least while starting up the editor).
	if (!gEnv->pNetContext)
		return;

	AUTO_LOCK(g_updateSchedulingProfileCritSec);
	for (auto it = g_updateSchedulingProfile.begin(); it != g_updateSchedulingProfile.end(); )
	{
		CNetEntity *obj = *it;
		if (obj->IsBoundToNetwork() &&
			gEnv->pNetContext->SetSchedulingParams(obj->m_pEntity->GetId(),
				obj->m_schedulingProfiles->normal, obj->m_schedulingProfiles->owned))
			it = g_updateSchedulingProfile.erase(it);
		else
			++it;
	}
}
