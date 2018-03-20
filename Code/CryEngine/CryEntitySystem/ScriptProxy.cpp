// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "stdafx.h"
#include "ScriptProxy.h"
#include "EntityScript.h"
#include "Entity.h"
#include "ScriptProperties.h"
#include "ScriptBind_Entity.h"
#include "EntitySystem.h"
#include <CryNetwork/ISerialize.h>
#include "EntityCVars.h"

#include <CryAnimation/CryCharAnimationParams.h>

#include <CryScriptSystem/IScriptSystem.h>

CRYREGISTER_CLASS(CEntityComponentLuaScript);

//////////////////////////////////////////////////////////////////////////
CEntityComponentLuaScript::CEntityComponentLuaScript()
	: m_pScript(nullptr)
	, m_pThis(nullptr)
	, m_fScriptUpdateRate(0.0f)
	, m_fScriptUpdateTimer(0.0f)
	, m_nCurrStateId(0)
	, m_bUpdateFuncImplemented(false)
	, m_bUpdateEnabledOverride(false)
	, m_bEnableSoundAreaEvents(false)
{
	m_componentFlags.Add(EEntityComponentFlags::Legacy);
	m_componentFlags.Add(EEntityComponentFlags::NoSave);
}

//////////////////////////////////////////////////////////////////////////
CEntityComponentLuaScript::~CEntityComponentLuaScript()
{
	SAFE_RELEASE(m_pThis);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::Initialize()
{
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::ChangeScript(IEntityScript* pScript, SEntitySpawnParams* params)
{
	if (pScript)
	{
		// Release current
		SAFE_RELEASE(m_pThis);

		m_pScript = static_cast<CEntityScript*>(pScript);
		m_nCurrStateId = 0;
		m_fScriptUpdateRate = 0;
		m_fScriptUpdateTimer = 0;
		m_bEnableSoundAreaEvents = false;

		// New object must be created here.
		CreateScriptTable(params);

		m_bUpdateFuncImplemented = CurrentState()->IsStateFunctionImplemented(ScriptState_OnUpdate);
		m_pEntity->UpdateComponentEventMask(this);
	}
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::EnableScriptUpdate(bool bEnable)
{
	m_bUpdateEnabledOverride = bEnable;
	m_pEntity->UpdateComponentEventMask(this);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::CreateScriptTable(SEntitySpawnParams* pSpawnParams)
{
	m_pThis = m_pScript->GetScriptSystem()->CreateTable();
	m_pThis->AddRef();
	//m_pThis->Clone( m_pScript->GetScriptTable() );

	if (gEnv->pEntitySystem)
	{
		//pEntitySystem->GetScriptBindEntity()->DelegateCalls( m_pThis );
		m_pThis->Delegate(m_pScript->GetScriptTable());
	}

	// Clone Properties table recursively.
	if (m_pScript->GetPropertiesTable())
	{
		// If entity have an archetype use shared property table.
		IEntityArchetype* pArchetype = m_pEntity->GetArchetype();
		if (!pArchetype)
		{
			// Custom properties table passed
			if (pSpawnParams && pSpawnParams->pPropertiesTable)
			{
				m_pThis->SetValue(SCRIPT_PROPERTIES_TABLE, pSpawnParams->pPropertiesTable);
			}
			else
			{
				SmartScriptTable pProps;
				pProps.Create(m_pScript->GetScriptSystem());
				pProps->Clone(m_pScript->GetPropertiesTable(), true, true);
				m_pThis->SetValue(SCRIPT_PROPERTIES_TABLE, pProps);
			}
		}
		else
		{
			IScriptTable* pPropertiesTable = pArchetype->GetProperties();
			if (pPropertiesTable)
				m_pThis->SetValue(SCRIPT_PROPERTIES_TABLE, pPropertiesTable);
		}

		SmartScriptTable pEntityPropertiesInstance;
		SmartScriptTable pPropsInst;
		if (m_pThis->GetValue("PropertiesInstance", pEntityPropertiesInstance))
		{
			pPropsInst.Create(m_pScript->GetScriptSystem());
			pPropsInst->Clone(pEntityPropertiesInstance, true, true);
			m_pThis->SetValue("PropertiesInstance", pPropsInst);
		}
	}

	// Set self.__this to this pointer of CScriptProxy
	ScriptHandle handle;
	handle.ptr = m_pEntity;
	m_pThis->SetValue("__this", handle);
	handle.n = m_pEntity->GetId();
	m_pThis->SetValue("id", handle);
	m_pThis->SetValue("class", m_pEntity->GetClass()->GetName());
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::Update(SEntityUpdateContext& ctx)
{
	// Shouldn't be the case, but we must not call Lua with a 0 frametime to avoid potential FPE
	assert(ctx.fFrameTime > FLT_EPSILON);

	if (CVar::pUpdateScript->GetIVal())
	{
		m_fScriptUpdateTimer -= ctx.fFrameTime;
		if (m_fScriptUpdateTimer <= 0)
		{
			ENTITY_PROFILER
			  m_fScriptUpdateTimer = m_fScriptUpdateRate;

			//////////////////////////////////////////////////////////////////////////
			// Script Update.
			m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnUpdate, ctx.fFrameTime);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::ProcessEvent(SEntityEvent& event)
{
	switch (event.event)
	{
	case ENTITY_EVENT_UPDATE:
		{
			SEntityUpdateContext* pCtx = (SEntityUpdateContext*)event.nParam[0];
			Update(*pCtx);
		}
		break;
	case ENTITY_EVENT_ANIM_EVENT:
		{
			const AnimEventInstance* const pAnimEvent = reinterpret_cast<const AnimEventInstance*>(event.nParam[0]);
			if (pAnimEvent)
			{
				SmartScriptTable eventData(gEnv->pScriptSystem);
				eventData->SetValue("customParam", pAnimEvent->m_CustomParameter);
				eventData->SetValue("jointName", pAnimEvent->m_BonePathName);
				eventData->SetValue("offset", pAnimEvent->m_vOffset);
				eventData->SetValue("direction", pAnimEvent->m_vDir);
				m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnAnimationEvent, pAnimEvent->m_EventName, eventData);
			}
		}
		break;
	case ENTITY_EVENT_DONE:
		if (m_pScript)
			m_pScript->Call_OnShutDown(m_pThis);
		break;
	case ENTITY_EVENT_RESET:
		// OnReset()
		m_pScript->Call_OnReset(m_pThis, event.nParam[0] == 1);
		break;
	case ENTITY_EVENT_INIT:
		{
			// Call Init.
			CallInitEvent(false);
		}
		break;
	case ENTITY_EVENT_TIMER:
		// OnTimer( nTimerId,nMilliseconds )
		m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnTimer, (int)event.nParam[0], (int)event.nParam[1]);
		break;
	case ENTITY_EVENT_XFORM:
		// OnMove()
		m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnMove);
		break;
	case ENTITY_EVENT_ATTACH:
		// OnBind( childEntity );
		{
			IEntity* pChildEntity = gEnv->pEntitySystem->GetEntity((EntityId)event.nParam[0]);
			if (pChildEntity)
			{
				IScriptTable* pChildEntityThis = pChildEntity->GetScriptTable();
				if (pChildEntityThis)
					m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnBind, pChildEntityThis);
			}
		}
		break;
	case ENTITY_EVENT_ATTACH_THIS:
		// OnBindThis( ParentEntity );
		{
			IEntity* pParentEntity = gEnv->pEntitySystem->GetEntity((EntityId)event.nParam[0]);
			if (pParentEntity)
			{
				IScriptTable* pParentEntityThis = pParentEntity->GetScriptTable();
				if (pParentEntityThis)
					m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnBindThis, pParentEntityThis);
				else
					m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnBindThis);
			}
		}
		break;
	case ENTITY_EVENT_DETACH:
		// OnUnbind( childEntity );
		{
			IEntity* pChildEntity = gEnv->pEntitySystem->GetEntity((EntityId)event.nParam[0]);
			if (pChildEntity)
			{
				IScriptTable* pChildEntityThis = pChildEntity->GetScriptTable();
				if (pChildEntityThis)
					m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnUnBind, pChildEntityThis);
			}
		}
		break;
	case ENTITY_EVENT_DETACH_THIS:
		// OnUnbindThis( ParentEntity );
		{
			IEntity* pParentEntity = gEnv->pEntitySystem->GetEntity((EntityId)event.nParam[0]);
			if (pParentEntity)
			{
				IScriptTable* pParentEntityThis = pParentEntity->GetScriptTable();
				if (pParentEntityThis)
					m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnUnBindThis, pParentEntityThis);
				else
					m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnUnBindThis);
			}
		}
		break;
	case ENTITY_EVENT_ENTERAREA:
		{
			if (m_bEnableSoundAreaEvents)
			{
				IEntity const* const pIEntity = gEnv->pEntitySystem->GetEntity(static_cast<EntityId>(event.nParam[0]));

				if (pIEntity != nullptr)
				{
					IEntity const* const pTriggerEntity = g_pIEntitySystem->GetEntity(static_cast<EntityId>(event.nParam[2]));
					IScriptTable* const pTriggerEntityScript = (pTriggerEntity ? pTriggerEntity->GetScriptTable() : nullptr);

					IScriptTable* const pTrgEntityThis = pIEntity->GetScriptTable();

					if (pTrgEntityThis != nullptr)
					{
						if (pTriggerEntityScript != nullptr)
						{
							m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnEnterArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0], pTriggerEntityScript);
						}
						else
						{
							m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnEnterArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
						}
					}

					// allow local player/camera source entities to trigger callback even without that entity having a scripttable
					if ((pIEntity->GetFlags() & (ENTITY_FLAG_LOCAL_PLAYER | ENTITY_FLAG_CAMERA_SOURCE)) > 0)
					{
						if (pTriggerEntityScript != nullptr)
						{
							m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLocalClientEnterArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0], pTriggerEntityScript);
						}
						else
						{
							m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLocalClientEnterArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
						}
					}

					if ((pIEntity->GetFlagsExtended() & (ENTITY_FLAG_EXTENDED_AUDIO_LISTENER)) > 0)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnAudioListenerEnterArea, pTrgEntityThis);
					}
				}
			}

			break;
		}
	case ENTITY_EVENT_MOVEINSIDEAREA:
		{
			if (m_bEnableSoundAreaEvents)
			{
				IEntity const* const pIEntity = gEnv->pEntitySystem->GetEntity(static_cast<EntityId>(event.nParam[0]));

				if (pIEntity != nullptr)
				{
					IScriptTable* const pTrgEntityThis = pIEntity->GetScriptTable();

					if (pTrgEntityThis != nullptr)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnProceedFadeArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
					}

					// allow local player/camera source entities to trigger callback even without that entity having a scripttable
					if ((pIEntity->GetFlags() & (ENTITY_FLAG_LOCAL_PLAYER | ENTITY_FLAG_CAMERA_SOURCE)) > 0)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLocalClientProceedFadeArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
					}

					if ((pIEntity->GetFlagsExtended() & (ENTITY_FLAG_EXTENDED_AUDIO_LISTENER)) > 0)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnAudioListenerProceedFadeArea, pTrgEntityThis, event.fParam[0]);
					}
				}
			}

			break;
		}
	case ENTITY_EVENT_LEAVEAREA:
		{
			if (m_bEnableSoundAreaEvents)
			{
				IEntity const* const pIEntity = gEnv->pEntitySystem->GetEntity(static_cast<EntityId>(event.nParam[0]));

				if (pIEntity != nullptr)
				{
					IEntity const* const pTriggerEntity = g_pIEntitySystem->GetEntity(static_cast<EntityId>(event.nParam[2]));
					IScriptTable* const pTriggerEntityScript = (pTriggerEntity ? pTriggerEntity->GetScriptTable() : nullptr);

					IScriptTable* const pTrgEntityThis = pIEntity->GetScriptTable();

					if (pTrgEntityThis != nullptr)
					{
						if (pTriggerEntityScript != nullptr)
						{
							m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLeaveArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0], pTriggerEntityScript);
						}
						else
						{
							m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLeaveArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
						}
					}

					// allow local player/camera source entities to trigger callback even without that entity having a scripttable
					if ((pIEntity->GetFlags() & (ENTITY_FLAG_LOCAL_PLAYER | ENTITY_FLAG_CAMERA_SOURCE)) > 0)
					{
						if (pTriggerEntityScript != nullptr)
						{
							m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLocalClientLeaveArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0], pTriggerEntityScript);
						}
						else
						{
							m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLocalClientLeaveArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
						}
					}

					if ((pIEntity->GetFlagsExtended() & (ENTITY_FLAG_EXTENDED_AUDIO_LISTENER)) > 0)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnAudioListenerLeaveArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0], event.fParam[1]);
					}
				}
			}

			break;
		}
	case ENTITY_EVENT_ENTERNEARAREA:
		{
			if (m_bEnableSoundAreaEvents)
			{
				IEntity const* const pIEntity = gEnv->pEntitySystem->GetEntity(static_cast<EntityId>(event.nParam[0]));

				if (pIEntity != nullptr)
				{
					IScriptTable* const pTrgEntityThis = pIEntity->GetScriptTable();

					if (pTrgEntityThis != nullptr)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnEnterNearArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
					}

					// allow local player/camera source entities to trigger callback even without that entity having a scripttable
					if ((pIEntity->GetFlags() & (ENTITY_FLAG_LOCAL_PLAYER | ENTITY_FLAG_CAMERA_SOURCE)) > 0)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLocalClientEnterNearArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
					}

					if ((pIEntity->GetFlagsExtended() & (ENTITY_FLAG_EXTENDED_AUDIO_LISTENER)) > 0)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnAudioListenerEnterNearArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
					}
				}
			}

			break;
		}
	case ENTITY_EVENT_LEAVENEARAREA:
		{
			if (m_bEnableSoundAreaEvents)
			{
				IEntity const* const pIEntity = gEnv->pEntitySystem->GetEntity(static_cast<EntityId>(event.nParam[0]));

				if (pIEntity != nullptr)
				{
					IScriptTable* const pTrgEntityThis = pIEntity->GetScriptTable();

					if (pTrgEntityThis != nullptr)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLeaveNearArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
					}

					// allow local player/camera source entities to trigger callback even without that entity having a scripttable
					if ((pIEntity->GetFlags() & (ENTITY_FLAG_LOCAL_PLAYER | ENTITY_FLAG_CAMERA_SOURCE)) > 0)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLocalClientLeaveNearArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
					}

					if ((pIEntity->GetFlagsExtended() & (ENTITY_FLAG_EXTENDED_AUDIO_LISTENER)) > 0)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnAudioListenerLeaveNearArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0], event.fParam[1]);
					}
				}
			}

			break;
		}
	case ENTITY_EVENT_MOVENEARAREA:
		{
			if (m_bEnableSoundAreaEvents)
			{
				IEntity const* const pIEntity = gEnv->pEntitySystem->GetEntity(static_cast<EntityId>(event.nParam[0]));

				if (pIEntity != nullptr)
				{
					IScriptTable* const pTrgEntityThis = pIEntity->GetScriptTable();

					if (pTrgEntityThis != nullptr)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnMoveNearArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0], event.fParam[1]);
					}

					// allow local player/camera source entities to trigger callback even without that entity having a scripttable
					if ((pIEntity->GetFlags() & (ENTITY_FLAG_LOCAL_PLAYER | ENTITY_FLAG_CAMERA_SOURCE)) > 0)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLocalClientMoveNearArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0], event.fParam[1]);
					}

					if ((pIEntity->GetFlagsExtended() & (ENTITY_FLAG_EXTENDED_AUDIO_LISTENER)) > 0)
					{
						m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnAudioListenerMoveNearArea, pTrgEntityThis, static_cast<int>(event.nParam[1]), event.fParam[0]);
					}
				}
			}

			break;
		}
	case ENTITY_EVENT_PHYS_BREAK:
		{
			EventPhysJointBroken* pBreakEvent = (EventPhysJointBroken*)event.nParam[0];
			if (pBreakEvent)
			{
				Vec3 pBreakPos = pBreakEvent->pt;
				int nBreakPartId = pBreakEvent->partid[0];
				int nBreakOtherEntityPartId = pBreakEvent->partid[1];
				m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnPhysicsBreak, pBreakPos, nBreakPartId, nBreakOtherEntityPartId);
			}
		}
		break;
	case ENTITY_EVENT_AUDIO_TRIGGER_ENDED:
		{
			CryAudio::SRequestInfo const* const pRequestInfo = reinterpret_cast<CryAudio::SRequestInfo const* const>(event.nParam[0]);
			ScriptHandle handle;
			handle.n = static_cast<UINT_PTR>(pRequestInfo->audioControlId);
			m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnSoundDone, handle);
		}
		break;
	case ENTITY_EVENT_LEVEL_LOADED:
		m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnLevelLoaded);
		break;
	case ENTITY_EVENT_START_LEVEL:
		m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnStartLevel);
		break;
	case ENTITY_EVENT_START_GAME:
		m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnStartGame);
		break;

	case ENTITY_EVENT_PRE_SERIALIZE:
		// Kill all timers.
		{
			// If state changed kill all old timers.
			m_pEntity->KillTimer(-1);
			m_nCurrStateId = 0;
		}
		break;

	case ENTITY_EVENT_POST_SERIALIZE:
		if (m_pThis->HaveValue("OnPostLoad"))
		{
			Script::CallMethod(m_pThis, "OnPostLoad");
		}
		break;
	case ENTITY_EVENT_HIDE:
		m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnHidden);
		break;
	case ENTITY_EVENT_UNHIDE:
		m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnUnhidden);
		break;
	case ENTITY_EVENT_XFORM_FINISHED_EDITOR:
		m_pScript->Call_OnTransformFromEditorDone(m_pThis);
		break;
	}
	;
}

