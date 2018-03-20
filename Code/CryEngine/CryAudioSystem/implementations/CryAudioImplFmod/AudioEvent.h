// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include "Common.h"
#include <ATLEntityData.h>
#include <PoolObject.h>

// Fmod forward declarations.
namespace FMOD
{
class ChannelGroup;
class DSP;

namespace Studio
{
class EventInstance;
class ParameterInstance;
} // namespace Studio
} // namespace FMOD

namespace CryAudio
{
namespace Impl
{
namespace Fmod
{
class CEnvironment;
class CObjectBase;

class CEvent final : public IEvent, public CPoolObject<CEvent, stl::PSyncNone>
{
public:

	explicit CEvent(CATLEvent* pEvent)
		: m_pEvent(pEvent)
	{}

	virtual ~CEvent() override;

	CEvent(CEvent const&) = delete;
	CEvent(CEvent&&) = delete;
	CEvent&                      operator=(CEvent const&) = delete;
	CEvent&                      operator=(CEvent&&) = delete;

	bool                         PrepareForOcclusion();
	void                         SetObstructionOcclusion(float const obstruction, float const occlusion);
	CATLEvent&                   GetATLEvent() const                                       { return *m_pEvent; }
	uint32                       GetEventPathId() const                                    { return m_eventPathId; }
	void                         SetEventPathId(uint32 const eventPathId)                  { m_eventPathId = eventPathId; }
	FMOD::Studio::EventInstance* GetInstance() const                                       { return m_pInstance; }
	void                         SetInstance(FMOD::Studio::EventInstance* const pInstance) { m_pInstance = pInstance; }
	CObjectBase* const           GetObject()                                               { return m_pObject; }
	void                         SetObject(CObjectBase* const pAudioObject)                { m_pObject = pAudioObject; }
	void                         TrySetEnvironment(CEnvironment const* const pEnvironment, float const value);

	// CryAudio::Impl::IEvent
	virtual ERequestStatus Stop() override;
	// ~CryAudio::Impl::IEvent

private:

	CATLEvent*                       m_pEvent = nullptr;
	uint32                           m_eventPathId = InvalidCRC32;

	float                            m_lowpassFrequencyMax = 0.0f;
	float                            m_lowpassFrequencyMin = 0.0f;

	FMOD::Studio::EventInstance*     m_pInstance = nullptr;
	FMOD::ChannelGroup*              m_pMasterTrack = nullptr;
	FMOD::DSP*                       m_pLowpass = nullptr;
	FMOD::Studio::ParameterInstance* m_pOcclusionParameter = nullptr;
	CObjectBase*                     m_pObject = nullptr;
};
} // namespace Fmod
} // namespace Impl
} // namespace CryAudio
