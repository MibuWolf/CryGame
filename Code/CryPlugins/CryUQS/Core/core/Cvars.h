// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

// *INDENT-OFF* - <hard to read code and declarations due to inconsistent indentation>

namespace UQS
{
	namespace Core
	{

		struct SCvars
		{
			static void          Register();
			static void          Unregister();

			static float         timeBudgetInSeconds;              // granted time in seconds to update all running queries in a time-sliced fashion
			static int           debugDraw;                        // 2d on-screen text drawing and also 3d in-world via debug geometry
			static int           debugDrawZTestOn;                 // z-buffer on/off of 3d in-world rendering
			static float         debugDrawLineThickness;           // thickness of all 3d lines (affects every primitive that uses lines for its basic debug drawing)
			static int           logQueryHistory;                  // if enabled, will keep a history of queries to allow drawing them at a later time
		};

	}
}