//////////////////////////////////////////////////////////////////////////
uint64 CEntityComponentLuaScript::GetEventMask() const
{
	// All events except runtime expensive ones
	uint64 eventMasks = ~ENTITY_PERFORMANCE_EXPENSIVE_EVENTS_MASK;

	if (m_bUpdateFuncImplemented && m_bUpdateEnabledOverride)
	{
		eventMasks |= BIT64(ENTITY_EVENT_UPDATE);
	}

	return eventMasks;
}

//////////////////////////////////////////////////////////////////////////
bool CEntityComponentLuaScript::GotoState(const char* sStateName)
{
	int nStateId = m_pScript->GetStateId(sStateName);
	if (nStateId >= 0)
	{
		GotoState(nStateId);
	}
	else
	{
		EntityWarning("GotoState called with unknown state %s, in entity %s", sStateName, m_pEntity->GetEntityTextDescription().c_str());
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
bool CEntityComponentLuaScript::GotoState(int nState)
{
	if (nState == m_nCurrStateId)
		return true; // Already in this state.

	SScriptState* pState = m_pScript->GetState(nState);

	// Call End state event.
	m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnEndState);

	// If state changed kill all old timers.
	m_pEntity->KillTimer(-1);

	SEntityEvent levent;
	levent.event = ENTITY_EVENT_LEAVE_SCRIPT_STATE;
	levent.nParam[0] = m_nCurrStateId;
	levent.nParam[1] = 0;
	m_pEntity->SendEvent(levent);

	m_nCurrStateId = nState;

	// Call BeginState event.
	m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnBeginState);

	//////////////////////////////////////////////////////////////////////////
	// Repeat check if update script function is implemented.
	m_bUpdateFuncImplemented = CurrentState()->IsStateFunctionImplemented(ScriptState_OnUpdate);
	m_pEntity->UpdateComponentEventMask(this);

	/*
	   //////////////////////////////////////////////////////////////////////////
	   // Check if need ResolveCollisions for OnContact script function.
	   m_bUpdateOnContact = IsStateFunctionImplemented(ScriptState_OnContact);
	   //////////////////////////////////////////////////////////////////////////
	 */

	SEntityEvent eevent;
	eevent.event = ENTITY_EVENT_ENTER_SCRIPT_STATE;
	eevent.nParam[0] = nState;
	eevent.nParam[1] = 0;
	m_pEntity->SendEvent(eevent);

	return true;
}

