// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include "ATLUtils.h"
#include "AudioInternalInterfaces.h"
#include "Common/SharedAudioData.h"
#include <CryAudio/IListener.h>
#include <CrySystem/IStreamEngine.h>
#include <CrySystem/TimeValue.h>
#include <PoolObject.h>
#include <CryString/HashedString.h>

namespace CryAudio
{
class CATLAudioObject;

struct SATLXMLTags
{
	static char const* const szPlatform;

	static char const* const szRootNodeTag;
	static char const* const szEditorDataTag;
	static char const* const szTriggersNodeTag;
	static char const* const szParametersNodeTag;
	static char const* const szSwitchesNodeTag;
	static char const* const szPreloadsNodeTag;
	static char const* const szEnvironmentsNodeTag;

	static char const* const szATLTriggerTag;
	static char const* const szATLSwitchTag;
	static char const* const szATLParametersTag;
	static char const* const szATLSwitchStateTag;
	static char const* const szATLEnvironmentTag;
	static char const* const szATLPlatformsTag;
	static char const* const szATLConfigGroupTag;

	static char const* const szATLTriggerRequestTag;
	static char const* const szATLSwitchRequestTag;
	static char const* const szATLValueTag;
	static char const* const szATLParametersRequestTag;
	static char const* const szATLPreloadRequestTag;
	static char const* const szATLEnvironmentRequestTag;

	static char const* const szATLVersionAttribute;
	static char const* const szATLNameAttribute;
	static char const* const szATLInternalNameAttribute;
	static char const* const szATLTypeAttribute;
	static char const* const szATLConfigGroupAttribute;
	static char const* const szATLRadiusAttribute;
	static char const* const szATLOcclusionFadeOutDistanceAttribute;

	static char const* const szATLDataLoadType;
};

namespace Impl
{
struct IObject;
struct IListener;
struct ITrigger;
struct IParameter;
struct ISwitchState;
struct IEnvironment;
struct IEvent;
struct IFile;
struct IStandaloneFile;
struct IImpl;
} // namespace Impl

enum class EObjectFlags : EnumFlagsType
{
	None                            = 0,
	TrackDoppler                    = BIT(0),
	TrackVelocity                   = BIT(1),
	NeedsDopplerUpdate              = BIT(2),
	NeedsVelocityUpdate             = BIT(3),
	InUse                           = BIT(4),
	Virtual                         = BIT(5),
	WaitingForInitialTransformation = BIT(6),
};
CRY_CREATE_ENUM_FLAG_OPERATORS(EObjectFlags);

enum class EFileFlags : EnumFlagsType
{
	None                      = 0,
	Cached                    = BIT(0),
	NotCached                 = BIT(1),
	NotFound                  = BIT(2),
	MemAllocFail              = BIT(3),
	Removable                 = BIT(4),
	Loading                   = BIT(5),
	UseCounted                = BIT(6),
	NeedsResetToManualLoading = BIT(7),
	Localized                 = BIT(8),
};
CRY_CREATE_ENUM_FLAG_OPERATORS(EFileFlags);

template<typename IDType>
class CATLEntity
{
public:

	explicit CATLEntity(IDType const id, EDataScope const dataScope)
		: m_id(id)
		, m_dataScope(dataScope)
	{}

	CATLEntity(CATLEntity const&) = delete;
	CATLEntity(CATLEntity&&) = delete;
	CATLEntity&        operator=(CATLEntity const&) = delete;
	CATLEntity&        operator=(CATLEntity&&) = delete;

	virtual IDType     GetId() const        { return m_id; }
	virtual EDataScope GetDataScope() const { return m_dataScope; }

#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	CryFixedStringT<MaxControlNameLength> m_name;
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

protected:

	~CATLEntity() = default;
	EDataScope m_dataScope;

private:

	IDType const m_id;
};

typedef CATLEntity<ControlId> ATLControl;

struct SATLSoundPropagationData
{
	float obstruction = 0.0f;
	float occlusion = 0.0f;
};

class CATLListener final : public CryAudio::IListener
{
public:

