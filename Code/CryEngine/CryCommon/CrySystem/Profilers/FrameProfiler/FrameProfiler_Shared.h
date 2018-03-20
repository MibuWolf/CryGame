// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include <CryCore/Platform/platform.h>

#define PROFILE_HISTORY_COUNT 64

enum EProfiledSubsystem
{
	PROFILE_ANY = 0,
	PROFILE_RENDERER,
	PROFILE_3DENGINE,
	PROFILE_PARTICLE,
	PROFILE_AI,
	PROFILE_ANIMATION,
	PROFILE_MOVIE,
	PROFILE_ENTITY,
	PROFILE_FONT,
	PROFILE_NETWORK,
	PROFILE_PHYSICS,
	PROFILE_SCRIPT,
	PROFILE_SCRIPT_CFUNC,
	PROFILE_AUDIO,
	PROFILE_EDITOR,
	PROFILE_SYSTEM,
	PROFILE_ACTION,
	PROFILE_GAME,
	PROFILE_INPUT,

	//////////////////////////////////////////////////////////////////////////
	// This enumes used for Network traffic profilers.
	// They record bytes transferred instead of time.
	//////////////////////////////////////////////////////////////////////////
	PROFILE_NETWORK_TRAFFIC,

	PROFILE_DEVICE,

	PROFILE_LAST_SUBSYSTEM //!< Must always be last.
};

//! Identify a profile label by its type.
enum EProfileDescription
{
	// When modifying this enum, always adjust profile_colors

	// Avoid using undefined
	UNDEFINED = 0,

	// ----------------------------
	// Three different profile types are supported - stored in two least significant bits
	FUNCTIONENTRY = 1,
	SECTION       = 2,
	REGION        = 3,

	// Waiting bit - can be combined with one of the three above
	WAITING = BIT(2),

	// Do not any new types here

	// ----------------------------

	// Optional types - without special coloring
	MARKER  = BIT(3),
	PUSHPOP = BIT(4)

	          // Add new types here
};

#define PROFILE_TYPE_CHECK_MASK 7 // 00000111 - profile type lookup is limited to 1 byte

class CFrameProfiler;
class CFrameProfilerSection;
class CCustomProfilerSection;
