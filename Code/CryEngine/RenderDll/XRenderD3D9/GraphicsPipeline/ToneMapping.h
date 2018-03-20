// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include "Common/GraphicsPipelineStage.h"
#include "Common/FullscreenPass.h"

class CToneMappingStage : public CGraphicsPipelineStage
{
public:
	void Init();
	void Execute();
	void ExecuteDebug();
	void ExecuteFixedExposure();
	void DisplayDebugInfo();

private:
	_smart_ptr<CTexture> m_pColorChartTex;
	CFullscreenPass      m_passToneMapping;
	CFullscreenPass      m_passFixedExposureToneMapping;
};
