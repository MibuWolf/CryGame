// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "stdafx.h"
#include "SoundEngine.h"
#include "SoundEngineUtil.h"
#include <CrySystem/File/CryFile.h>
#include <CryString/CryPath.h>

#include <SDL.h>
#include <SDL_mixer.h>

#define SDL_MIXER_PROJECT_PATH AUDIO_SYSTEM_DATA_ROOT CRY_NATIVE_PATH_SEPSTR "sdlmixer" CRY_NATIVE_PATH_SEPSTR

namespace CryAudio
{
namespace Impl
{
namespace SDL_mixer
{
static constexpr int s_supportedFormats = MIX_INIT_OGG | MIX_INIT_MP3;
static constexpr int s_numMixChannels = 512;
static constexpr SampleId s_invalidSampleId = 0;
static constexpr int s_sampleRate = 48000;
static constexpr int s_bufferSize = 4096;

// Samples
string g_sampleDataRootDir;
using SampleDataMap = std::unordered_map<SampleId, Mix_Chunk*>;
SampleDataMap g_sampleData;

using SampleNameMap = std::unordered_map<SampleId, string>;
SampleNameMap g_samplePaths;

using SampleIdUsageCounterMap = std::unordered_map<SampleId, int>;
SampleIdUsageCounterMap g_usageCounters;

// Channels
struct SChannelData
{
	CObject* pObject;
};

SChannelData g_channels[s_numMixChannels];

using TChannelQueue = std::queue<int>;
TChannelQueue g_freeChannels;

enum class EChannelFinishedRequestQueueId : EnumFlagsType
{
	One,
	Two,
	Count
};
CRY_CREATE_ENUM_FLAG_OPERATORS(EChannelFinishedRequestQueueId);

using ChannelFinishedRequests = std::deque<int>;
ChannelFinishedRequests g_channelFinishedRequests[IntegralValue(EChannelFinishedRequestQueueId::Count)];
CryCriticalSection g_channelFinishedCriticalSection;

// Audio Objects
using Objects = std::vector<CObject*>;
Objects g_objects;

// Listeners
CObjectTransformation g_listenerTransformation;
bool g_bListenerPosChanged;
bool g_bMuted;

SoundEngine::FnEventCallback g_fnEventFinishedCallback;
SoundEngine::FnStandaloneFileCallback g_fnStandaloneFileFinishedCallback;

void SoundEngine::RegisterEventFinishedCallback(FnEventCallback pCallbackFunction)
{
	g_fnEventFinishedCallback = pCallbackFunction;
}

void SoundEngine::RegisterStandaloneFileFinishedCallback(FnStandaloneFileCallback pCallbackFunction)
{
	g_fnStandaloneFileFinishedCallback = pCallbackFunction;
}

void EventFinishedPlaying(CATLEvent& audioEvent)
{
	if (g_fnEventFinishedCallback)
	{
		g_fnEventFinishedCallback(audioEvent);
	}
}

void StandaloneFileFinishedPlaying(CATLStandaloneFile& standaloneFile, char const* const szFileName)
{
	if (g_fnStandaloneFileFinishedCallback)
	{
		g_fnStandaloneFileFinishedCallback(standaloneFile, szFileName);
	}
}

void SoundEngine::UnloadSample(const SampleId nID)
{
	Mix_Chunk* pSample = stl::find_in_map(g_sampleData, nID, nullptr);
	if (pSample != nullptr)
	{
		Mix_FreeChunk(pSample);
		stl::member_find_and_erase(g_sampleData, nID);
	}
	else
	{
		g_implLogger.Log(ELogType::Error, "Could not find sample with id %d", nID);
	}
}

void ProcessChannelFinishedRequests(ChannelFinishedRequests& queue)
{
	if (!queue.empty())
	{
		for (const int finishedChannelId : queue)
		{
			CObject* pAudioObject = g_channels[finishedChannelId].pObject;
			if (pAudioObject)
			{
				EventInstanceList::iterator eventsEnd = pAudioObject->m_events.end();
				for (EventInstanceList::iterator eventsIt = pAudioObject->m_events.begin(); eventsIt != eventsEnd; )
				{
					CEvent* pEventInstance = *eventsIt;
					EventInstanceList::iterator eventsCurrent = eventsIt;
					++eventsIt;
					if (pEventInstance)
					{
						const ChannelList::iterator channelsEnd = pEventInstance->m_channels.end();
						for (ChannelList::iterator channelIt = pEventInstance->m_channels.begin(); channelIt != channelsEnd; ++channelIt)
						{
							if (*channelIt == finishedChannelId)
							{
								pEventInstance->m_channels.erase(channelIt);
								if (pEventInstance->m_channels.empty())
								{
									eventsIt = pAudioObject->m_events.erase(eventsCurrent);
									eventsEnd = pAudioObject->m_events.end();
									EventFinishedPlaying(pEventInstance->m_event);
								}
								break;
							}
						}
					}
				}
				StandAloneFileInstanceList::iterator standaloneFilesEnd = pAudioObject->m_standaloneFiles.end();
				for (StandAloneFileInstanceList::iterator standaloneFilesIt = pAudioObject->m_standaloneFiles.begin(); standaloneFilesIt != standaloneFilesEnd; )
				{
					CStandaloneFile* pStandaloneFileInstance = *standaloneFilesIt;
					StandAloneFileInstanceList::iterator standaloneFilesCurrent = standaloneFilesIt;
					++standaloneFilesIt;
					if (pStandaloneFileInstance)
					{
						const ChannelList::iterator channelsEnd = pStandaloneFileInstance->m_channels.end();
						for (ChannelList::iterator channelIt = pStandaloneFileInstance->m_channels.begin(); channelIt != channelsEnd; ++channelIt)
						{
							if (*channelIt == finishedChannelId)
							{
								pStandaloneFileInstance->m_channels.erase(channelIt);
								if (pStandaloneFileInstance->m_channels.empty())
								{
									standaloneFilesIt = pAudioObject->m_standaloneFiles.erase(standaloneFilesCurrent);
									standaloneFilesEnd = pAudioObject->m_standaloneFiles.end();
									StandaloneFileFinishedPlaying(pStandaloneFileInstance->m_atlFile, pStandaloneFileInstance->m_name.c_str());

									SampleIdUsageCounterMap::iterator it = g_usageCounters.find(pStandaloneFileInstance->m_sampleId);
									CRY_ASSERT(it != g_usageCounters.end() && it->second > 0);
									if (--it->second == 0)
									{
										SoundEngine::UnloadSample(pStandaloneFileInstance->m_sampleId);
									}
								}
								break;
							}
						}
					}
				}
				g_channels[finishedChannelId].pObject = nullptr;
			}
			g_freeChannels.push(finishedChannelId);
		}
		queue.clear();
	}
}

void ChannelFinishedPlaying(int nChannel)
{
	if (nChannel >= 0 && nChannel < s_numMixChannels)
	{
		CryAutoLock<CryCriticalSection> autoLock(g_channelFinishedCriticalSection);
		g_channelFinishedRequests[IntegralValue(EChannelFinishedRequestQueueId::One)].push_back(nChannel);
	}
}

void LoadMetadata(const string& path)
{
	_finddata_t fd;
	ICryPak* pCryPak = gEnv->pCryPak;
	intptr_t handle = pCryPak->FindFirst(g_sampleDataRootDir + path + "*.*", &fd);
	if (handle != -1)
	{
		do
		{
			const string name = fd.name;
			if (name != "." && name != ".." && !name.empty())
			{
				if (fd.attrib & _A_SUBDIR)
				{
					LoadMetadata(path + name + CRY_NATIVE_PATH_SEPSTR);
				}
				else
				{
					if (name.find(".wav") != string::npos ||
					    name.find(".ogg") != string::npos ||
					    name.find(".mp3") != string::npos)
					{
						if (path.empty())
						{
							g_samplePaths[GetIDFromString(name)] = g_sampleDataRootDir + name;
						}
						else
						{
							string pathName = path + name;
							g_samplePaths[GetIDFromString(pathName)] = g_sampleDataRootDir + pathName;
						}
					}
				}
			}
		}
		while (pCryPak->FindNext(handle, &fd) >= 0);
		pCryPak->FindClose(handle);
	}
}

bool SoundEngine::Init()
{
	if (SDL_Init(SDL_INIT_AUDIO) < 0)
	{
		g_implLogger.Log(ELogType::Error, "SDL::SDL_Init() returned: %s", SDL_GetError());
		return false;
	}

	int loadedFormats = Mix_Init(s_supportedFormats);
	if ((loadedFormats & s_supportedFormats) != s_supportedFormats)
	{
		g_implLogger.Log(ELogType::Error, "SDLMixer::Mix_Init() failed to init support for format flags %d with error \"%s\"", s_supportedFormats, Mix_GetError());
		return false;
	}

	if (Mix_OpenAudio(s_sampleRate, MIX_DEFAULT_FORMAT, 2, s_bufferSize) < 0)
	{
		g_implLogger.Log(ELogType::Error, "SDLMixer::Mix_OpenAudio() failed to init the SDL Mixer API with error \"%s\"", Mix_GetError());
		return false;
	}

	Mix_AllocateChannels(s_numMixChannels);

	g_bMuted = false;
	Mix_Volume(-1, SDL_MIX_MAXVOLUME);
	for (int i = 0; i < s_numMixChannels; ++i)
	{
		g_freeChannels.push(i);
	}

	Mix_ChannelFinished(ChannelFinishedPlaying);

	g_sampleDataRootDir = PathUtil::GetPathWithoutFilename(PathUtil::GetGameFolder() + CRY_NATIVE_PATH_SEPSTR SDL_MIXER_PROJECT_PATH);
	LoadMetadata("");
	g_bListenerPosChanged = false;

	// need to reinit as the global variable might have been initialized with wrong values
	g_listenerTransformation = CObjectTransformation();

	g_objects.reserve(128);

	return true;
}

void FreeAllSampleData()
{
	Mix_HaltChannel(-1);
	SampleDataMap::const_iterator it = g_sampleData.begin();
	SampleDataMap::const_iterator end = g_sampleData.end();
	for (; it != end; ++it)
	{
		Mix_FreeChunk(it->second);
	}
	g_sampleData.clear();
}

void SoundEngine::Release()
{
	FreeAllSampleData();
	g_objects.clear();
	g_objects.shrink_to_fit();
	Mix_Quit();
	Mix_CloseAudio();
	SDL_Quit();
}

void SoundEngine::Refresh()
{
	FreeAllSampleData();
	LoadMetadata(PathUtil::GetGameFolder() + CRY_NATIVE_PATH_SEPSTR SDL_MIXER_PROJECT_PATH);
}

const SampleId SoundEngine::LoadSampleFromMemory(void* pMemory, const size_t size, const string& samplePath, const SampleId overrideId)
{
	const SampleId id = (overrideId != 0) ? overrideId : GetIDFromString(samplePath);
	Mix_Chunk* pSample = stl::find_in_map(g_sampleData, id, nullptr);
	if (pSample != nullptr)
	{
		Mix_FreeChunk(pSample);
		g_implLogger.Log(ELogType::Warning, "Loading sample %s which had already been loaded", samplePath.c_str());
	}
	SDL_RWops* pData = SDL_RWFromMem(pMemory, size);
	if (pData)
	{
		pSample = Mix_LoadWAV_RW(pData, 0);
		if (pSample != nullptr)
		{
			g_sampleData[id] = pSample;
			g_samplePaths[id] = samplePath;
			return id;
		}
		else
		{
			g_implLogger.Log(ELogType::Error, "SDL Mixer failed to load sample. Error: \"%s\"", Mix_GetError());
		}
	}
	else
	{
		g_implLogger.Log(ELogType::Error, "SDL Mixer failed to transform the audio data. Error: \"%s\"", SDL_GetError());
	}
	return s_invalidSampleId;
}

bool LoadSampleImpl(const SampleId id, const string& samplePath)
{
	bool bSuccess = true;
	Mix_Chunk* pSample = Mix_LoadWAV(samplePath.c_str());
	if (pSample != nullptr)
	{
#if defined(INCLUDE_SDLMIXER_IMPL_PRODUCTION_CODE)
		SampleNameMap::const_iterator it = g_samplePaths.find(id);
		if (it != g_samplePaths.end() && it->second != samplePath)
		{
			g_implLogger.Log(ELogType::Error, "Loaded a Sample with the already existing ID %u, but from a different path source path '%s' <-> '%s'.", static_cast<uint>(id), it->second.c_str(), samplePath.c_str());
		}
		if (stl::find_in_map(g_sampleData, id, nullptr) != nullptr)
		{
			g_implLogger.Log(ELogType::Error, "Loading sample '%s' which had already been loaded", samplePath.c_str());
		}
#endif
		g_sampleData[id] = pSample;
		g_samplePaths[id] = samplePath;
	}
	else
	{
		// Sample could be inside a pak file so we need to open it manually and load it from the raw file
		const size_t fileSize = gEnv->pCryPak->FGetSize(samplePath);
		FILE* const pFile = gEnv->pCryPak->FOpen(samplePath, "rbx", ICryPak::FOPEN_HINT_DIRECT_OPERATION);
		if (pFile && fileSize > 0)
		{
			void* const pData = CryModuleMalloc(fileSize);
			gEnv->pCryPak->FReadRawAll(pData, fileSize, pFile);
			const SampleId newId = SoundEngine::LoadSampleFromMemory(pData, fileSize, samplePath, id);
			if (newId == s_invalidSampleId)
			{
				g_implLogger.Log(ELogType::Error, "SDL Mixer failed to load sample %s. Error: \"%s\"", samplePath.c_str(), Mix_GetError());
				bSuccess = false;
			}
			CryModuleFree(pData);
		}
	}
	return bSuccess;
}

const SampleId SoundEngine::LoadSample(const string& sampleFilePath, bool bOnlyMetadata)
{
	const SampleId id = GetIDFromString(sampleFilePath);
	if (stl::find_in_map(g_sampleData, id, nullptr) == nullptr)
	{
		if (bOnlyMetadata)
		{
			g_samplePaths[id] = g_sampleDataRootDir + sampleFilePath;
		}
		else if (!LoadSampleImpl(id, sampleFilePath))
		{
			return s_invalidSampleId;
		}
	}
	return id;
}

void SoundEngine::Pause()
{
	Mix_Pause(-1);
	Mix_PauseMusic();
}

void SoundEngine::Resume()
{
	Mix_Resume(-1);
	Mix_ResumeMusic();
}

void SoundEngine::Stop()
{
	Mix_HaltChannel(-1);
}

void SoundEngine::Mute()
{
	Mix_Volume(-1, 0);
	g_bMuted = true;
}

void SoundEngine::UnMute()
{
	Objects::const_iterator audioObjectIt = g_objects.begin();
	const Objects::const_iterator audioObjectEnd = g_objects.end();
	for (; audioObjectIt != audioObjectEnd; ++audioObjectIt)
	{
		CObject* pAudioObject = *audioObjectIt;
		if (pAudioObject)
		{
			EventInstanceList::const_iterator eventIt = pAudioObject->m_events.begin();
			const EventInstanceList::const_iterator eventEnd = pAudioObject->m_events.end();
			for (; eventIt != eventEnd; ++eventIt)
			{
				CEvent* pEventInstance = *eventIt;
				if (pEventInstance)
				{
					ChannelList::const_iterator channelIt = pEventInstance->m_channels.begin();
					ChannelList::const_iterator channelEnd = pEventInstance->m_channels.end();
					for (; channelIt != channelEnd; ++channelIt)
					{
						Mix_Volume(*channelIt, pEventInstance->m_pTrigger->m_volume);
					}
				}
			}
		}
	}
	g_bMuted = false;
}

void SetChannelPosition(const CTrigger* pStaticData, const int channelID, const float distance, const float angle)
{
	static const uint8 sdlMaxDistance = 255;
	const float min = pStaticData->m_attenuationMinDistance;
	const float max = pStaticData->m_attenuationMaxDistance;
	if (min <= max)
	{
		uint8 nDistance = 0;
		if (max >= 0.0f && distance > min)
		{
			if (min != max)
			{
				const float finalDistance = distance - min;
				const float range = max - min;
				nDistance = static_cast<uint8>((std::min((finalDistance / range), 1.0f) * sdlMaxDistance) + 0.5f);
			}
			else
			{
				nDistance = sdlMaxDistance;
			}
		}
		//Temp code, to be reviewed during the SetChannelPosition rewrite:
		Mix_SetDistance(channelID, nDistance);

		if (pStaticData->m_bPanningEnabled)
		{
			//Temp code, to be reviewed during the SetChannelPosition rewrite:
			float const absAngle = fabs(angle);
			float const frontAngle = (angle > 0.0f ? 1.0f : -1.0f) * (absAngle > 90.0f ? 180.f - absAngle : absAngle);
			float const rightVolume = (frontAngle + 90.0f) / 180.0f;
			float const leftVolume = 1.0f - rightVolume;
			Mix_SetPanning(channelID,
			               static_cast<uint8>(255.0f * leftVolume),
			               static_cast<uint8>(255.0f * rightVolume));
		}
	}
	else
	{
		g_implLogger.Log(ELogType::Error, "The minimum attenuation distance value is higher than the maximum");
	}
}

bool SoundEngine::StopEvent(CEvent const* const pEvent)
{
	if (pEvent != nullptr)
	{
		// need to make a copy because the callback
		// registered with Mix_ChannelFinished can edit the list
		ChannelList channels = pEvent->m_channels;
		for (int channel : channels)
		{
			Mix_HaltChannel(channel);
		}
		return true;
	}
	return false;
}

bool SoundEngine::ExecuteEvent(CObject* const pObject, CTrigger const* const pTrigger, CEvent* const pEvent)
{
	bool bSuccess = false;

	if (pObject != nullptr && pTrigger != nullptr && pEvent != nullptr)
	{
		if (pTrigger->m_bStartEvent)
		{
			// start playing samples
			pEvent->m_pTrigger = pTrigger;

			Mix_Chunk* pSample = stl::find_in_map(g_sampleData, pTrigger->m_sampleId, nullptr);
			if (pSample == nullptr)
			{
				// Trying to play sample that hasn't been loaded yet, load it in place
				// NOTE: This should be avoided as it can cause lag in audio playback
				const string& samplePath = g_samplePaths[pTrigger->m_sampleId];
				if (LoadSampleImpl(pTrigger->m_sampleId, samplePath))
				{
					pSample = stl::find_in_map(g_sampleData, pTrigger->m_sampleId, nullptr);
				}
				if (pSample == nullptr)
				{
					return false;
				}
			}

			int loopCount = pTrigger->m_numLoops;
			if (loopCount > 0)
			{
				// For SDL Mixer 0 loops means play only once, 1 loop play twice, etc ...
				--loopCount;
			}

			if (!g_freeChannels.empty())
			{
				int channelID = Mix_PlayChannel(g_freeChannels.front(), pSample, loopCount);
				if (channelID >= 0)
				{
					g_freeChannels.pop();
					Mix_Volume(channelID, g_bMuted ? 0 : pTrigger->m_volume);

					// Get distance and angle from the listener to the audio object
					float distance = 0.0f;
					float angle = 0.0f;
					GetDistanceAngleToObject(g_listenerTransformation, pObject->m_transformation, distance, angle);
					SetChannelPosition(pEvent->m_pTrigger, channelID, distance, angle);

					g_channels[channelID].pObject = pObject;
					pEvent->m_channels.push_back(channelID);
				}
				else
				{
					g_implLogger.Log(ELogType::Error, "Could not play sample. Error: %s", Mix_GetError());
				}
			}
			else
			{
				g_implLogger.Log(ELogType::Error, "Ran out of free audio channels. Are you trying to play more than %d samples?", s_numMixChannels);
			}

			if (!pEvent->m_channels.empty())
			{
				// if any sample was added then add the event to the audio object
				pObject->m_events.push_back(pEvent);
				bSuccess = true;
			}
		}
		else
		{
			// stop event in audio object
			const SampleId id = pTrigger->m_sampleId;
			for (CEvent* pEvent : pObject->m_events)
			{
				if (pEvent && (id == pEvent->m_pTrigger->m_sampleId))
				{
					SoundEngine::StopEvent(pEvent);
				}
			}
		}
	}

	return bSuccess;
}

bool SoundEngine::PlayFile(CObject* const pObject, CStandaloneFile* const pStandaloneFile)
{
	SampleId idForThisFile = pStandaloneFile->m_sampleId;
	Mix_Chunk* pSample = stl::find_in_map(g_sampleData, idForThisFile, nullptr);

	if (!pSample)
	{
		if (LoadSampleImpl(idForThisFile, pStandaloneFile->m_name.c_str()))
		{
			pSample = stl::find_in_map(g_sampleData, idForThisFile, nullptr);
		}
		if (!pSample)
		{
			return false;
		}
	}

	SampleIdUsageCounterMap::iterator it = g_usageCounters.find(idForThisFile);
	if (it != g_usageCounters.end())
	{
		++it->second;
	}
	else
	{
		g_usageCounters[idForThisFile] = 1;
	}

	if (!g_freeChannels.empty())
	{
		int channelId = Mix_PlayChannel(g_freeChannels.front(), pSample, 1);
		if (channelId >= 0)
		{
			g_freeChannels.pop();
			Mix_Volume(channelId, g_bMuted ? 0 : 128);

			// Get distance and angle from the listener to the audio object
			float distance = 0.0f;
			float angle = 0.0f;
			GetDistanceAngleToObject(g_listenerTransformation, pObject->m_transformation, distance, angle);

			// Assuming a max distance of 100.0
			uint8 sldMixerDistance = static_cast<uint8>((std::min((distance / 100.0f), 1.0f) * 255) + 0.5f);

			Mix_SetDistance(channelId, sldMixerDistance);

			float const absAngle = fabs(angle);
			float const frontAngle = (angle > 0.0f ? 1.0f : -1.0f) * (absAngle > 90.0f ? 180.f - absAngle : absAngle);
			float const rightVolume = (frontAngle + 90.0f) / 180.0f;
			float const leftVolume = 1.0f - rightVolume;
			Mix_SetPanning(channelId, static_cast<uint8>(255.0f * leftVolume), static_cast<uint8>(255.0f * rightVolume));

			g_channels[channelId].pObject = pObject;
			pStandaloneFile->m_channels.push_back(channelId);
		}
		else
		{
			g_implLogger.Log(ELogType::Error, "Could not play sample. Error: %s", Mix_GetError());
		}
	}
	else
	{
		g_implLogger.Log(ELogType::Error, "Ran out of free audio channels. Are you trying to play more than %d samples?", s_numMixChannels);
	}

	if (!pStandaloneFile->m_channels.empty())
	{
		// if any sample was added then add the event to the audio object
		pObject->m_standaloneFiles.push_back(pStandaloneFile);
	}

	return true;
}

bool SoundEngine::StopFile(CObject* const pObject, CStandaloneFile* const pStandaloneFile)
{
	bool bResult = false;

	if (pObject != nullptr)
	{
		for (CStandaloneFile* const pTempStandaloneFile : pObject->m_standaloneFiles)
		{
			if (pTempStandaloneFile == pStandaloneFile)
			{
				// need to make a copy because the callback
				// registered with Mix_ChannelFinished can edit the list
				ChannelList channels = pStandaloneFile->m_channels;
				for (int channel : channels)
				{
					Mix_HaltChannel(channel);
				}
				bResult = true;
			}
		}
	}
	return bResult;
}

bool SoundEngine::SetListenerPosition(ListenerId const listenerId, CObjectTransformation const& transformation)
{
	g_listenerTransformation = transformation;
	g_bListenerPosChanged = true;
	return true;
}

bool SoundEngine::RegisterObject(CObject* const pObject)
{
	if (pObject != nullptr)
	{
		g_objects.push_back(pObject);
		return true;
	}
	return false;
}

bool SoundEngine::UnregisterObject(CObject const* const pObject)
{
	if (pObject != nullptr)
	{
		stl::find_and_erase(g_objects, pObject);
		return true;
	}
	return false;
}

bool SoundEngine::SetObjectTransformation(CObject* const pObject, CObjectTransformation const& transformation)
{
	if (pObject != nullptr)
	{
		pObject->m_transformation = transformation;
		pObject->m_bPositionChanged = true;
		return true;
	}
	return false;
}

bool SoundEngine::StopTrigger(CTrigger const* const pTrigger)
{
	bool bResult = false;

	for (auto const pObject : g_objects)
	{
		if (pObject != nullptr)
		{
			for (auto const pEvent : pObject->m_events)
			{
				if (pEvent != nullptr && pEvent->m_pTrigger == pTrigger)
				{
					SoundEngine::StopEvent(pEvent);
					bResult = true;
				}
			}
		}
	}

	return bResult;
}

CTrigger* SoundEngine::CreateTrigger()
{
	return new CTrigger;
}

void SoundEngine::Update()
{
	ProcessChannelFinishedRequests(g_channelFinishedRequests[IntegralValue(EChannelFinishedRequestQueueId::Two)]);

	{
		CryAutoLock<CryCriticalSection> lock(g_channelFinishedCriticalSection);
		g_channelFinishedRequests[IntegralValue(EChannelFinishedRequestQueueId::One)].swap(g_channelFinishedRequests[IntegralValue(EChannelFinishedRequestQueueId::Two)]);
	}

	for (auto const pObject : g_objects)
	{
		if (pObject != nullptr)
		{
			// Get distance and angle from the listener to the audio object
			float distance = 0.0f;
			float angle = 0.0f;
			GetDistanceAngleToObject(g_listenerTransformation, pObject->m_transformation, distance, angle);

			for (auto const pEvent : pObject->m_events)
			{
				if (pEvent != nullptr && pEvent->m_pTrigger != nullptr)
				{
					for (auto const channelIndex : pEvent->m_channels)
					{
						SetChannelPosition(pEvent->m_pTrigger, channelIndex, distance, angle);
					}
				}

				pObject->m_bPositionChanged = false;
			}
		}
	}

	g_bListenerPosChanged = false;
}
} // namespace SDL_mixer
} // namespace Impl
} // namespace CryAudio
