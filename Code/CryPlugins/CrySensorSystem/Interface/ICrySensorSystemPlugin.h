// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include <CrySystem/ICryPlugin.h>

struct ISensorSystem;

struct ICrySensorSystemPlugin : public ICryPlugin
{
	CRYINTERFACE_DECLARE_GUID(ICrySensorSystemPlugin, "43678bb8-48cd-4bb8-ae11-8f6b0a347d9a"_cry_guid);

public:

	virtual ISensorSystem& GetSensorSystem() const = 0;
};
