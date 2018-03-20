// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include "FileIOHandler.h"
#include "ATLEntities.h"

namespace CryAudio
{
namespace Impl
{
namespace Wwise
{
class CImpl final : public IImpl
{
public:

	CImpl();
	CImpl(CImpl const&) = delete;
	CImpl(CImpl&&) = delete;
	CImpl& operator=(CImpl const&) = delete;
	CImpl& operator=(CImpl&&) = delete;

	// CryAudio::Impl::IImpl
	virtual void                Update(float const deltaTime) override;
	virtual ERequestStatus      Init(uint32 const objectPoolSize, uint32 const eventPoolSize) override;
	virtual ERequestStatus      OnBeforeShutDown() override;
	virtual ERequestStatus      ShutDown() override;
	virtual ERequestStatus      Release() override;
	virtual ERequestStatus      OnLoseFocus() override;
	virtual ERequestStatus      OnGetFocus() override;
	virtual ERequestStatus      MuteAll() override;
	virtual ERequestStatus      UnmuteAll() override;
	virtual ERequestStatus      StopAllSounds() override;
	virtual void                GamepadConnected(DeviceId const deviceUniqueID) override;
	virtual void                GamepadDisconnected(DeviceId const deviceUniqueID) override;
	virtual void                OnRefresh() override;
	virtual void                SetLanguage(char const* const szLanguage) override;
	virtual ERequestStatus      RegisterInMemoryFile(SFileInfo* const pFileInfo) override;
	virtual ERequestStatus      UnregisterInMemoryFile(SFileInfo* const pFileInfo) override;
	virtual ERequestStatus      ConstructFile(XmlNodeRef const pRootNode, SFileInfo* const pFileInfo) override;
	virtual void                DestructFile(IFile* const pIFile) override;
	virtual char const* const   GetFileLocation(SFileInfo* const pFileInfo) override;
	virtual ITrigger const*     ConstructTrigger(XmlNodeRef const pRootNode) override;
	virtual void                DestructTrigger(ITrigger const* const pITrigger) override;
	virtual IParameter const*   ConstructParameter(XmlNodeRef const pRootNode) override;
	virtual void                DestructParameter(IParameter const* const pIParameter) override;
	virtual ISwitchState const* ConstructSwitchState(XmlNodeRef const pRootNode) override;
	virtual void                DestructSwitchState(ISwitchState const* const pISwitchState) override;
	virtual IEnvironment const* ConstructEnvironment(XmlNodeRef const pRootNode) override;
	virtual void                DestructEnvironment(IEnvironment const* const pIEnvironment) override;
	virtual IObject*            ConstructGlobalObject() override;
	virtual IObject*            ConstructObject(char const* const szName = nullptr) override;
	virtual void                DestructObject(IObject const* const pIObject) override;
	virtual IListener*          ConstructListener(char const* const szName = nullptr) override;
	virtual void                DestructListener(IListener* const pIListener) override;
	virtual IEvent*             ConstructEvent(CATLEvent& event) override;
	virtual void                DestructEvent(IEvent const* const pIEvent) override;
	virtual IStandaloneFile*    ConstructStandaloneFile(CATLStandaloneFile& standaloneFile, char const* const szFile, bool const bLocalized, ITrigger const* pITrigger = nullptr) override;
	virtual void                DestructStandaloneFile(IStandaloneFile const* const pIStandaloneFile) override;

	// Below data is only used when INCLUDE_WWISE_IMPL_PRODUCTION_CODE is defined!
	virtual char const* const GetName() const override;
	virtual void              GetMemoryInfo(SMemoryInfo& memoryInfo) const override;
	virtual void              GetFileData(char const* const szName, SFileData& fileData) const override;
	// ~CryAudio::Impl::IImpl

private:

	static char const* const s_szWwiseEventTag;
	static char const* const s_szWwiseRtpcTag;
	static char const* const s_szWwiseSwitchTag;
	static char const* const s_szWwiseStateTag;
	static char const* const s_szWwiseRtpcSwitchTag;
	static char const* const s_szWwiseFileTag;
	static char const* const s_szWwiseAuxBusTag;
	static char const* const s_szWwiseValueTag;
	static char const* const s_szWwiseNameAttribute;
	static char const* const s_szWwiseValueAttribute;
	static char const* const s_szWwiseMutiplierAttribute;
	static char const* const s_szWwiseShiftAttribute;
	static char const* const s_szWwiseLocalisedAttribute;

	bool                ParseSwitchOrState(XmlNodeRef const pNode, AkUInt32& outStateOrSwitchGroupId, AkUInt32& outStateOrSwitchId);
	SSwitchState const* ParseWwiseRtpcSwitch(XmlNodeRef pNode);
	void                ParseRtpcImpl(XmlNodeRef const pNode, AkRtpcID& rtpcId, float& multiplier, float& shift);
	void                SignalAuxAudioThread();
	void                WaitForAuxAudioThread();

	AkGameObjectID                     m_gameObjectId;

	AkBankID                           m_initBankId;
	CFileIOHandler                     m_fileIOHandler;

	CryFixedStringT<MaxFilePathLength> m_regularSoundBankFolder;
	CryFixedStringT<MaxFilePathLength> m_localizedSoundBankFolder;

	using AudioInputDevices = std::map<DeviceId, AkUInt8>;
	AudioInputDevices m_mapInputDevices;

#if !defined(WWISE_FOR_RELEASE)
	bool m_bCommSystemInitialized;
#endif  // !WWISE_FOR_RELEASE

#if defined(INCLUDE_WWISE_IMPL_PRODUCTION_CODE)
	CryFixedStringT<MaxFilePathLength> m_name;
#endif  // INCLUDE_WWISE_IMPL_PRODUCTION_CODE

#if defined(WWISE_USE_OCULUS)
	void* m_pOculusSpatializerLibrary;
#endif  // WWISE_USE_OCULUS
};
} // namespace Wwise
} // namespace Impl
} // namespace CryAudio