	CATLListener(CATLListener const&) = delete;
	CATLListener(CATLListener&&) = delete;
	CATLListener& operator=(CATLListener const&) = delete;
	CATLListener& operator=(CATLListener&&) = delete;

	explicit CATLListener(Impl::IListener* const pImplData)
		: m_pImplData(pImplData)
		, m_bNeedsFinalSetPosition(false)
	{}

	// CryAudio::IListener
	virtual void SetTransformation(CObjectTransformation const& transformation, SRequestUserData const& userData = SRequestUserData::GetEmptyObject()) override;
	// ~CryAudio::IListener

	void                             Update();
	ERequestStatus                   HandleSetTransformation(CObjectTransformation const& transformation);
	Impl::SObject3DAttributes const& Get3DAttributes() const { return m_attributes; }

	Impl::IListener* m_pImplData;

#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	CryFixedStringT<MaxObjectNameLength> m_name;
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

private:

	bool                      m_bNeedsFinalSetPosition;
	Impl::SObject3DAttributes m_attributes;
	Impl::SObject3DAttributes m_previousAttributes;
	CTimeValue                m_previousTime;

};

class CATLControlImpl
{
public:

	CATLControlImpl() = default;
	CATLControlImpl(CATLControlImpl const&) = delete;
	CATLControlImpl(CATLControlImpl&&) = delete;
	CATLControlImpl& operator=(CATLControlImpl const&) = delete;
	CATLControlImpl& operator=(CATLControlImpl&&) = delete;

	static void      SetImpl(Impl::IImpl* const pIImpl) { s_pIImpl = pIImpl; }

protected:

	static Impl::IImpl* s_pIImpl;
};

class CATLTriggerImpl final : public CATLControlImpl
{
public:

	explicit CATLTriggerImpl(
	  TriggerImplId const audioTriggerImplId,
	  Impl::ITrigger const* const pImplData = nullptr)
		: m_audioTriggerImplId(audioTriggerImplId)
		, m_pImplData(pImplData)
	{}

	~CATLTriggerImpl();

	TriggerImplId const         m_audioTriggerImplId;
	Impl::ITrigger const* const m_pImplData;
};

class CATLTrigger final : public ATLControl
{
public:

	typedef std::vector<CATLTriggerImpl const*> ImplPtrVec;

	explicit CATLTrigger(
	  ControlId const audioTriggerId,
	  EDataScope const dataScope,
	  ImplPtrVec const& implPtrs,
	  float const maxRadius,
	  float const occlusionFadeOutDistance)
		: ATLControl(audioTriggerId, dataScope)
		, m_implPtrs(implPtrs)
		, m_maxRadius(maxRadius)
		, m_occlusionFadeOutDistance(occlusionFadeOutDistance)
	{}

	ImplPtrVec const m_implPtrs;
	float const      m_maxRadius;
	float const      m_occlusionFadeOutDistance;
};

// Base class for a parameter implementation
class IParameterImpl : public CATLControlImpl
{
public:

	virtual ~IParameterImpl() = default;
	virtual ERequestStatus Set(CATLAudioObject& audioObject, float const value) const = 0;
};

// Class for a parameter associated with a middleware parameter
class CParameterImpl final : public IParameterImpl
{
public:

	explicit CParameterImpl(Impl::IParameter const* const pImplData = nullptr)
		: m_pImplData(pImplData)
	{}

	virtual ~CParameterImpl() override;

	virtual ERequestStatus Set(CATLAudioObject& audioObject, float const value) const override;

private:

	Impl::IParameter const* const m_pImplData = nullptr;
};

class CParameter final : public ATLControl
{
public:

	typedef std::vector<IParameterImpl const*> ImplPtrVec;

	explicit CParameter(ControlId const parameterId, EDataScope const dataScope, ImplPtrVec const& cImplPtrs)
		: ATLControl(parameterId, dataScope)
		, m_implPtrs(cImplPtrs)
	{}

