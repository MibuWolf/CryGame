// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#if _MSC_VER == 1900
// Disabling warnings related to member variables of classes that are exported with SANDBOX_API not being exported as well.
	#pragma warning(disable: 4251)
// Disabling warnings related to base classes of classes that are exported with SANDBOX_API not being exported as well.
	#pragma warning(disable: 4275)
#endif

#ifdef CryQt_EXPORTS
	#define QToolWindowManager_EXPORTS
#endif
