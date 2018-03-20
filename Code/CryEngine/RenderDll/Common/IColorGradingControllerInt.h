// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include <CryRenderer/IColorGradingController.h>

struct IColorGradingControllerInt : public IColorGradingController
{
	virtual void RT_SetLayers(const SColorChartLayer* pLayers, uint32 numLayers) = 0;
};
