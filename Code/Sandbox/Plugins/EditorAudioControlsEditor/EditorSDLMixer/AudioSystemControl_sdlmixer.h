// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include <IAudioConnection.h>
#include <IAudioSystemItem.h>

namespace ACE
{
enum ESdlMixerTypes
{
	eSdlMixerTypes_Invalid,
	eSdlMixerTypes_Event,
	eSdlMixerTypes_Folder,
};

class IAudioSystemControl_sdlmixer final : public IAudioSystemItem
{
public:
	IAudioSystemControl_sdlmixer() = default;
	IAudioSystemControl_sdlmixer(const string& name, CID id, ItemType type);
};
} // namespace ACE