	ImplPtrVec const m_implPtrs;
};

class IAudioSwitchStateImpl : public CATLControlImpl
{
public:

	virtual ~IAudioSwitchStateImpl() = default;
	virtual ERequestStatus Set(CATLAudioObject& audioObject) const = 0;
};

class CExternalAudioSwitchStateImpl final : public IAudioSwitchStateImpl
{
public:

	explicit CExternalAudioSwitchStateImpl(Impl::ISwitchState const* const pImplData)
		: m_pImplData(pImplData)
	{}

	virtual ~CExternalAudioSwitchStateImpl() override;

	// IAudioSwitchStateImpl
	virtual ERequestStatus Set(CATLAudioObject& audioObject) const override;
	// ~IAudioSwitchStateImpl

private:

	Impl::ISwitchState const* const m_pImplData;
};

class CATLSwitchState final
{
public:

	typedef std::vector<IAudioSwitchStateImpl const*> ImplPtrVec;

	explicit CATLSwitchState(
	  ControlId const audioSwitchId,
	  SwitchStateId const audioSwitchStateId,
	  ImplPtrVec const& implPtrs)
		: m_audioSwitchStateId(audioSwitchStateId)
		, m_audioSwitchId(audioSwitchId)
		, m_implPtrs(implPtrs)
	{}

	CATLSwitchState(CATLSwitchState const&) = delete;
	CATLSwitchState(CATLSwitchState&&) = delete;
	CATLSwitchState& operator=(CATLSwitchState const&) = delete;
	CATLSwitchState& operator=(CATLSwitchState&&) = delete;

	SwitchStateId    GetId() const       { return m_audioSwitchStateId; }
	SwitchStateId    GetParentId() const { return m_audioSwitchId; }

	ImplPtrVec const m_implPtrs;

#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	CryFixedStringT<MaxControlNameLength> m_name;
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

private:

	SwitchStateId const m_audioSwitchStateId;
	ControlId const     m_audioSwitchId;
};

class CATLSwitch final : public ATLControl
{
public:

	explicit CATLSwitch(ControlId const audioSwitchId, EDataScope const dataScope)
		: ATLControl(audioSwitchId, dataScope)
	{}

	typedef std::map<SwitchStateId, CATLSwitchState const*> AudioStates;
	AudioStates audioSwitchStates;
};

class CATLEnvironmentImpl final : public CATLControlImpl
{
public:

	explicit CATLEnvironmentImpl(Impl::IEnvironment const* const pImplData)
		: m_pImplData(pImplData)
	{}

	~CATLEnvironmentImpl();

	Impl::IEnvironment const* const m_pImplData;
};

class CATLAudioEnvironment final : public CATLEntity<EnvironmentId>
{
public:

	typedef std::vector<CATLEnvironmentImpl const*> ImplPtrVec;

	explicit CATLAudioEnvironment(EnvironmentId const audioEnvironmentId, EDataScope const dataScope, ImplPtrVec const& implPtrs)
		: CATLEntity<EnvironmentId>(audioEnvironmentId, dataScope)
		, m_implPtrs(implPtrs)
	{}

	ImplPtrVec const m_implPtrs;
};

class CATLStandaloneFile final : public CPoolObject<CATLStandaloneFile, stl::PSyncNone>
{
public:

	explicit CATLStandaloneFile()
	{}

	bool IsPlaying() const { return (m_state == EAudioStandaloneFileState::Playing) || (m_state == EAudioStandaloneFileState::Stopping); }

	CATLAudioObject*          m_pAudioObject = nullptr;
	Impl::IStandaloneFile*    m_pImplData = nullptr;
	EAudioStandaloneFileState m_state = EAudioStandaloneFileState::None;
	CHashedString             m_hashedFilename;

