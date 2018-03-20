// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "stdafx.h"
#include "ATLAudioObject.h"
#include "AudioCVars.h"
#include "AudioEventManager.h"
#include "AudioSystem.h"
#include "AudioStandaloneFileManager.h"
#include <IAudioImpl.h>
#include <CryString/HashedString.h>
#include <CryEntitySystem/IEntitySystem.h>

#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	#include <CryRenderer/IRenderAuxGeom.h>
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

namespace CryAudio
{
TriggerInstanceId CryAudio::CATLAudioObject::s_triggerInstanceIdCounter = 1;
CSystem* CryAudio::CATLAudioObject::s_pAudioSystem = nullptr;
CAudioEventManager* CryAudio::CATLAudioObject::s_pEventManager = nullptr;
CAudioStandaloneFileManager* CryAudio::CATLAudioObject::s_pStandaloneFileManager = nullptr;

//////////////////////////////////////////////////////////////////////////
CATLAudioObject::CATLAudioObject()
	: m_pImplData(nullptr)
	, m_maxRadius(0.0f)
	, m_flags(EObjectFlags::InUse)
	, m_previousVelocity(0.0f)
	, m_propagationProcessor(m_attributes.transformation)
	, m_occlusionFadeOutDistance(0.0f)
	, m_entityId(INVALID_ENTITYID)
	, m_numPendingSyncCallbacks(0)
{}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::Release()
{
	// Do not clear the object's name though!
	m_activeEvents.clear();
	m_triggerImplStates.clear();
	m_activeStandaloneFiles.clear();

	for (auto& triggerStatesPair : m_triggerStates)
	{
		triggerStatesPair.second.numLoadingEvents = 0;
		triggerStatesPair.second.numPlayingEvents = 0;
	}

	m_pImplData = nullptr;
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ReportStartingTriggerInstance(TriggerInstanceId const audioTriggerInstanceId, ControlId const audioTriggerId)
{
	SAudioTriggerInstanceState& audioTriggerInstanceState = m_triggerStates.emplace(audioTriggerInstanceId, SAudioTriggerInstanceState()).first->second;
	audioTriggerInstanceState.audioTriggerId = audioTriggerId;
	audioTriggerInstanceState.flags |= ETriggerStatus::Starting;
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ReportStartedTriggerInstance(
  TriggerInstanceId const audioTriggerInstanceId,
  void* const pOwnerOverride,
  void* const pUserData,
  void* const pUserDataOwner,
  ERequestFlags const flags)
{
	ObjectTriggerStates::iterator iter(m_triggerStates.find(audioTriggerInstanceId));

	if (iter != m_triggerStates.end())
	{
		SAudioTriggerInstanceState& audioTriggerInstanceState = iter->second;

		if (audioTriggerInstanceState.numPlayingEvents > 0 || audioTriggerInstanceState.numLoadingEvents > 0)
		{
			audioTriggerInstanceState.pOwnerOverride = pOwnerOverride;
			audioTriggerInstanceState.pUserData = pUserData;
			audioTriggerInstanceState.pUserDataOwner = pUserDataOwner;
			audioTriggerInstanceState.flags &= ~ETriggerStatus::Starting;
			audioTriggerInstanceState.flags |= ETriggerStatus::Playing;

			if ((flags& ERequestFlags::DoneCallbackOnAudioThread) > 0)
			{
				audioTriggerInstanceState.flags |= ETriggerStatus::CallbackOnAudioThread;
			}
			else if ((flags& ERequestFlags::DoneCallbackOnExternalThread) > 0)
			{
				audioTriggerInstanceState.flags |= ETriggerStatus::CallbackOnExternalThread;
			}
		}
		else
		{
			// All of the events have either finished before we got here or never started.
			// So we report this trigger as finished immediately.
			ReportFinishedTriggerInstance(iter);
		}
	}
	else
	{
		g_logger.Log(ELogType::Warning, "Reported a started instance %u that couldn't be found on an object", audioTriggerInstanceId);
	}
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ReportStartedEvent(CATLEvent* const pEvent)
{
	m_activeEvents.insert(pEvent);
	m_triggerImplStates.insert(std::make_pair(pEvent->m_audioTriggerImplId, SAudioTriggerImplState()));

	// Update the radius where events playing in this audio object are audible
	if (pEvent->m_pTrigger)
	{
		m_maxRadius = std::max(pEvent->m_pTrigger->m_maxRadius, m_maxRadius);
		m_occlusionFadeOutDistance = std::max(pEvent->m_pTrigger->m_occlusionFadeOutDistance, m_occlusionFadeOutDistance);
	}

	ObjectTriggerStates::iterator const iter(m_triggerStates.find(pEvent->m_audioTriggerInstanceId));

	if (iter != m_triggerStates.end())
	{
		SAudioTriggerInstanceState& audioTriggerInstanceState = iter->second;

		switch (pEvent->m_state)
		{
		case EEventState::Playing:
			{
				++(audioTriggerInstanceState.numPlayingEvents);

				break;
			}
		case EEventState::PlayingDelayed:
			{
				CRY_ASSERT(audioTriggerInstanceState.numLoadingEvents > 0);
				--(audioTriggerInstanceState.numLoadingEvents);
				++(audioTriggerInstanceState.numPlayingEvents);
				pEvent->m_state = EEventState::Playing;

				break;
			}
		case EEventState::Loading:
			{
				++(audioTriggerInstanceState.numLoadingEvents);

				break;
			}
		case EEventState::Unloading:
			{
				// not handled currently
				break;
			}
		default:
			{
				// unknown event state!
				CRY_ASSERT(false);

				break;
			}
		}
	}
	else
	{
		// must exist!
		CRY_ASSERT(false);
	}
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ReportFinishedEvent(CATLEvent* const pEvent, bool const bSuccess)
{
	m_activeEvents.erase(pEvent);
	m_triggerImplStates.erase(pEvent->m_audioTriggerImplId);

	// recalculate the max radius of the audio object
	m_maxRadius = 0.0f;
	m_occlusionFadeOutDistance = 0.0f;
	for (auto const pEvent : m_activeEvents)
	{
		if (pEvent->m_pTrigger)
		{
			m_maxRadius = std::max(pEvent->m_pTrigger->m_maxRadius, m_maxRadius);
			m_occlusionFadeOutDistance = std::max(pEvent->m_pTrigger->m_occlusionFadeOutDistance, m_occlusionFadeOutDistance);
		}
	}

	ObjectTriggerStates::iterator iter(m_triggerStates.begin());

	if (FindPlace(m_triggerStates, pEvent->m_audioTriggerInstanceId, iter))
	{
		switch (pEvent->m_state)
		{
		case EEventState::Playing:
			{
				SAudioTriggerInstanceState& audioTriggerInstanceState = iter->second;
				CRY_ASSERT(audioTriggerInstanceState.numPlayingEvents > 0);

				if (--(audioTriggerInstanceState.numPlayingEvents) == 0 &&
				    audioTriggerInstanceState.numLoadingEvents == 0 &&
				    (audioTriggerInstanceState.flags & ETriggerStatus::Starting) == 0)
				{
					ReportFinishedTriggerInstance(iter);
				}

				break;
			}
		case EEventState::Loading:
			{
				if (bSuccess)
				{
					ReportFinishedLoadingTriggerImpl(pEvent->m_audioTriggerImplId, true);
				}

				break;
			}
		case EEventState::Unloading:
			{
				if (bSuccess)
				{
					ReportFinishedLoadingTriggerImpl(pEvent->m_audioTriggerImplId, false);
				}

				break;
			}
		default:
			{
				// unknown event state!
				CRY_ASSERT(false);

				break;
			}
		}
	}
	else
	{
		if (pEvent->m_pTrigger != nullptr)
		{
#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
			g_logger.Log(ELogType::Warning, "Reported finished event on an inactive trigger %s", pEvent->m_pTrigger->m_name.c_str());
#else
			g_logger.Log(ELogType::Warning, "Reported finished event on an inactive trigger %u", pEvent->m_pTrigger->GetId());
#endif  // INCLUDE_AUDIO_PRODUCTION_CODE
		}
		else
		{
			g_logger.Log(ELogType::Warning, "Reported finished event on a trigger that does not exist anymore");
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::GetStartedStandaloneFileRequestData(CATLStandaloneFile* const pStandaloneFile, CAudioRequest& request)
{
	ObjectStandaloneFileMap::const_iterator const iter(m_activeStandaloneFiles.find(pStandaloneFile));

	if (iter != m_activeStandaloneFiles.end())
	{
		SUserDataBase const& audioStandaloneFileData = iter->second;
		request.pOwner = audioStandaloneFileData.pOwnerOverride;
		request.pUserData = audioStandaloneFileData.pUserData;
		request.pUserDataOwner = audioStandaloneFileData.pUserDataOwner;
	}
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ReportStartedStandaloneFile(
  CATLStandaloneFile* const pStandaloneFile,
  void* const pOwner,
  void* const pUserData,
  void* const pUserDataOwner)
{
	m_activeStandaloneFiles.emplace(pStandaloneFile, SUserDataBase(pOwner, pUserData, pUserDataOwner));
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ReportFinishedStandaloneFile(CATLStandaloneFile* const pStandaloneFile)
{
	m_activeStandaloneFiles.erase(pStandaloneFile);
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ReportFinishedLoadingTriggerImpl(TriggerImplId const audioTriggerImplId, bool const bLoad)
{
	if (bLoad)
	{
		m_triggerImplStates[audioTriggerImplId].flags |= ETriggerStatus::Loaded;
	}
	else
	{
		m_triggerImplStates[audioTriggerImplId].flags &= ~ETriggerStatus::Loaded;
	}
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::HandleExecuteTrigger(
  CATLTrigger const* const pTrigger,
  void* const pOwner,
  void* const pUserData,
  void* const pUserDataOwner,
  ERequestFlags const flags)
{
	ERequestStatus result = ERequestStatus::Failure;

	// Sets ETriggerStatus::Starting on this TriggerInstance to avoid
	// reporting TriggerFinished while the events are being started.
	ReportStartingTriggerInstance(s_triggerInstanceIdCounter, pTrigger->GetId());

	for (auto const pTriggerImpl : pTrigger->m_implPtrs)
	{
		CATLEvent* const pEvent = s_pEventManager->ConstructAudioEvent();
		ERequestStatus activateResult = m_pImplData->ExecuteTrigger(pTriggerImpl->m_pImplData, pEvent->m_pImplData);

		if (activateResult == ERequestStatus::Success || activateResult == ERequestStatus::Pending)
		{
			pEvent->m_pAudioObject = this;
			pEvent->m_pTrigger = pTrigger;
			pEvent->m_audioTriggerImplId = pTriggerImpl->m_audioTriggerImplId;
			pEvent->m_audioTriggerInstanceId = s_triggerInstanceIdCounter;
			pEvent->SetDataScope(pTrigger->GetDataScope());

			if (activateResult == ERequestStatus::Success)
			{
				pEvent->m_state = EEventState::Playing;
			}
			else if (activateResult == ERequestStatus::Pending)
			{
				pEvent->m_state = EEventState::Loading;
			}

			ReportStartedEvent(pEvent);

			// If at least one event fires, it is a success: the trigger has been activated.
			result = ERequestStatus::Success;
		}
		else
		{
			s_pEventManager->ReleaseEvent(pEvent);
		}
	}

	// Either removes the ETriggerStatus::Starting flag on this trigger instance or removes it if no event was started.
	ReportStartedTriggerInstance(s_triggerInstanceIdCounter++, pOwner, pUserData, pUserDataOwner, flags);

#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	if (result != ERequestStatus::Success)
	{
		// No TriggerImpl generated an active event.
		g_logger.Log(ELogType::Warning, R"(Trigger "%s" failed on AudioObject "%s")", pTrigger->m_name.c_str(), m_name.c_str());
	}
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

	return result;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::HandleStopTrigger(CATLTrigger const* const pTrigger)
{
	ERequestStatus result = ERequestStatus::Failure;
	for (auto const pEvent : m_activeEvents)
	{
		if ((pEvent != nullptr) && pEvent->IsPlaying() && (pEvent->m_pTrigger == pTrigger))
		{
			result = pEvent->Stop();
		}
	}

	return result;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::HandleSetSwitchState(CATLSwitch const* const pSwitch, CATLSwitchState const* const pState)
{
	ERequestStatus result = ERequestStatus::Failure;

	for (auto const pSwitchStateImpl : pState->m_implPtrs)
	{
		result = pSwitchStateImpl->Set(*this);
	}

	if (result == ERequestStatus::Success)
	{
		m_switchStates[pSwitch->GetId()] = pState->GetId();
	}
#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	else
	{
		g_logger.Log(ELogType::Warning, R"(Failed to set the ATLSwitch "%s" to ATLSwitchState "%s" on AudioObject "%s")", pSwitch->m_name.c_str(), pState->m_name.c_str(), m_name.c_str());
	}
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

	return result;

}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::HandleSetParameter(CParameter const* const pParameter, float const value)
{
	ERequestStatus result = ERequestStatus::Failure;
	for (auto const pParameterImpl : pParameter->m_implPtrs)
	{
		result = pParameterImpl->Set(*this, value);
	}

	if (result == ERequestStatus::Success)
	{
		m_parameters[pParameter->GetId()] = value;
	}
#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	else
	{
		g_logger.Log(ELogType::Warning, R"(Failed to set the Audio Parameter "%s" to %f on Audio Object "%s")", pParameter->m_name.c_str(), value, m_name.c_str());
	}
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

	return result;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::HandleSetEnvironment(CATLAudioEnvironment const* const pEnvironment, float const amount)
{
	ERequestStatus result = ERequestStatus::Failure;

	for (auto const pEnvImpl : pEnvironment->m_implPtrs)
	{
		if (m_pImplData->SetEnvironment(pEnvImpl->m_pImplData, amount) == ERequestStatus::Success)
		{
			result = ERequestStatus::Success;
		}
	}
	if (result == ERequestStatus::Success)
	{
		if (amount > 0.0f)
		{
			m_environments[pEnvironment->GetId()] = amount;
		}
		else
		{
			m_environments.erase(pEnvironment->GetId());
		}
	}
#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	else
	{
		g_logger.Log(ELogType::Warning, R"(Failed to set the ATLAudioEnvironment "%s" to %f on AudioObject "%s")", pEnvironment->m_name.c_str(), amount, m_name.c_str());
	}
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

	return result;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::StopAllTriggers()
{
	return m_pImplData->StopAllTriggers();
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetObstructionOcclusion(float const obstruction, float const occlusion)
{
	m_pImplData->SetObstructionOcclusion(obstruction, occlusion);
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::LoadTriggerAsync(CATLTrigger const* const pTrigger, bool const bLoad)
{
	ERequestStatus result = ERequestStatus::Failure;
	ControlId const audioTriggerId = pTrigger->GetId();

	for (auto const pTriggerImpl : pTrigger->m_implPtrs)
	{
		ETriggerStatus triggerStatus = ETriggerStatus::None;
		ObjectTriggerImplStates::const_iterator iPlace = m_triggerImplStates.end();
		if (FindPlaceConst(m_triggerImplStates, pTriggerImpl->m_audioTriggerImplId, iPlace))
		{
			triggerStatus = iPlace->second.flags;
		}
		CATLEvent* const pEvent = s_pEventManager->ConstructAudioEvent();
		ERequestStatus prepUnprepResult = ERequestStatus::Failure;

		if (bLoad)
		{
			if (((triggerStatus& ETriggerStatus::Loaded) == 0) && ((triggerStatus& ETriggerStatus::Loading) == 0))
			{
				prepUnprepResult = pTriggerImpl->m_pImplData->LoadAsync(pEvent->m_pImplData);
			}
		}
		else
		{
			if (((triggerStatus& ETriggerStatus::Loaded) > 0) && ((triggerStatus& ETriggerStatus::Unloading) == 0))
			{
				prepUnprepResult = pTriggerImpl->m_pImplData->UnloadAsync(pEvent->m_pImplData);
			}
		}

		if (prepUnprepResult == ERequestStatus::Success)
		{
			pEvent->m_pAudioObject = this;
			pEvent->m_pTrigger = pTrigger;
			pEvent->m_audioTriggerImplId = pTriggerImpl->m_audioTriggerImplId;

			pEvent->m_state = bLoad ? EEventState::Loading : EEventState::Unloading;
		}

		if (prepUnprepResult == ERequestStatus::Success)
		{
			pEvent->SetDataScope(pTrigger->GetDataScope());
			ReportStartedEvent(pEvent);
			result = ERequestStatus::Success;// if at least one event fires, it is a success
		}
		else
		{
			s_pEventManager->ReleaseEvent(pEvent);
		}
	}

#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	if (result != ERequestStatus::Success)
	{
		// No TriggerImpl produced an active event.
		g_logger.Log(ELogType::Warning, R"(LoadTriggerAsync failed on AudioObject "%s")", m_name.c_str());
	}
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

	return result;
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::HandleResetEnvironments(AudioEnvironmentLookup const& environmentsLookup)
{
	// Needs to be a copy as SetEnvironment removes from the map that we are iterating over.
	ObjectEnvironmentMap const environments = m_environments;
	ERequestStatus result = ERequestStatus::Success;

	for (auto const& environmentPair : environments)
	{
		CATLAudioEnvironment const* const pEnvironment = stl::find_in_map(environmentsLookup, environmentPair.first, nullptr);

		if (pEnvironment != nullptr)
		{
			if (HandleSetEnvironment(pEnvironment, 0.0f) != ERequestStatus::Success)
			{
				// If setting at least one Environment fails, we consider this a failure.
				result = ERequestStatus::Failure;
			}
		}
	}

	if (result == ERequestStatus::Success)
	{
		m_environments.clear();
	}
#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	else
	{
		g_logger.Log(ELogType::Warning, R"(Failed to Reset AudioEnvironments on AudioObject "%s")", m_name.c_str());
	}
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

	return result;
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ReportFinishedTriggerInstance(ObjectTriggerStates::iterator& iter)
{
	SAudioTriggerInstanceState& audioTriggerInstanceState = iter->second;
	SAudioCallbackManagerRequestData<EAudioCallbackManagerRequestType::ReportFinishedTriggerInstance> requestData(audioTriggerInstanceState.audioTriggerId);
	CAudioRequest request(&requestData);
	request.pObject = this;
	request.pOwner = audioTriggerInstanceState.pOwnerOverride;
	request.pUserData = audioTriggerInstanceState.pUserData;
	request.pUserDataOwner = audioTriggerInstanceState.pUserDataOwner;

	if ((audioTriggerInstanceState.flags & ETriggerStatus::CallbackOnExternalThread) > 0)
	{
		request.flags = ERequestFlags::CallbackOnExternalOrCallingThread;
	}
	else if ((audioTriggerInstanceState.flags & ETriggerStatus::CallbackOnAudioThread) > 0)
	{
		request.flags = ERequestFlags::CallbackOnAudioThread;
	}

	s_pAudioSystem->PushRequest(request);

	if ((audioTriggerInstanceState.flags & ETriggerStatus::Loaded) > 0)
	{
		// if the trigger instance was manually loaded -- keep it
		audioTriggerInstanceState.flags &= ~ETriggerStatus::Playing;
	}
	else
	{
		//if the trigger instance wasn't loaded -- kill it
		m_triggerStates.erase(iter);
	}
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::PushRequest(SAudioRequestData const& requestData, SRequestUserData const& userData)
{
	CAudioRequest const request(userData.flags, this, userData.pOwner, userData.pUserData, userData.pUserDataOwner, &requestData);
	s_pAudioSystem->PushRequest(request);
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::Update(
  float const deltaTime,
  float const distance,
  Vec3 const& audioListenerPosition)
{
	m_propagationProcessor.Update(deltaTime, distance, audioListenerPosition, m_flags);

	if (m_propagationProcessor.HasNewOcclusionValues())
	{
		SATLSoundPropagationData propagationData;
		m_propagationProcessor.GetPropagationData(propagationData);
		m_pImplData->SetObstructionOcclusion(propagationData.obstruction, propagationData.occlusion);
	}

	if (m_maxRadius > 0.0f)
	{
		float occlusionFadeOut = 0.0f;
		if (distance < m_maxRadius)
		{
			float const fadeOutStart = m_maxRadius - m_occlusionFadeOutDistance;
			if (fadeOutStart < distance)
			{
				occlusionFadeOut = 1.0f - ((distance - fadeOutStart) / m_occlusionFadeOutDistance);
			}
			else
			{
				occlusionFadeOut = 1.0f;
			}
		}

		m_propagationProcessor.SetOcclusionMultiplier(occlusionFadeOut);
	}
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ProcessPhysicsRay(CAudioRayInfo* const pAudioRayInfo)
{
	m_propagationProcessor.ProcessPhysicsRay(pAudioRayInfo);
}

///////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::HandleSetTransformation(CObjectTransformation const& transformation, float const distanceToListener)
{
	ERequestStatus status = ERequestStatus::Success;
	float const threshold = distanceToListener * g_cvars.m_positionUpdateThresholdMultiplier;

	if (!m_attributes.transformation.IsEquivalent(transformation, threshold))
	{
		float const deltaTime = (g_lastMainThreadFrameStartTime - m_previousTime).GetSeconds();

		if (deltaTime > 0.0f)
		{
			m_attributes.transformation = transformation;
			m_attributes.velocity = (m_attributes.transformation.GetPosition() - m_previousAttributes.transformation.GetPosition()) / deltaTime;
			m_flags |= EObjectFlags::NeedsVelocityUpdate;
			m_previousTime = g_lastMainThreadFrameStartTime;
			m_previousAttributes = m_attributes;
		}
		else if (deltaTime < 0.0f) // delta time can get negative after loading a savegame...
		{
			m_previousTime = 0.0f;  // ...in that case we force an update to the new position
			HandleSetTransformation(transformation, 0.0f);
		}
		else
		{
			// No time has passed meaning different transformations were set during the same main frame.
			// TODO: update velocity accordingly!
			m_attributes.transformation = transformation;
			m_previousAttributes = m_attributes;
		}

		status = m_pImplData->Set3DAttributes(m_attributes);
	}

	return status;
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::HandleSetOcclusionType(EOcclusionType const calcType, Vec3 const& audioListenerPosition)
{
	CRY_ASSERT(calcType != EOcclusionType::None);
	m_propagationProcessor.SetOcclusionType(calcType, audioListenerPosition);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ReleasePendingRays()
{
	m_propagationProcessor.ReleasePendingRays();
}

//////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::HandlePlayFile(CATLStandaloneFile* const pFile, void* const pOwner, void* const pUserData, void* const pUserDataOwner)
{
	ERequestStatus status = ERequestStatus::Failure;

	ERequestStatus const tempStatus = m_pImplData->PlayFile(pFile->m_pImplData);

	if (tempStatus == ERequestStatus::Success || tempStatus == ERequestStatus::Pending)
	{
		if (tempStatus == ERequestStatus::Success)
		{
			pFile->m_state = EAudioStandaloneFileState::Playing;
		}
		else if (tempStatus == ERequestStatus::Pending)
		{
			pFile->m_state = EAudioStandaloneFileState::Loading;
		}

		pFile->m_pAudioObject = this;

#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
		pFile->m_pOwner = pOwner;
		pFile->m_pUserData = pUserData;
		pFile->m_pUserDataOwner = pUserDataOwner;
#endif //INCLUDE_AUDIO_PRODUCTION_CODE

		ReportStartedStandaloneFile(pFile, pOwner, pUserData, pUserDataOwner);

		// It's a success in both cases.
		status = ERequestStatus::Success;
	}
	else
	{
#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
		g_logger.Log(ELogType::Warning, R"(PlayFile failed with "%s" on AudioObject "%s")", pFile->m_hashedFilename.GetText().c_str(), m_name.c_str());
#endif //INCLUDE_AUDIO_PRODUCTION_CODE

		s_pStandaloneFileManager->ReleaseStandaloneFile(pFile);
	}

	return status;
}

//////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::HandleStopFile(char const* const szFile)
{
	ERequestStatus status = ERequestStatus::Failure;
	CHashedString const hashedFilename(szFile);
	auto iter = m_activeStandaloneFiles.cbegin();
	auto iterEnd = m_activeStandaloneFiles.cend();

	while (iter != iterEnd)
	{
		CATLStandaloneFile* const pStandaloneFile = iter->first;

		if (pStandaloneFile != nullptr && pStandaloneFile->m_hashedFilename == hashedFilename)
		{
#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
			if (pStandaloneFile->m_state != EAudioStandaloneFileState::Playing)
			{
				char const* szState = "unknown";

				switch (pStandaloneFile->m_state)
				{
				case EAudioStandaloneFileState::Playing:
					szState = "playing";
					break;
				case EAudioStandaloneFileState::Loading:
					szState = "loading";
					break;
				case EAudioStandaloneFileState::Stopping:
					szState = "stopping";
					break;
				default:
					break;
				}
				g_logger.Log(ELogType::Warning, R"(Request to stop a standalone audio file that is not playing! State: "%s")", szState);
			}
#endif  //INCLUDE_AUDIO_PRODUCTION_CODE

			ERequestStatus const tempStatus = m_pImplData->StopFile(pStandaloneFile->m_pImplData);

			status = ERequestStatus::Success;

			if (tempStatus != ERequestStatus::Pending)
			{
				ReportFinishedStandaloneFile(pStandaloneFile);
				s_pStandaloneFileManager->ReleaseStandaloneFile(pStandaloneFile);

				iter = m_activeStandaloneFiles.begin();
				iterEnd = m_activeStandaloneFiles.end();
				continue;
			}
			else
			{
				pStandaloneFile->m_state = EAudioStandaloneFileState::Stopping;
			}
		}

		++iter;
	}

	return status;
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::Init(char const* const szName, Impl::IObject* const pImplData, Vec3 const& audioListenerPosition, EntityId entityId)
{
#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	m_name = szName;
#endif  // INCLUDE_AUDIO_PRODUCTION_CODE

	m_entityId = entityId;
	m_pImplData = pImplData;
	m_propagationProcessor.Init(this, audioListenerPosition);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetDopplerTracking(bool const bEnable)
{
	if (bEnable)
	{
		m_previousAttributes = m_attributes;
		m_flags |= EObjectFlags::TrackDoppler;
	}
	else
	{
		m_flags &= ~EObjectFlags::TrackDoppler;
	}
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetVelocityTracking(bool const bEnable)
{
	if (bEnable)
	{
		m_previousAttributes = m_attributes;
		m_flags |= EObjectFlags::TrackVelocity;
	}
	else
	{
		m_flags &= ~EObjectFlags::TrackVelocity;
	}
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::UpdateControls(float const deltaTime, Impl::SObject3DAttributes const& listenerAttributes)
{
	if ((m_flags& EObjectFlags::TrackDoppler) > 0)
	{
		// Approaching positive, departing negative value.
		if (m_attributes.velocity.GetLengthSquared() > 0.0f || listenerAttributes.velocity.GetLengthSquared() > 0.0f)
		{
			Vec3 const relativeVelocityVec(m_attributes.velocity - listenerAttributes.velocity);
			float const relativeVelocity = -relativeVelocityVec.Dot((m_attributes.transformation.GetPosition() - listenerAttributes.transformation.GetPosition()).GetNormalized());

			SAudioObjectRequestData<EAudioObjectRequestType::SetParameter> requestData(RelativeVelocityParameterId, relativeVelocity);
			CAudioRequest request(&requestData);
			request.pObject = this;
			s_pAudioSystem->PushRequest(request);

			m_flags |= EObjectFlags::NeedsDopplerUpdate;
		}
		else if ((m_flags& EObjectFlags::NeedsDopplerUpdate) > 0)
		{
			m_attributes.velocity = ZERO;

			SAudioObjectRequestData<EAudioObjectRequestType::SetParameter> requestData(RelativeVelocityParameterId, 0.0f);
			CAudioRequest request(&requestData);
			request.pObject = this;
			s_pAudioSystem->PushRequest(request);

			m_flags &= ~EObjectFlags::NeedsDopplerUpdate;
			m_pImplData->Set3DAttributes(m_attributes);
		}
	}

	if ((m_flags& EObjectFlags::TrackVelocity) > 0)
	{
		if (m_attributes.velocity.GetLengthSquared() > 0.0f)
		{
			float const currentVelocity = m_attributes.velocity.GetLength();

			if (fabs(currentVelocity - m_previousVelocity) > g_cvars.m_velocityTrackingThreshold)
			{
				m_previousVelocity = currentVelocity;

				SAudioObjectRequestData<EAudioObjectRequestType::SetParameter> requestData(AbsoluteVelocityParameterId, currentVelocity);
				CAudioRequest request(&requestData);
				request.pObject = this;
				s_pAudioSystem->PushRequest(request);
			}
		}
		else if ((m_flags& EObjectFlags::NeedsVelocityUpdate) > 0)
		{
			m_attributes.velocity = ZERO;
			m_previousVelocity = 0.0f;

			SAudioObjectRequestData<EAudioObjectRequestType::SetParameter> requestData(AbsoluteVelocityParameterId, 0.0f);
			CAudioRequest request(&requestData);
			request.pObject = this;
			s_pAudioSystem->PushRequest(request);

			m_flags &= ~EObjectFlags::NeedsVelocityUpdate;
			m_pImplData->Set3DAttributes(m_attributes);
		}
	}

	// Exponential decay towards zero.
	if (m_attributes.velocity.GetLengthSquared() > 0.0f)
	{
		float const deltaTime2 = (g_lastMainThreadFrameStartTime - m_previousTime).GetSeconds();
		float const decay = std::max(1.0f - deltaTime2 / 0.125f, 0.0f);
		m_attributes.velocity *= decay;
		m_pImplData->Set3DAttributes(m_attributes);
	}
}

///////////////////////////////////////////////////////////////////////////
bool CATLAudioObject::CanBeReleased() const
{
	return (m_flags& EObjectFlags::InUse) == 0 &&
	       m_activeEvents.empty() &&
	       m_activeStandaloneFiles.empty() &&
	       !m_propagationProcessor.HasPendingRays() &&
	       m_numPendingSyncCallbacks == 0;
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetFlag(EObjectFlags const flag)
{
	m_flags |= flag;
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::RemoveFlag(EObjectFlags const flag)
{
	m_flags &= ~flag;
}

#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
float const CATLAudioObject::CStateDebugDrawData::s_minAlpha = 0.5f;
float const CATLAudioObject::CStateDebugDrawData::s_maxAlpha = 1.0f;
int const CATLAudioObject::CStateDebugDrawData::s_maxToMinUpdates = 100;
static char const* const s_szOcclusionTypes[] = { "None", "Ignore", "Adaptive", "Low", "Medium", "High" };

///////////////////////////////////////////////////////////////////////////
CATLAudioObject::CStateDebugDrawData::CStateDebugDrawData(SwitchStateId const audioSwitchState)
	: m_currentState(audioSwitchState)
	, m_currentAlpha(s_maxAlpha)
{
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::CStateDebugDrawData::Update(SwitchStateId const audioSwitchState)
{
	if ((audioSwitchState == m_currentState) && (m_currentAlpha > s_minAlpha))
	{
		m_currentAlpha -= (s_maxAlpha - s_minAlpha) / s_maxToMinUpdates;
	}
	else if (audioSwitchState != m_currentState)
	{
		m_currentState = audioSwitchState;
		m_currentAlpha = s_maxAlpha;
	}
}

using TriggerCountMap = std::map<ControlId const, size_t>;

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::DrawDebugInfo(
  IRenderAuxGeom& auxGeom,
  Vec3 const& listenerPosition,
  AudioTriggerLookup const& triggers,
  AudioParameterLookup const& parameters,
  AudioSwitchLookup const& switches,
  AudioPreloadRequestLookup const& preloadRequests,
  AudioEnvironmentLookup const& environments) const
{
	m_propagationProcessor.DrawObstructionRays(auxGeom, m_flags);

	if (g_cvars.m_drawAudioDebug > 0)
	{
		Vec3 const& position = m_attributes.transformation.GetPosition();
		Vec3 screenPos(ZERO);

		if (IRenderer* const pRenderer = gEnv->pRenderer)
		{
			pRenderer->ProjectToScreen(position.x, position.y, position.z, &screenPos.x, &screenPos.y, &screenPos.z);

			screenPos.x = screenPos.x * 0.01f * pRenderer->GetWidth();
			screenPos.y = screenPos.y * 0.01f * pRenderer->GetHeight();
		}
		else
		{
			screenPos.z = -1.0f;
		}

		if ((screenPos.z >= 0.0f) && (screenPos.z <= 1.0f))
		{
			float const distance = position.GetDistance(listenerPosition);

			if ((g_cvars.m_drawAudioDebug & EAudioDebugDrawFilter::DrawSpheres) > 0)
			{
				SAuxGeomRenderFlags const previousRenderFlags = auxGeom.GetRenderFlags();
				SAuxGeomRenderFlags newRenderFlags(e_Def3DPublicRenderflags | e_AlphaBlended);
				newRenderFlags.SetCullMode(e_CullModeNone);
				auxGeom.SetRenderFlags(newRenderFlags);
				float const radius = 0.15f;
				auxGeom.DrawSphere(position, radius, ColorB(255, 1, 1, 255));
				auxGeom.SetRenderFlags(previousRenderFlags);
			}
			float const fontSize = 1.3f;
			float const lineHeight = 12.0f;

			if ((g_cvars.m_drawAudioDebug & EAudioDebugDrawFilter::ShowObjectStates) > 0 && !m_switchStates.empty())
			{
				Vec3 switchPos(screenPos);

				for (auto const& switchStatePair : m_switchStates)
				{

					CATLSwitch const* const pSwitch = stl::find_in_map(switches, switchStatePair.first, nullptr);

					if (pSwitch != nullptr)
					{
						CATLSwitchState const* const pSwitchState = stl::find_in_map(pSwitch->audioSwitchStates, switchStatePair.second, nullptr);

						if (pSwitchState != nullptr)
						{
							if (!pSwitch->m_name.empty() && !pSwitchState->m_name.empty())
							{
								CStateDebugDrawData& drawData = m_stateDrawInfoMap.emplace(std::piecewise_construct, std::forward_as_tuple(pSwitch->GetId()), std::forward_as_tuple(pSwitchState->GetId())).first->second;
								drawData.Update(pSwitchState->GetId());
								float const switchTextColor[4] = { 0.8f, 0.8f, 0.8f, drawData.m_currentAlpha };

								switchPos.y -= lineHeight;
								auxGeom.Draw2dLabel(
								  switchPos.x,
								  switchPos.y,
								  fontSize,
								  switchTextColor,
								  false,
								  "%s: %s\n",
								  pSwitch->m_name.c_str(),
								  pSwitchState->m_name.c_str());
							}
						}
					}
				}
			}

			CryFixedStringT<MaxMiscStringLength> temp;

			if ((g_cvars.m_drawAudioDebug & EAudioDebugDrawFilter::ShowObjectLabel) > 0)
			{
				static float const objectTextColor[4] = { 0.90f, 0.90f, 0.90f, 1.0f };
				static float const objectGrayTextColor[4] = { 0.50f, 0.50f, 0.50f, 1.0f };

				EOcclusionType const occlusionType = m_propagationProcessor.GetOcclusionType();
				SATLSoundPropagationData propagationData;
				m_propagationProcessor.GetPropagationData(propagationData);

				CATLAudioObject* const pAudioObject = const_cast<CATLAudioObject*>(this);

				auxGeom.Draw2dLabel(
				  screenPos.x,
				  screenPos.y,
				  fontSize,
				  objectTextColor,
				  false,
				  "%s Dist:%4.1fm",
				  pAudioObject->m_name.c_str(),
				  distance);

				if (distance < g_cvars.m_occlusionMaxDistance)
				{
					if (occlusionType == EOcclusionType::Adaptive)
					{
						temp.Format(
						  "%s(%s)",
						  s_szOcclusionTypes[IntegralValue(occlusionType)],
						  s_szOcclusionTypes[IntegralValue(m_propagationProcessor.GetOcclusionTypeWhenAdaptive())]);
					}
					else
					{
						temp.Format("%s", s_szOcclusionTypes[IntegralValue(occlusionType)]);
					}
				}
				else
				{
					temp.Format("Ignore (exceeded activity range)");
				}

				auxGeom.Draw2dLabel(
				  screenPos.x,
				  screenPos.y + lineHeight,
				  fontSize,
				  (occlusionType != EOcclusionType::None && occlusionType != EOcclusionType::Ignore) ? objectTextColor : objectGrayTextColor,
				  false,
				  "Obst: %3.2f Occl: %3.2f Type: %s",
				  propagationData.obstruction,
				  propagationData.occlusion,
				  temp.c_str());
			}

			float const textColor[4] = { 0.8f, 0.8f, 0.8f, 1.0f };

			if ((g_cvars.m_drawAudioDebug & EAudioDebugDrawFilter::ShowObjectParameters) > 0 && !m_parameters.empty())
			{
				Vec3 parameterPos(screenPos);

				for (auto const& parameterPair : m_parameters)
				{
					CParameter const* const pParameter = stl::find_in_map(parameters, parameterPair.first, nullptr);

					if (pParameter != nullptr)
					{
						float const offsetOnX = (static_cast<float>(pParameter->m_name.size()) + 5.6f) * 5.4f * fontSize;
						parameterPos.y -= lineHeight;
						auxGeom.Draw2dLabel(
						  parameterPos.x - offsetOnX,
						  parameterPos.y,
						  fontSize,
						  textColor, false,
						  "%s: %2.2f\n",
						  pParameter->m_name.c_str(), parameterPair.second);
					}
				}
			}

			if ((g_cvars.m_drawAudioDebug & EAudioDebugDrawFilter::ShowObjectEnvironments) > 0 && !m_environments.empty())
			{
				Vec3 envPos(screenPos);

				for (auto const& environmentPair : m_environments)
				{
					CATLAudioEnvironment const* const pEnvironment = stl::find_in_map(environments, environmentPair.first, nullptr);

					if (pEnvironment != nullptr)
					{
						float const offsetOnX = (static_cast<float>(pEnvironment->m_name.size()) + 5.1f) * 5.4f * fontSize;

						envPos.y += lineHeight;
						auxGeom.Draw2dLabel(
						  envPos.x - offsetOnX,
						  envPos.y,
						  fontSize,
						  textColor,
						  false,
						  "%s: %.2f\n",
						  pEnvironment->m_name.c_str(),
						  environmentPair.second);
					}
				}
			}

			CryFixedStringT<MaxMiscStringLength> controls;

			if ((g_cvars.m_drawAudioDebug & EAudioDebugDrawFilter::ShowObjectTriggers) > 0 && !m_triggerStates.empty())
			{
				TriggerCountMap triggerCounts;

				for (auto const& triggerStatesPair : m_triggerStates)
				{
					++(triggerCounts[triggerStatesPair.second.audioTriggerId]);
				}

				for (auto const& triggerCountsPair : triggerCounts)
				{
					CATLTrigger const* const pTrigger = stl::find_in_map(triggers, triggerCountsPair.first, nullptr);

					if (pTrigger != nullptr)
					{
						size_t const numInstances = triggerCountsPair.second;

						if (numInstances == 1)
						{
							temp.Format("%s\n", pTrigger->m_name.c_str());
						}
						else
						{
							temp.Format("%s: %" PRISIZE_T "\n", pTrigger->m_name.c_str(), numInstances);
						}

						controls += temp;
						temp.clear();
					}
				}
			}

			if ((g_cvars.m_drawAudioDebug & EAudioDebugDrawFilter::DrawObjectStandaloneFiles) > 0 && !m_activeStandaloneFiles.empty())
			{
				std::map<CHashedString, size_t> numStandaloneFiles;
				for (auto const& standaloneFilePair : m_activeStandaloneFiles)
				{
					++(numStandaloneFiles[standaloneFilePair.first->m_hashedFilename]);
				}

				for (auto const& numInstancesPair : numStandaloneFiles)
				{
					size_t const numInstances = numInstancesPair.second;

					if (numInstances == 1)
					{
						temp.Format("%s\n", numInstancesPair.first.GetText().c_str());
					}
					else
					{
						temp.Format("%s: %" PRISIZE_T "\n", numInstancesPair.first.GetText().c_str(), numInstances);
					}

					controls += temp;
					temp.clear();
				}
			}

			if (!controls.empty())
			{
				auxGeom.Draw2dLabel(
				  screenPos.x,
				  screenPos.y + 2.0f * lineHeight,
				  fontSize, textColor,
				  false,
				  "%s",
				  controls.c_str());
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ForceImplementationRefresh(
  AudioTriggerLookup const& triggers,
  AudioParameterLookup const& parameters,
  AudioSwitchLookup const& switches,
  AudioEnvironmentLookup const& environments,
  bool const bSet3DAttributes)
{
	if (bSet3DAttributes)
	{
		m_pImplData->Set3DAttributes(m_attributes);
	}

	// Parameters
	ObjectParameterMap const audioParameters = m_parameters;

	for (auto const& parameterPair : audioParameters)
	{
		CParameter const* const pParameter = stl::find_in_map(parameters, parameterPair.first, nullptr);

		if (pParameter != nullptr)
		{
			ERequestStatus const result = HandleSetParameter(pParameter, parameterPair.second);

			if (result != ERequestStatus::Success)
			{
				g_logger.Log(ELogType::Warning, R"(Parameter "%s" failed during audio middleware switch on AudioObject "%s")", pParameter->m_name.c_str(), m_name.c_str());
			}
		}
	}

	// Switches
	ObjectStateMap const audioSwitches = m_switchStates;

	for (auto const& switchPair : audioSwitches)
	{
		CATLSwitch const* const pSwitch = stl::find_in_map(switches, switchPair.first, nullptr);

		if (pSwitch != nullptr)
		{
			CATLSwitchState const* const pState = stl::find_in_map(pSwitch->audioSwitchStates, switchPair.second, nullptr);

			if (pState != nullptr)
			{
				ERequestStatus const result = HandleSetSwitchState(pSwitch, pState);

				if (result != ERequestStatus::Success)
				{
					g_logger.Log(ELogType::Warning, R"(SwitchStateImpl "%s" : "%s" failed during audio middleware switch on AudioObject "%s")", pSwitch->m_name.c_str(), pState->m_name.c_str(), m_name.c_str());
				}
			}
		}
	}

	// Environments
	ObjectEnvironmentMap const audioEnvironments = m_environments;

	for (auto const& environmentPair : audioEnvironments)
	{
		CATLAudioEnvironment const* const pEnvironment = stl::find_in_map(environments, environmentPair.first, nullptr);

		if (pEnvironment != nullptr)
		{
			ERequestStatus const result = HandleSetEnvironment(pEnvironment, environmentPair.second);

			if (result != ERequestStatus::Success)
			{
				g_logger.Log(ELogType::Warning, R"(Environment "%s" failed during audio middleware switch on AudioObject "%s")", pEnvironment->m_name.c_str(), m_name.c_str());
			}
		}
	}

	// Last re-execute its active triggers and standalone files.
	ObjectTriggerStates& triggerStates = m_triggerStates;
	auto it = triggerStates.cbegin();
	auto end = triggerStates.cend();

	while (it != end)
	{
		auto const& triggerState = *it;
		CATLTrigger const* const pTrigger = stl::find_in_map(triggers, triggerState.second.audioTriggerId, nullptr);

		if (pTrigger != nullptr)
		{
			if (!pTrigger->m_implPtrs.empty())
			{
				for (auto const pTriggerImpl : pTrigger->m_implPtrs)
				{

					CATLEvent* const pEvent = s_pEventManager->ConstructAudioEvent();
					ERequestStatus activateResult = m_pImplData->ExecuteTrigger(pTriggerImpl->m_pImplData, pEvent->m_pImplData);

					if (activateResult == ERequestStatus::Success || activateResult == ERequestStatus::Pending)
					{
						pEvent->m_pAudioObject = this;
						pEvent->m_pTrigger = pTrigger;
						pEvent->m_audioTriggerImplId = pTriggerImpl->m_audioTriggerImplId;
						pEvent->m_audioTriggerInstanceId = triggerState.first;
						pEvent->SetDataScope(pTrigger->GetDataScope());

						if (activateResult == ERequestStatus::Success)
						{
							pEvent->m_state = EEventState::Playing;
						}
						else if (activateResult == ERequestStatus::Pending)
						{
							pEvent->m_state = EEventState::Loading;
						}
						ReportStartedEvent(pEvent);
					}
					else
					{
						g_logger.Log(ELogType::Warning, R"(TriggerImpl "%s" failed during audio middleware switch on AudioObject "%s")", pTrigger->m_name.c_str(), m_name.c_str());
						s_pEventManager->ReleaseEvent(pEvent);
					}
				}
			}
			else
			{
				// The middleware has no connections set up.
				// Stop the event in this case.
				g_logger.Log(ELogType::Warning, R"(No trigger connections found during audio middleware switch for "%s" on "%s")", pTrigger->m_name.c_str(), m_name.c_str());
			}
		}
		else
		{
			it = triggerStates.erase(it);
			end = triggerStates.cend();
			continue;
		}

		++it;
	}

	ObjectStandaloneFileMap const& activeStandaloneFiles = m_activeStandaloneFiles;

	for (auto const& standaloneFilePair : activeStandaloneFiles)
	{
		CATLStandaloneFile* const pStandaloneFile = standaloneFilePair.first;

		if (pStandaloneFile != nullptr)
		{
			CRY_ASSERT(pStandaloneFile->m_state == EAudioStandaloneFileState::Playing);

			ERequestStatus const status = HandlePlayFile(pStandaloneFile, pStandaloneFile->m_pOwner, pStandaloneFile->m_pUserData, pStandaloneFile->m_pUserDataOwner);

			if (status == ERequestStatus::Success || status == ERequestStatus::Pending)
			{
				if (status == ERequestStatus::Success)
				{
					pStandaloneFile->m_state = EAudioStandaloneFileState::Playing;
				}
				else if (status == ERequestStatus::Pending)
				{
					pStandaloneFile->m_state = EAudioStandaloneFileState::Loading;
				}
			}
			else
			{
				g_logger.Log(ELogType::Warning, R"(PlayFile failed with "%s" on AudioObject "%s")", pStandaloneFile->m_hashedFilename.GetText().c_str(), m_name.c_str());
			}
		}
		else
		{
			g_logger.Log(ELogType::Error, "Retrigger active standalone audio files failed on instance: %u and file: %u as m_audioStandaloneFileMgr.LookupID() returned nullptr!", standaloneFilePair.first, standaloneFilePair.second);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
ERequestStatus CATLAudioObject::HandleSetName(char const* const szName)
{
	m_name = szName;
	return m_pImplData->SetName(szName);
}

#endif // INCLUDE_AUDIO_PRODUCTION_CODE

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ExecuteTrigger(ControlId const triggerId, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	SAudioObjectRequestData<EAudioObjectRequestType::ExecuteTrigger> requestData(triggerId);
	PushRequest(requestData, userData);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::StopTrigger(ControlId const triggerId /* = CryAudio::InvalidControlId */, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	if (triggerId != InvalidControlId)
	{
		SAudioObjectRequestData<EAudioObjectRequestType::StopTrigger> requestData(triggerId);
		PushRequest(requestData, userData);
	}
	else
	{
		SAudioObjectRequestData<EAudioObjectRequestType::StopAllTriggers> requestData;
		PushRequest(requestData, userData);
	}
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetTransformation(CObjectTransformation const& transformation, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	SAudioObjectRequestData<EAudioObjectRequestType::SetTransformation> requestData(transformation);
	PushRequest(requestData, userData);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetParameter(ControlId const parameterId, float const value, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	SAudioObjectRequestData<EAudioObjectRequestType::SetParameter> requestData(parameterId, value);
	PushRequest(requestData, userData);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetSwitchState(ControlId const audioSwitchId, SwitchStateId const audioSwitchStateId, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	SAudioObjectRequestData<EAudioObjectRequestType::SetSwitchState> requestData(audioSwitchId, audioSwitchStateId);
	PushRequest(requestData, userData);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetEnvironment(EnvironmentId const audioEnvironmentId, float const amount, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	SAudioObjectRequestData<EAudioObjectRequestType::SetEnvironment> requestData(audioEnvironmentId, amount);
	PushRequest(requestData, userData);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetCurrentEnvironments(EntityId const entityToIgnore, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	SAudioObjectRequestData<EAudioObjectRequestType::SetCurrentEnvironments> requestData(entityToIgnore, m_attributes.transformation.GetPosition());
	PushRequest(requestData, userData);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::ResetEnvironments(SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	SAudioObjectRequestData<EAudioObjectRequestType::ResetEnvironments> requestData;
	PushRequest(requestData, userData);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetOcclusionType(EOcclusionType const occlusionType, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	if (occlusionType < EOcclusionType::Count)
	{
		SAudioObjectRequestData<EAudioObjectRequestType::SetSwitchState> requestData(OcclusionTypeSwitchId, OcclusionTypeStateIds[IntegralValue(occlusionType)]);
		PushRequest(requestData, userData);
	}
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::PlayFile(SPlayFileInfo const& playFileInfo, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	SAudioObjectRequestData<EAudioObjectRequestType::PlayFile> requestData(playFileInfo.szFile, playFileInfo.usedTriggerForPlayback, playFileInfo.bLocalized);
	PushRequest(requestData, userData);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::StopFile(char const* const szFile, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	SAudioObjectRequestData<EAudioObjectRequestType::StopFile> requestData(szFile);
	PushRequest(requestData, userData);
}

//////////////////////////////////////////////////////////////////////////
void CATLAudioObject::SetName(char const* const szName, SRequestUserData const& userData /* = SAudioRequestUserData::GetEmptyObject() */)
{
	SAudioObjectRequestData<EAudioObjectRequestType::SetName> requestData(szName);
	PushRequest(requestData, userData);
}
} // namespace CryAudio