//////////////////////////////////////////////////////////////////////////
bool CEntityComponentLuaScript::IsInState(const char* sStateName)
{
	int nStateId = m_pScript->GetStateId(sStateName);
	if (nStateId >= 0)
	{
		return nStateId == m_nCurrStateId;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CEntityComponentLuaScript::IsInState(int nState)
{
	return nState == m_nCurrStateId;
}

//////////////////////////////////////////////////////////////////////////
const char* CEntityComponentLuaScript::GetState()
{
	return m_pScript->GetStateName(m_nCurrStateId);
}

//////////////////////////////////////////////////////////////////////////
int CEntityComponentLuaScript::GetStateId()
{
	return m_nCurrStateId;
}

//////////////////////////////////////////////////////////////////////////
bool CEntityComponentLuaScript::NeedGameSerialize()
{
	if (!m_pThis)
		return false;

	if (m_fScriptUpdateRate != 0 || m_nCurrStateId != 0)
		return true;

	if (CVar::pEnableFullScriptSave && CVar::pEnableFullScriptSave->GetIVal())
		return true;

	if (m_pThis->HaveValue("OnSave") && m_pThis->HaveValue("OnLoad"))
		return true;

	return false;
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::SerializeProperties(TSerialize ser)
{
	if (ser.GetSerializationTarget() == eST_Network)
		return;

	// Saving.
	if (!(m_pEntity->GetFlags() & ENTITY_FLAG_UNREMOVABLE))
	{

		// Properties never serialized
		/*
		   if (!m_pEntity->GetArchetype())
		   {
		   // If not archetype also serialize properties table of the entity.
		   SerializeTable( ser, "Properties" );
		   }*/

		// Instance table always initialized for dynamic entities.
		SerializeTable(ser, "PropertiesInstance");
	}
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::GameSerialize(TSerialize ser)
{
	CHECK_SCRIPT_STACK;

	if (ser.GetSerializationTarget() != eST_Network)
	{
		MEMSTAT_CONTEXT(EMemStatContextTypes::MSC_Other, 0, "Script proxy serialization");

		if (NeedGameSerialize())
		{
			if (ser.BeginOptionalGroup("ScriptProxy", true))
			{
				ser.Value("scriptUpdateRate", m_fScriptUpdateRate);
				int currStateId = m_nCurrStateId;
				ser.Value("currStateId", currStateId);

				// Simulate state change
				if (m_nCurrStateId != currStateId)
				{
					// If state changed kill all old timers.
					m_pEntity->KillTimer(-1);
					m_nCurrStateId = currStateId;
				}
				if (ser.IsReading())
				{
					// Repeat check if update script function is implemented.
					m_bUpdateFuncImplemented = CurrentState()->IsStateFunctionImplemented(ScriptState_OnUpdate);
					m_pEntity->UpdateComponentEventMask(this);
				}

				if (CVar::pEnableFullScriptSave && CVar::pEnableFullScriptSave->GetIVal())
				{
					ser.Value("FullScriptData", m_pThis);
				}
				else if (m_pThis->HaveValue("OnSave") && m_pThis->HaveValue("OnLoad"))
				{
					//SerializeTable( ser, "Properties" );
					//SerializeTable( ser, "PropertiesInstance" );
					//SerializeTable( ser, "Events" );

					SmartScriptTable persistTable(m_pThis->GetScriptSystem());
					if (ser.IsWriting())
						Script::CallMethod(m_pThis, "OnSave", persistTable);
					ser.Value("ScriptData", persistTable.GetPtr());
					if (ser.IsReading())
						Script::CallMethod(m_pThis, "OnLoad", persistTable);
				}

				ser.EndGroup(); //ScriptProxy
			}
			else
			{
				// If we haven't already serialized the script proxy then reset it back to the default state.

				CRY_ASSERT(m_pScript);

				m_pScript->Call_OnReset(m_pThis, true);
			}
		}
	}
	else
	{
		int stateId = m_nCurrStateId;
		ser.Value("currStateId", stateId, 'sSts');
		if (ser.IsReading())
			GotoState(stateId);
	}
}

//////////////////////////////////////////////////////////////////////////
bool CEntityComponentLuaScript::HaveTable(const char* name)
{
	SmartScriptTable table;
	if (m_pThis && m_pThis->GetValue(name, table))
		return true;
	else
		return false;
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::SerializeTable(TSerialize ser, const char* name)
{
	CHECK_SCRIPT_STACK;

	SmartScriptTable table;
	if (ser.IsReading())
	{
		if (ser.BeginOptionalGroup(name, true))
		{
			table = SmartScriptTable(m_pThis->GetScriptSystem());
			ser.Value("table", table);
			m_pThis->SetValue(name, table);
			ser.EndGroup();
		}
	}
	else
	{
		if (m_pThis->GetValue(name, table))
		{
			ser.BeginGroup(name);
			ser.Value("table", table);
			ser.EndGroup();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::LegacySerializeXML(XmlNodeRef& entityNode, XmlNodeRef& componentNode, bool bLoading)
{
	// Initialize script properties.
	if (bLoading)
	{
		CScriptProperties scriptProps;
		// Initialize properties.
		// Script properties are currently stored in the entity node, not the component itself (legacy)
		scriptProps.SetProperties(entityNode, m_pThis);

		XmlNodeRef eventTargets = entityNode->findChild("EventTargets");
		if (eventTargets)
			SetEventTargets(eventTargets);
	}
}

//////////////////////////////////////////////////////////////////////////
struct SEntityEventTarget
{
	string   event;
	EntityId target;
	string   sourceEvent;
};

//////////////////////////////////////////////////////////////////////////
// Set event targets from the XmlNode exported by Editor.
void CEntityComponentLuaScript::SetEventTargets(XmlNodeRef& eventTargetsNode)
{
	std::set<string> sourceEvents;
	std::vector<SEntityEventTarget> eventTargets;

	IScriptSystem* pSS = GetIScriptSystem();

	for (int i = 0; i < eventTargetsNode->getChildCount(); i++)
	{
		XmlNodeRef eventNode = eventTargetsNode->getChild(i);

		SEntityEventTarget et;
		et.event = eventNode->getAttr("Event");
		if (!eventNode->getAttr("Target", et.target))
			et.target = 0; // failed reading...
		et.sourceEvent = eventNode->getAttr("SourceEvent");

		if (et.event.empty() || !et.target || et.sourceEvent.empty())
			continue;

		eventTargets.push_back(et);
		sourceEvents.insert(et.sourceEvent);
	}
	if (eventTargets.empty())
		return;

	SmartScriptTable pEventsTable;

	if (!m_pThis->GetValue("Events", pEventsTable))
	{
		pEventsTable = pSS->CreateTable();
		// assign events table to the entity self script table.
		m_pThis->SetValue("Events", pEventsTable);
	}

	for (auto const& sourceEvent : sourceEvents)
	{
		SmartScriptTable pTrgEvents(pSS);

		pEventsTable->SetValue(sourceEvent.c_str(), pTrgEvents);

		// Put target events to table.
		int trgEventIndex = 1;

		for (auto const& et : eventTargets)
		{
			if (et.sourceEvent == sourceEvent)
			{
				SmartScriptTable pTrgEvent(pSS);

				pTrgEvents->SetAt(trgEventIndex, pTrgEvent);
				trgEventIndex++;
				ScriptHandle hdl;
				hdl.n = et.target;
				pTrgEvent->SetAt(1, hdl);
				pTrgEvent->SetAt(2, et.event.c_str());
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::CallEvent(const char* sEvent)
{
	m_pScript->CallEvent(m_pThis, sEvent, (bool)true);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::CallEvent(const char* sEvent, float fValue)
{
	m_pScript->CallEvent(m_pThis, sEvent, fValue);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::CallEvent(const char* sEvent, bool bValue)
{
	m_pScript->CallEvent(m_pThis, sEvent, bValue);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::CallEvent(const char* sEvent, const char* sValue)
{
	m_pScript->CallEvent(m_pThis, sEvent, sValue);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::CallEvent(const char* sEvent, EntityId nEntityId)
{
	IScriptTable* pTable = nullptr;
	IEntity* const pIEntity = gEnv->pEntitySystem->GetEntity(nEntityId);

	if (pIEntity != nullptr)
	{
		pTable = pIEntity->GetScriptTable();
	}

	m_pScript->CallEvent(m_pThis, sEvent, pTable);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::CallEvent(const char* sEvent, const Vec3& vValue)
{
	m_pScript->CallEvent(m_pThis, sEvent, vValue);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::CallInitEvent(bool bFromReload)
{
	m_pScript->Call_OnInit(m_pThis, bFromReload);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::OnPreparedFromPool()
{
	m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnPreparedFromPool);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::OnCollision(CEntity* pTarget, int matId, const Vec3& pt, const Vec3& n, const Vec3& vel, const Vec3& targetVel, int partId, float mass)
{
	if (!CurrentState()->IsStateFunctionImplemented(ScriptState_OnCollision))
		return;

	FUNCTION_PROFILER(GetISystem(), PROFILE_ENTITY);

	if (!m_hitTable)
		m_hitTable.Create(gEnv->pScriptSystem);
	{
		Vec3 dir(0, 0, 0);
		CScriptSetGetChain chain(m_hitTable);
		chain.SetValue("normal", n);
		chain.SetValue("pos", pt);

		if (vel.GetLengthSquared() > 1e-6f)
		{
			dir = vel.GetNormalized();
			chain.SetValue("dir", dir);
		}

		chain.SetValue("velocity", vel);
		chain.SetValue("target_velocity", targetVel);
		chain.SetValue("target_mass", mass);
		chain.SetValue("partid", partId);
		chain.SetValue("backface", n.Dot(dir) >= 0);
		chain.SetValue("materialId", matId);

		if (pTarget)
		{
			ScriptHandle sh;
			sh.n = pTarget->GetId();

			if (pTarget->GetPhysics())
			{
				chain.SetValue("target_type", (int)pTarget->GetPhysics()->GetType());
			}
			else
			{
				chain.SetToNull("target_type");
			}

			chain.SetValue("target_id", sh);

			if (pTarget->GetScriptTable())
			{
				chain.SetValue("target", pTarget->GetScriptTable());
			}
		}
		else
		{
			chain.SetToNull("target_type");
			chain.SetToNull("target_id");
			chain.SetToNull("target");
		}
	}

	m_pScript->CallStateFunction(CurrentState(), m_pThis, ScriptState_OnCollision, m_hitTable);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::SendScriptEvent(int Event, IScriptTable* pParamters, bool* pRet)
{
	SScriptState* pState = CurrentState();
	for (int i = 0; i < NUM_STATES; i++)
	{
		if (m_pScript->ShouldExecuteCall(i) && pState->pStateFuns[i] && pState->pStateFuns[i]->pFunction[ScriptState_OnEvent])
		{
			if (pRet)
				Script::CallReturn(GetIScriptSystem(), pState->pStateFuns[i]->pFunction[ScriptState_OnEvent], m_pThis, Event, pParamters, *pRet);
			else
				Script::Call(GetIScriptSystem(), pState->pStateFuns[i]->pFunction[ScriptState_OnEvent], m_pThis, Event, pParamters);
			pRet = nullptr;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::SendScriptEvent(int Event, const char* str, bool* pRet)
{
	SScriptState* pState = CurrentState();
	for (int i = 0; i < NUM_STATES; i++)
	{
		if (m_pScript->ShouldExecuteCall(i) && pState->pStateFuns[i] && pState->pStateFuns[i]->pFunction[ScriptState_OnEvent])
		{
			if (pRet)
				Script::CallReturn(GetIScriptSystem(), pState->pStateFuns[i]->pFunction[ScriptState_OnEvent], m_pThis, Event, str, *pRet);
			else
				Script::Call(GetIScriptSystem(), pState->pStateFuns[i]->pFunction[ScriptState_OnEvent], m_pThis, Event, str);
			pRet = nullptr;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentLuaScript::SendScriptEvent(int Event, int nParam, bool* pRet)
{
	SScriptState* pState = CurrentState();
	for (int i = 0; i < NUM_STATES; i++)
	{
		if (m_pScript->ShouldExecuteCall(i) && pState->pStateFuns[i] && pState->pStateFuns[i]->pFunction[ScriptState_OnEvent])
		{
			if (pRet)
				Script::CallReturn(GetIScriptSystem(), pState->pStateFuns[i]->pFunction[ScriptState_OnEvent], m_pThis, Event, nParam, *pRet);
			else
				Script::Call(GetIScriptSystem(), pState->pStateFuns[i]->pFunction[ScriptState_OnEvent], m_pThis, Event, nParam);
			pRet = nullptr;
		}
	}
}

void CEntityComponentLuaScript::RegisterForAreaEvents(bool bEnable)
{
	m_bEnableSoundAreaEvents = bEnable;
}

bool CEntityComponentLuaScript::IsRegisteredForAreaEvents() const
{
	return m_bEnableSoundAreaEvents;
}

void CEntityComponentLuaScript::GetMemoryUsage(ICrySizer* pSizer) const
{
	pSizer->AddObject(this, sizeof(*this));
}

void CEntityComponentLuaScript::SetPhysParams(int type, IScriptTable* params)
{
	// This function can currently be called by a component created without an entity, hence the special case
	IPhysicalEntity* pPhysicalEntity = m_pEntity != nullptr ? m_pEntity->GetPhysics() : nullptr;
	((CEntitySystem*)gEnv->pEntitySystem)->GetScriptBindEntity()->SetEntityPhysicParams(nullptr, pPhysicalEntity, type, params);
}