	// These variables are only needed when switching middleware
#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	bool                  m_bLocalized = true;
	void*                 m_pOwner = nullptr;
	void*                 m_pUserData = nullptr;
	void*                 m_pUserDataOwner = nullptr;
	Impl::ITrigger const* m_pITrigger = nullptr;
#endif // INCLUDE_AUDIO_PRODUCTION_CODE

};

class CATLEvent final : public CPoolObject<CATLEvent, stl::PSyncNone>
{
public:
	ERequestStatus Reset();
	ERequestStatus Stop();
	void           SetDataScope(EDataScope const dataScope) { m_dataScope = dataScope; }
	bool           IsPlaying() const                        { return m_state == EEventState::Playing || m_state == EEventState::PlayingDelayed; }

	EDataScope         m_dataScope = EDataScope::None;
	CATLAudioObject*   m_pAudioObject = nullptr;
	CATLTrigger const* m_pTrigger = nullptr;
	TriggerImplId      m_audioTriggerImplId = InvalidTriggerImplId;
	TriggerInstanceId  m_audioTriggerInstanceId = InvalidTriggerInstanceId;
	EEventState        m_state = EEventState::None;
	Impl::IEvent*      m_pImplData = nullptr;
};

class CATLAudioFileEntry final
{
public:

	explicit CATLAudioFileEntry(char const* const szPath = nullptr, Impl::IFile* const pImplData = nullptr)
		: m_path(szPath)
		, m_size(0)
		, m_useCount(0)
		, m_memoryBlockAlignment(MEMORY_ALLOCATION_ALIGNMENT)
		, m_flags(EFileFlags::NotFound)
		, m_dataScope(EDataScope::All)
		, m_streamTaskType(eStreamTaskTypeCount)
		, m_pMemoryBlock(nullptr)
		, m_pReadStream(nullptr)
		, m_pImplData(pImplData)
	{
#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
		m_timeCached.SetValue(0);
#endif // INCLUDE_AUDIO_PRODUCTION_CODE
	}

	CATLAudioFileEntry(CATLAudioFileEntry const&) = delete;
	CATLAudioFileEntry(CATLAudioFileEntry&&) = delete;
	CATLAudioFileEntry& operator=(CATLAudioFileEntry const&) = delete;
	CATLAudioFileEntry& operator=(CATLAudioFileEntry&&) = delete;

	CryFixedStringT<MaxFilePathLength> m_path;
	size_t                             m_size;
	size_t                             m_useCount;
	size_t                             m_memoryBlockAlignment;
	EFileFlags                         m_flags;
	EDataScope                         m_dataScope;
	EStreamTaskType                    m_streamTaskType;
	_smart_ptr<ICustomMemoryBlock>     m_pMemoryBlock;
	IReadStreamPtr                     m_pReadStream;
	Impl::IFile*                       m_pImplData;

#if defined(INCLUDE_AUDIO_PRODUCTION_CODE)
	CTimeValue m_timeCached;
#endif // INCLUDE_AUDIO_PRODUCTION_CODE
};

class CATLPreloadRequest final : public CATLEntity<PreloadRequestId>
{
public:
	typedef std::vector<FileEntryId> FileEntryIds;

	explicit CATLPreloadRequest(
	  PreloadRequestId const audioPreloadRequestId,
	  EDataScope const dataScope,
	  bool const bAutoLoad,
	  FileEntryIds const& fileEntryIds)
		: CATLEntity<PreloadRequestId>(audioPreloadRequestId, dataScope)
		, m_bAutoLoad(bAutoLoad)
		, m_fileEntryIds(fileEntryIds)
	{}

	bool const   m_bAutoLoad;
	FileEntryIds m_fileEntryIds;
};

//-------------------- ATLObject container typedefs --------------------------
typedef std::map<ControlId, CATLTrigger const*>              AudioTriggerLookup;
typedef std::map<ControlId, CParameter const*>               AudioParameterLookup;
typedef std::map<ControlId, CATLSwitch const*>               AudioSwitchLookup;
typedef std::map<PreloadRequestId, CATLPreloadRequest*>      AudioPreloadRequestLookup;
typedef std::map<EnvironmentId, CATLAudioEnvironment const*> AudioEnvironmentLookup;
} // namespace CryAudio
