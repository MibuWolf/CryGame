// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include <CryCore/Project/CryModuleDefs.h>
#define eCryModule eCryM_AudioImplementation
#include <CryCore/Platform/platform.h>
#include <CryCore/StlUtils.h>
#include <CryCore/Project/ProjectDefines.h>
#include <CrySystem/ISystem.h>

#if !defined(_RELEASE)
	#define INCLUDE_PORTAUDIO_IMPL_PRODUCTION_CODE
	#define ENABLE_AUDIO_LOGGING
#endif // _RELEASE

#include <AudioLogger.h>

namespace CryAudio
{
extern CLogger g_implLogger;
} // namespace CryAudio
