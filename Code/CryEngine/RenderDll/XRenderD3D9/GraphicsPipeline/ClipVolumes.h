// Copyright 2001-2015 Crytek GmbH. All rights reserved.

#pragma once

#include "Common/GraphicsPipelineStage.h"
#include "Common/PrimitiveRenderPass.h"
#include "Common/FullscreenPass.h"
//#include "Common/UtilityPasses.h"

#if !CRY_PLATFORM_ORBIS
	#define FEATURE_RENDER_CLIPVOLUME_GEOMETRY_SHADER
#endif

class CClipVolumesStage : public CGraphicsPipelineStage
{
public:
	static const uint32 MaxDeferredClipVolumes = 64;
	static const uint32 VisAreasOutdoorStencilOffset = 2; // Note: 2 stencil values reserved for stencil+outdoor fragments

public:
	CClipVolumesStage();
	virtual ~CClipVolumesStage();

	void Init();
	void Prepare(CRenderView* pRenderView) final;
	void Execute();

	void GetClipVolumeShaderParams(const Vec4*& pParams, uint32& paramCount)
	{
		CRY_ASSERT(m_bClipVolumesValid);
		pParams = m_clipVolumeShaderParams;
		paramCount = m_nShaderParamCount;
	}

	bool      IsOutdoorVisible() { CRY_ASSERT(m_bClipVolumesValid); return m_bOutdoorVisible; }

	CTexture* GetClipVolumeStencilVolumeTexture() const;

private:
	void PrepareVolumetricFog();
	void ExecuteVolumetricFog();

private:
	CPrimitiveRenderPass m_stencilPass;
	CPrimitiveRenderPass m_blendValuesPass;
	CFullscreenPass      m_stencilResolvePass;

#ifdef FEATURE_RENDER_CLIPVOLUME_GEOMETRY_SHADER
	CPrimitiveRenderPass m_volumetricStencilPass;
#else
	std::vector<std::unique_ptr<CPrimitiveRenderPass>> m_volumetricStencilPassArray;
	std::vector<std::unique_ptr<CFullscreenPass>>      m_resolveVolumetricStencilPassArray;
#endif
	std::vector<std::unique_ptr<CFullscreenPass>>      m_jitteredDepthPassArray;

	CRenderPrimitive m_stencilPrimitives[MaxDeferredClipVolumes * 2];
	CRenderPrimitive m_blendPrimitives[MaxDeferredClipVolumes];
#ifdef FEATURE_RENDER_CLIPVOLUME_GEOMETRY_SHADER
	CRenderPrimitive m_stencilPrimitivesVolFog[MaxDeferredClipVolumes * 2];
#endif

	CRY_ALIGN(16) Vec4 m_clipVolumeShaderParams[MaxDeferredClipVolumes];
	uint32                          m_nShaderParamCount;

	CTexture*                       m_pBlendValuesRT;
	CTexture*                       m_pDepthTarget;

	CTexture*                  m_pClipVolumeStencilVolumeTex;
#ifdef FEATURE_RENDER_CLIPVOLUME_GEOMETRY_SHADER
	CTexture*                  m_depthTargetVolFog;
#else
	std::vector<CTexture*>     m_pClipVolumeStencilVolumeTexArray;
#endif
	std::vector<ResourceViewHandle> m_depthTargetArrayVolFog;

	int32                           m_cleared;
	float                           m_nearDepth;
	float                           m_raymarchDistance;

	bool                            m_bClipVolumesValid;
	bool                            m_bBlendPassReady;
	bool                            m_bOutdoorVisible;
};
