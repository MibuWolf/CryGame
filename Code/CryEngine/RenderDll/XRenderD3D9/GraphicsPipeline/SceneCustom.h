// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include "Common/GraphicsPipelineStage.h"
#include "Common/GraphicsPipelineStateSet.h"
#include "Common/SceneRenderPass.h"
#include "Common/FullscreenPass.h"

struct SGraphicsPipelineStateDescription;

class CSceneCustomStage : public CGraphicsPipelineStage
{
	enum EPerPassTexture
	{
		ePerPassTexture_PerlinNoiseMap = 25,
		ePerPassTexture_TerrainElevMap,
		ePerPassTexture_WindGrid,
		ePerPassTexture_TerrainNormMap,
		ePerPassTexture_TerrainBaseMap,
		ePerPassTexture_NormalsFitting,
		ePerPassTexture_DissolveNoise,
		ePerPassTexture_SceneLinearDepth,
		ePerPassTexture_PaletteTexelsPerMeter,

		ePerPassTexture_Count
	};
	
public:
	enum EPass
	{
		ePass_DebugViewSolid = 0,
		ePass_DebugViewWireframe,
		ePass_DebugViewDrawModes,
		ePass_SelectionIDs, // draw highlighted objects from editor
		ePass_Silhouette,
	};

public:
	CSceneCustomStage();

	virtual void Init() override;
	virtual void Prepare(CRenderView* pRenderView) override;

	void Execute();
	void ExecuteSilhouettePass();
	void ExecuteHelperPass();
	void Execute_DebugModes();
	void Execute_SelectionID();

	bool CreatePipelineStates(DevicePipelineStatesArray* pStateArray, const SGraphicsPipelineStateDescription& stateDesc, CGraphicsPipelineStateLocalCache* pStateCache);
	bool CreatePipelineState(const SGraphicsPipelineStateDescription& desc, EPass passID, CDeviceGraphicsPSOPtr& outPSO);

private:
	bool SetAndBuildPerPassResources(bool bOnInit);

private:
	CDeviceResourceSetDesc   m_perPassResources;
	CDeviceResourceSetPtr    m_pPerPassResourceSet;
	CDeviceResourceLayoutPtr m_pResourceLayout;
	CConstantBufferPtr       m_pPerPassConstantBuffer;
	
	CSceneRenderPass         m_debugViewPass;
	CSceneRenderPass         m_selectionIDPass;
	CFullscreenPass          m_highlightPass;

	CSceneRenderPass         m_silhouetteMaskPass;

	SDepthTexture            m_depthTarget;
};
