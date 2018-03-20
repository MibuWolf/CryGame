// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "StdAfx.h"
#include "UtilityPasses.h"
#include "FullscreenPass.h"
#include "DriverD3D.h"

ResourceViewHandle s_RTVDefaults[] =
{
	EDefaultResourceViews::RenderTarget, EDefaultResourceViews::RenderTarget,
	EDefaultResourceViews::RenderTarget, EDefaultResourceViews::RenderTarget,
	EDefaultResourceViews::RenderTarget, EDefaultResourceViews::RenderTarget,
	EDefaultResourceViews::RenderTarget, EDefaultResourceViews::RenderTarget,

	EDefaultResourceViews::RenderTarget, EDefaultResourceViews::RenderTarget,
	EDefaultResourceViews::RenderTarget, EDefaultResourceViews::RenderTarget,
	EDefaultResourceViews::RenderTarget, EDefaultResourceViews::RenderTarget,
	EDefaultResourceViews::RenderTarget, EDefaultResourceViews::RenderTarget,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CStretchRectPass
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CStretchRectPass::Execute(CTexture* pSrcRT, CTexture* pDestRT)
{
	//Check if the required shader is loaded
	if (CShaderMan::s_shPostEffects == nullptr)
		return;

	CD3D9Renderer* const __restrict rd = gcpRendD3D;

	if (pSrcRT == NULL || pDestRT == NULL)
		return;

	PROFILE_LABEL_SCOPE("STRETCHRECT");

	bool bResample = pSrcRT->GetWidth() != pDestRT->GetWidth() || pSrcRT->GetHeight() != pDestRT->GetHeight();
	const D3DFormat destFormat = DeviceFormats::ConvertFromTexFormat(pDestRT->GetDstFormat());
	const D3DFormat srcFormat = DeviceFormats::ConvertFromTexFormat(pSrcRT->GetDstFormat());

	if (!bResample && destFormat == srcFormat)
	{
		GetDeviceObjectFactory().GetCoreCommandList().GetCopyInterface()->Copy(pSrcRT->GetDevTexture(), pDestRT->GetDevTexture());
		return;
	}

	if (!m_pass.InputChanged(pSrcRT->GetTextureID(), pDestRT->GetTextureID()))
	{
		m_pass.Execute();
		return;
	}

	static CCryNameTSCRC techTexToTex("TextureToTexture");
	static CCryNameTSCRC techTexToTexResampled("TextureToTextureResampled");

	m_pass.SetPrimitiveFlags(bResample ? CRenderPrimitive::eFlags_ReflectShaderConstants_PS : CRenderPrimitive::eFlags_None);
	m_pass.SetRenderTarget(0, pDestRT);
	m_pass.SetTechnique(CShaderMan::s_shPostEffects, bResample ? techTexToTexResampled : techTexToTex, 0);
	m_pass.SetState(GS_NODEPTHTEST);
	m_pass.SetTextureSamplerPair(0, pSrcRT, bResample ? EDefaultSamplerStates::LinearClamp : EDefaultSamplerStates::PointClamp);

	if (bResample)
	{
		static CCryNameR param0Name("texToTexParams0");
		static CCryNameR param1Name("texToTexParams1");

		const bool bBigDownsample = false;  // TODO
		CTexture* pOffsetTex = bBigDownsample ? pDestRT : pSrcRT;

		float s1 = 0.5f / (float)pOffsetTex->GetWidth();  // 2.0 better results on lower res images resizing
		float t1 = 0.5f / (float)pOffsetTex->GetHeight();

		Vec4 params0, params1;
		if (bBigDownsample)
		{
			// Use rotated grid + middle sample (~Quincunx)
			params0 = Vec4(s1 * 0.96f, t1 * 0.25f, -s1 * 0.25f, t1 * 0.96f);
			params1 = Vec4(-s1 * 0.96f, -t1 * 0.25f, s1 * 0.25f, -t1 * 0.96f);
		}
		else
		{
			// Use box filtering (faster - can skip bilinear filtering, only 4 taps)
			params0 = Vec4(-s1, -t1, s1, -t1);
			params1 = Vec4(s1, t1, -s1, t1);
		}

		m_pass.BeginConstantUpdate();
		m_pass.SetConstant(param0Name, params0, eHWSC_Pixel);
		m_pass.SetConstant(param1Name, params1, eHWSC_Pixel);
	}

	m_pass.Execute();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CStretchRectPass
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CStretchRegionPass *CStretchRegionPass::s_pPass = nullptr;

CStretchRegionPass &CStretchRegionPass::GetPass()
{
	if (!s_pPass)
		s_pPass = new CStretchRegionPass;
	return *s_pPass;
}
void CStretchRegionPass::Shutdown()
{
	if (s_pPass)
		delete s_pPass;
	s_pPass = NULL;
}

void CStretchRegionPass::Execute(CTexture* pSrcRT, CTexture* pDestRT, const RECT *pSrcRect, const RECT *pDstRect, bool bBigDownsample)
{
	CD3D9Renderer* const __restrict rd = gcpRendD3D;

	if (pSrcRT == NULL || pDestRT == NULL)
		return;

	PROFILE_LABEL_SCOPE("STRETCHREGION");

	RECT rcS, rcD;
	if (pSrcRect)
		rcS = *pSrcRect;
	else
	{
		rcS.left = 0; rcS.right = pSrcRT->GetWidth(); rcS.top = 0; rcS.bottom = pSrcRT->GetHeight();
	}
	if (pDstRect)
		rcD = *pDstRect;
	else
	{
		rcD.left = 0; rcD.right = pDestRT->GetWidth(); rcD.top = 0; rcS.bottom = pDestRT->GetHeight();
	}
	const D3DFormat destFormat = DeviceFormats::ConvertFromTexFormat(pDestRT->GetDstFormat());
	const D3DFormat srcFormat = DeviceFormats::ConvertFromTexFormat(pSrcRT->GetDstFormat());

	bool bResample = false;
	if (pSrcRect || pDstRect || rcS.right - rcS.left != rcD.right - rcD.left || rcS.bottom - rcS.top != rcD.bottom - rcD.top || destFormat != srcFormat)
		bResample = true;

	if (!bResample && destFormat == srcFormat && !pSrcRT)
	{
		CDeviceCommandListRef commandList = GetDeviceObjectFactory().GetCoreCommandList();
		commandList.GetCopyInterface()->Copy(pSrcRT->GetDevTexture(), pDestRT->GetDevTexture());
		return;
	}

	m_pass.SetRenderTarget(0, pDestRT);

	D3DViewPort viewport;
	viewport.TopLeftX = (float)rcD.left; viewport.TopLeftY = (float)rcD.top;
	viewport.Width = (float)(rcD.right - rcD.left);
	viewport.Height = (float)(rcD.bottom - rcD.top);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	int renderState = GS_NODEPTHTEST;

	m_pass.SetViewport(viewport);

	// FIXME: I had to Reset primitive here because otherwise it doesn't recognize texture change
	m_Primitive.Reset();
	PreparePrimitive(m_Primitive, rcS, renderState, viewport, bResample, bBigDownsample, pSrcRT, pDestRT);

	m_pass.AddPrimitive(&m_Primitive);
	m_pass.Execute();
}

void CStretchRegionPass::PreparePrimitive(CRenderPrimitive& prim, const RECT& rcS, int renderState, const D3DViewPort& targetViewport, bool bResample, bool bBigDownsample, CTexture *pSrcRT, CTexture *pDestRT)
{
	static CCryNameTSCRC techTexToTex("TextureToTexture");
	static CCryNameTSCRC techTexToTexResampled("TextureToTextureResampledReg");

	static CCryNameR param0Name("texToTexParams0");
	static CCryNameR param1Name("texToTexParams1");
	static CCryNameR paramTCName("texToTexParamsTC");

	CTexture* pOffsetTex = bBigDownsample ? pDestRT : pSrcRT;

	float s1 = 0.5f / (float)pOffsetTex->GetWidth();  // 2.0 better results on lower res images resizing
	float t1 = 0.5f / (float)pOffsetTex->GetHeight();

	Vec4 params0, params1;
	if (bBigDownsample)
	{
		// Use rotated grid + middle sample (~Quincunx)
		params0 = Vec4(s1 * 0.96f, t1 * 0.25f, -s1 * 0.25f, t1 * 0.96f);
		params1 = Vec4(-s1 * 0.96f, -t1 * 0.25f, s1 * 0.25f, -t1 * 0.96f);
	}
	else
	{
		// Use box filtering (faster - can skip bilinear filtering, only 4 taps)
		params0 = Vec4(-s1, -t1, s1, -t1);
		params1 = Vec4(s1, t1, -s1, t1);
	}
	Vec4 ParamsTC;
	ParamsTC.x = (float)rcS.left / (float)pSrcRT->GetWidth();
	ParamsTC.z = (float)(rcS.right - rcS.left) / (float)pSrcRT->GetWidth();
	ParamsTC.y = (float)rcS.top / (float)pSrcRT->GetHeight();
	ParamsTC.w = (float)(rcS.bottom - rcS.top) / (float)pSrcRT->GetHeight();

	prim.SetFlags(CRenderPrimitive::eFlags_ReflectShaderConstants);
	prim.SetPrimitiveType(CRenderPrimitive::ePrim_FullscreenQuad);

	prim.SetTechnique(CShaderMan::s_shPostEffects, bResample ? techTexToTexResampled : techTexToTex, 0);
	prim.SetRenderState(renderState);
	prim.SetTexture(0, pSrcRT);
	prim.SetSampler(0, bResample ? EDefaultSamplerStates::LinearClamp : EDefaultSamplerStates::PointClamp);

	m_pass.BeginAddingPrimitives();

	auto& constantManager = prim.GetConstantManager();
	constantManager.BeginNamedConstantUpdate();

	constantManager.SetNamedConstant(param0Name, params0, eHWSC_Pixel);
	constantManager.SetNamedConstant(param1Name, params1, eHWSC_Pixel);
	constantManager.SetNamedConstant(paramTCName, ParamsTC, eHWSC_Vertex);

	constantManager.EndNamedConstantUpdate();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CSharpeningUpsamplePass
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CSharpeningUpsamplePass::Execute(CTexture* pSrcRT, CTexture* pDestRT)
{
	PROFILE_LABEL_SCOPE("UPSAMPLE_SHARP");

	if (!pSrcRT || !pDestRT)
		return;

	if (!m_pass.InputChanged(pSrcRT->GetTextureID(), pDestRT->GetTextureID()))
	{
		m_pass.Execute();
		return;
	}

	static CCryNameTSCRC techName("UpscaleImage");
	static CCryNameR param0Name("vParams");

	Vec4 params0;
	params0.x = (float)pSrcRT->GetWidth();
	params0.y = (float)pSrcRT->GetHeight();

	m_pass.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_PS);
	m_pass.SetRenderTarget(0, pDestRT);
	m_pass.SetTechnique(CShaderMan::s_shPostAA, techName, 0);
	m_pass.SetState(GS_NODEPTHTEST);
	m_pass.SetTextureSamplerPair(0, pSrcRT, EDefaultSamplerStates::LinearClamp, EDefaultResourceViews::sRGB);
	m_pass.BeginConstantUpdate();
	m_pass.SetConstant(param0Name, params0, eHWSC_Pixel);
	m_pass.Execute();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CDownsamplePass
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CDownsamplePass::Execute(CTexture* pSrcRT, CTexture* pDestRT, int nSrcW, int nSrcH, int nDstW, int nDstH, EFilterType eFilter)
{
	PROFILE_LABEL_SCOPE("DOWNSAMPLE");

	if (!pSrcRT || !pDestRT)
		return;

	// squeeze all the parameters in two integers, limit is 2^15 bit dimension and 2^4 filters
	union
	{
		struct
		{
			int sW : 15;
			int sH : 15;

			int dW : 15;
			int dH : 15;

			int fF : 4;
		};

		struct
		{
			int hi;
			int lo;
		};
	} match;

	match.sW = nSrcW;
	match.sH = nSrcH;
	match.dW = nDstW;
	match.dH = nDstH;
	match.fF = 0;

	if (!m_pass.InputChanged(pSrcRT->GetTextureID(), pDestRT->GetTextureID(), match.hi, match.lo))
	{
		m_pass.Execute();
		return;
	}

	static CCryNameTSCRC techName("TextureToTextureResampleFilter");
	static CCryNameR param0Name("texToTexParams0");
	static CCryNameR param1Name("texToTexParams1");
	static CCryNameR param2Name("texToTexParams2");

	// Currently only exact multiples supported
	Vec2 vSamples(float(nSrcW) / nDstW, float(nSrcH) / nDstH);
	const Vec2 vSampleSize(1.f / nSrcW, 1.f / nSrcH);
	const Vec2 vPixelSize(1.f / nDstW, 1.f / nDstH);
	// Adjust UV space if source rect smaller than texture
	const float fClippedRatioX = float(nSrcW) / pSrcRT->GetWidth();
	const float fClippedRatioY = float(nSrcH) / pSrcRT->GetHeight();

	// Base kernel size in pixels
	float fBaseKernelSize = 1.f;
	// How many lines of border samples to skip
	float fBorderSamplesToSkip = 0.f;

	UINT64 FlagsShader_RT = 0;
	switch (eFilter)
	{
	default:
	case FilterType_Box:
		fBaseKernelSize = 1.f;
		fBorderSamplesToSkip = 0.f;
		break;
	case FilterType_Tent:
		fBaseKernelSize = 2.f;
		fBorderSamplesToSkip = 0.f;
		FlagsShader_RT |= g_HWSR_MaskBit[HWSR_SAMPLE0];
		break;
	case FilterType_Gauss:
		// The base kernel for Gaussian filter is 3x3 pixels [-1.5 .. 1.5]
		// Samples on the borders are ignored due to small contribution
		// so the actual kernel size is N*3 - 2 where N is number of samples per pixel
		fBaseKernelSize = 3.f;
		fBorderSamplesToSkip = 1.f;
		FlagsShader_RT |= g_HWSR_MaskBit[HWSR_SAMPLE1];
		break;
	case FilterType_Lanczos:
		fBaseKernelSize = 3.f;
		fBorderSamplesToSkip = 0.f;
		FlagsShader_RT |= g_HWSR_MaskBit[HWSR_SAMPLE2];
		break;
	}

	// Kernel position step
	const Vec2 vSampleStep(1.f / vSamples.x, 1.f / vSamples.y);
	// The actual kernel radius in pixels
	const Vec2 vKernelRadius = 0.5f * Vec2(fBaseKernelSize, fBaseKernelSize) - fBorderSamplesToSkip * vSampleStep;

	// UV offset from pixel center to first (top-left) sample
	const Vec2 vFirstSampleOffset(0.5f * vSampleSize.x - vKernelRadius.x * vPixelSize.x, 0.5f * vSampleSize.y - vKernelRadius.y * vPixelSize.y);
	// Kernel position of first (top-left) sample
	const Vec2 vFirstSamplePos = -vKernelRadius + 0.5f * vSampleStep;

	const Vec4 params0(vKernelRadius.x, vKernelRadius.y, fClippedRatioX, fClippedRatioY);
	const Vec4 params1(vSampleSize.x, vSampleSize.y, vFirstSampleOffset.x, vFirstSampleOffset.y);
	const Vec4 params2(vSampleStep.x, vSampleStep.y, vFirstSamplePos.x, vFirstSamplePos.y);

	m_pass.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_PS);
	m_pass.SetRenderTarget(0, pDestRT);
	m_pass.SetTechnique(CShaderMan::s_shPostEffects, techName, FlagsShader_RT);
	m_pass.SetState(GS_NODEPTHTEST);
	m_pass.SetTextureSamplerPair(0, pSrcRT, EDefaultSamplerStates::LinearClamp/*, EDefaultResourceViews::sRGB*/);
	m_pass.BeginConstantUpdate();
	m_pass.SetConstant(param0Name, params0, eHWSC_Pixel);
	m_pass.SetConstant(param1Name, params1, eHWSC_Pixel);
	m_pass.SetConstant(param2Name, params2, eHWSC_Pixel);
	m_pass.Execute();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CStableDownsamplePass
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CStableDownsamplePass::Execute(CTexture* pSrcRT, CTexture* pDestRT, bool bKillFireflies)
{
	PROFILE_LABEL_SCOPE("DOWNSAMPLE_STABLE");

	if (!pSrcRT || !pDestRT)
		return;

	if (!m_pass.InputChanged(pSrcRT->GetTextureID(), pDestRT->GetTextureID(), (int)bKillFireflies))
	{
		m_pass.Execute();
		return;
	}

	static CCryNameTSCRC techName("DownsampleStable");

	m_pass.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_PS);
	m_pass.SetRenderTarget(0, pDestRT);
	m_pass.SetTechnique(CShaderMan::s_shPostEffects, techName, bKillFireflies ? g_HWSR_MaskBit[HWSR_SAMPLE0] : 0);
	m_pass.SetState(GS_NODEPTHTEST);
	m_pass.SetTextureSamplerPair(0, pSrcRT, EDefaultSamplerStates::LinearClamp);
	m_pass.BeginConstantUpdate();
	m_pass.Execute();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CDepthDownsamplePass
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CDepthDownsamplePass::Execute(CTexture* pSrcRT, CTexture* pDestRT, bool bLinearizeSrcDepth, bool bFromSingleChannel)
{
	PROFILE_LABEL_SCOPE("DOWNSAMPLE_DEPTH");

	if (!pSrcRT || !pDestRT)
		return;

	if (!m_pass.InputChanged(pSrcRT->GetTextureID(), pDestRT->GetTextureID(), (int)bLinearizeSrcDepth, (int)bFromSingleChannel))
	{
		m_pass.Execute();
		return;
	}

	static CCryNameTSCRC techName("DownsampleDepth");

	uint64 rtMask = 0;
	rtMask |= bLinearizeSrcDepth ? g_HWSR_MaskBit[HWSR_SAMPLE0] : 0;
	rtMask |= bFromSingleChannel ? g_HWSR_MaskBit[HWSR_SAMPLE1] : 0;

	m_pass.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_PS);
	m_pass.SetRenderTarget(0, pDestRT);
	m_pass.SetTechnique(CShaderMan::s_shPostEffects, techName, rtMask);
	m_pass.SetState(GS_NODEPTHTEST);
	m_pass.SetRequirePerViewConstantBuffer(true);
	m_pass.SetTextureSamplerPair(0, pSrcRT, EDefaultSamplerStates::PointClamp);

	static CCryNameR paramName("DownsampleDepth_Params");

	m_pass.BeginConstantUpdate();

	Vec4 params(1.0f / (float)pSrcRT->GetWidth(), 1.0f / (float)pSrcRT->GetHeight(), 0, 0);
	m_pass.SetConstant(paramName, params, eHWSC_Pixel);
	m_pass.Execute();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CGaussianBlurPass
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline float CGaussianBlurPass::GaussianDistribution1D(float x, float rho)
{
	float g = 1.0f / (rho * sqrtf(2.0f * PI));
	g *= expf(-(x * x) / (2.0f * rho * rho));
	return g;
}

void CGaussianBlurPass::ComputeParams(int texWidth, int texHeight, int numSamples, float scale, float distribution)
{
	assert(numSamples <= 16);
	const int halfNumSamples = (numSamples >> 1);

	float s1 = 1.0f / (float)texWidth;
	float t1 = 1.0f / (float)texHeight;

	float weights[16];
	float weightSum = 0.0f;

	// Compute Gaussian weights
	for (int s = 0; s < numSamples; ++s)
	{
		if (distribution != 0.0f)
			weights[s] = GaussianDistribution1D((float)(s - halfNumSamples), distribution);
		else
			weights[s] = 0.0f;
		weightSum += weights[s];
	}

	// Normalize weights
	for (int s = 0; s < numSamples; ++s)
	{
		weights[s] /= weightSum;
	}

	// Compute bilinear offsets
	for (int s = 0; s < halfNumSamples; ++s)
	{
		float off_a = weights[s * 2];
		float off_b = ((s * 2 + 1) <= numSamples - 1) ? weights[s * 2 + 1] : 0;
		float a_plus_b = (off_a + off_b);
		if (a_plus_b == 0)
			a_plus_b = 1.0f;
		float offset = off_b / a_plus_b;

		weights[s] = off_a + off_b;
		weights[s] *= scale;
		m_weights[s] = Vec4(1, 1, 1, 1) * weights[s];

		float currOffset = (float)s * 2.0f + offset - (float)halfNumSamples;
		m_paramsH[s] = Vec4(s1 * currOffset, 0, 0, 0);
		m_paramsV[s] = Vec4(0, t1 * currOffset, 0, 0);
	}
}

void CGaussianBlurPass::Execute(CTexture* pScrDestRT, CTexture* pTempRT, float scale, float distribution, bool bAlphaOnly)
{
	CD3D9Renderer* const __restrict rd = gcpRendD3D;

	if (pScrDestRT == NULL || pTempRT == NULL)
		return;

	PROFILE_LABEL_SCOPE("TEXBLUR_GAUSSIAN");

	if (!m_passH.InputChanged(pScrDestRT->GetTextureID(), pTempRT->GetTextureID()) &&
	    !m_passV.InputChanged(pScrDestRT->GetTextureID(), pTempRT->GetTextureID()) &&
	    m_scale == scale && m_distribution == distribution)
	{
		m_passH.Execute();
		m_passV.Execute();
		return;
	}

	CShader* pShader = CShaderMan::s_shPostEffects;

	static CCryNameTSCRC techDefault("GaussBlurBilinear");
	static CCryNameTSCRC techAlphaBlur("GaussAlphaBlur");
	static CCryNameR clampTCName("clampTC");
	static CCryNameR param0Name("psWeights");
	static CCryNameR param1Name("PB_psOffsets");

	Vec4 clampTC(0.0f, 1.0f, 0.0f, 1.0f);
	if (pScrDestRT->GetWidth() == rd->GetWidth() && pScrDestRT->GetHeight() == rd->GetHeight())
	{
		// Clamp manually in shader since texture clamp won't apply for smaller viewport
		clampTC = Vec4(0.0f, rd->m_RP.m_CurDownscaleFactor.x, 0.0f, rd->m_RP.m_CurDownscaleFactor.y);
	}

	const int numSamples = 16;
	if (m_scale != scale || m_distribution != distribution)
	{
		ComputeParams(pScrDestRT->GetWidth(), pScrDestRT->GetHeight(), numSamples, scale, distribution);
		m_scale = scale;
		m_distribution = distribution;
	}

	auto& techName = bAlphaOnly ? techAlphaBlur : techDefault;

	// Horizontal
	m_passH.SetRenderTarget(0, pTempRT);
	m_passH.SetTechnique(pShader, techName, 0);
	m_passH.SetState(GS_NODEPTHTEST);
	m_passH.SetTextureSamplerPair(0, pScrDestRT, EDefaultSamplerStates::LinearClamp);

	m_passH.BeginConstantUpdate();
	m_passH.SetConstantArray(param1Name, m_paramsH, numSamples / 2, eHWSC_Vertex);
	m_passH.SetConstantArray(param0Name, m_weights, numSamples / 2, eHWSC_Pixel);
	m_passH.SetConstant(clampTCName, clampTC, eHWSC_Pixel);
	m_passH.Execute();

	// Vertical
	m_passV.SetRenderTarget(0, pScrDestRT);
	m_passV.SetTechnique(pShader, techName, 0);
	m_passV.SetState(GS_NODEPTHTEST);
	m_passV.SetTextureSamplerPair(0, pTempRT, EDefaultSamplerStates::LinearClamp);

	m_passV.BeginConstantUpdate();
	m_passV.SetConstantArray(param1Name, m_paramsV, numSamples / 2, eHWSC_Vertex);
	m_passV.SetConstantArray(param0Name, m_weights, numSamples / 2, eHWSC_Pixel);
	m_passV.SetConstant(clampTCName, clampTC, eHWSC_Pixel);
	m_passV.Execute();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CMipmapGenPass
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CMipmapGenPass::Execute(CTexture* pScrDestRT, int mipCount)
{

#if CRY_RENDERER_VULKAN
	return; // TODO: add support for individual subresource states
#endif

	static CCryNameTSCRC techDownsample("TextureToTexture");
	const int numPasses = mipCount == 0 ? pScrDestRT->GetNumMips() - 1 : std::min(pScrDestRT->GetNumMips() - 1, mipCount);

	for (int i = 0; i < numPasses; ++i)
	{
		auto& curPass = m_downsamplePasses[i];

		if (curPass.InputChanged(pScrDestRT->GetID()))
		{
			auto rtv = SResourceView::RenderTargetView(DeviceFormats::ConvertFromTexFormat(pScrDestRT->GetDstFormat()), 0, -1, i + 1);
			auto srv = SResourceView::ShaderResourceView(DeviceFormats::ConvertFromTexFormat(pScrDestRT->GetDstFormat()), 0, -1, i, 1);
			curPass.SetPrimitiveFlags(CRenderPrimitive::eFlags_None);
			curPass.SetRenderTarget(0, pScrDestRT, pScrDestRT->GetDevTexture()->GetOrCreateResourceViewHandle(rtv));
			curPass.SetTechnique(CShaderMan::s_shPostEffects, techDownsample, 0);
			curPass.SetState(GS_NODEPTHTEST);
			curPass.SetTextureSamplerPair(0, pScrDestRT, EDefaultSamplerStates::LinearClamp, pScrDestRT->GetDevTexture()->GetOrCreateResourceViewHandle(srv));
		}
		curPass.BeginConstantUpdate();
		curPass.Execute();
	}

#if (CRY_RENDERER_DIRECT3D >= 120)
	// Revert state of resource to one coherent resource-state after mip-mapping
	CDeviceCommandListRef commandList = GetDeviceObjectFactory().GetCoreCommandList();
	CCryDX12Resource<ID3D11ResourceToImplement>* DX11res = reinterpret_cast<CCryDX12Resource<ID3D11ResourceToImplement>*>(pScrDestRT->GetDevTexture()->GetBaseTexture());
	NCryDX12::CResource& DX12res = DX11res->GetDX12Resource();
	NCryDX12::CCommandList* DX12cmd = commandList.GetDX12CommandList();

	DX12cmd->SetResourceState(DX12res, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
#endif
}

CClearRegionPass::CClearRegionPass()
{
	CryStackAllocWithSizeVector(SVF_P3F, 6, quadVertices, CDeviceBufferManager::AlignBufferSizeForStreaming);
	quadVertices[0].xyz = Vec3(-1,  1, 0);
	quadVertices[1].xyz = Vec3(-1, -1, 0);
	quadVertices[2].xyz = Vec3( 1,  1, 0);

	quadVertices[3].xyz = Vec3(-1, -1, 0);
	quadVertices[4].xyz = Vec3( 1, -1, 0);
	quadVertices[5].xyz = Vec3( 1,  1, 0);

	m_quadVertices = gcpRendD3D->m_DevBufMan.Create(BBT_VERTEX_BUFFER, BU_STATIC, 6 * sizeof(SVF_P3F));
	gcpRendD3D->m_DevBufMan.UpdateBuffer(m_quadVertices, quadVertices, 6 * sizeof(SVF_P3F));
}

CClearRegionPass::~CClearRegionPass()
{
	gcpRendD3D->m_DevBufMan.Destroy(m_quadVertices);
}

void CClearRegionPass::Execute(CTexture* pDepthTex, const int nFlags, const float cDepth, const uint8 cStencil, const uint numRects, const RECT* pRects)
{
#if (CRY_RENDERER_DIRECT3D >= 120)
	CDeviceCommandListRef commandList = GetDeviceObjectFactory().GetCoreCommandList();
	D3DDepthSurface* pDsv = pDepthTex->GetDevTexture()->LookupDSV(EDefaultResourceViews::DepthStencil);

	commandList.GetGraphicsInterface()->ClearSurface(pDsv, nFlags, cDepth, cStencil, numRects, pRects);
#else
	if (!numRects || (numRects == 1 && pRects->left <= 0 && pRects->top <= 0 && pRects->right >= pDepthTex->GetWidthNonVirtual() && pRects->bottom >= pDepthTex->GetHeightNonVirtual()))
	{
		// Full screen clear, no need to do custom pass
		CDeviceCommandListRef commandList = GetDeviceObjectFactory().GetCoreCommandList();
		commandList.GetGraphicsInterface()->ClearSurface(pDepthTex->GetDevTexture()->LookupDSV(EDefaultResourceViews::DepthStencil), nFlags, cDepth, cStencil);
		return;
	}

	D3DViewPort viewport;
	viewport.TopLeftX = viewport.TopLeftY = 0.0f;
	viewport.Width  = (float)pDepthTex->GetWidthNonVirtual();
	viewport.Height = (float)pDepthTex->GetHeightNonVirtual();
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	int renderState = GS_NODEPTHTEST;
	renderState |= (nFlags & CLEAR_ZBUFFER) ? GS_DEPTHWRITE : 0;
	renderState |= (nFlags & CLEAR_STENCIL) ? GS_STENCIL    : 0;

	int stencilState = STENC_FUNC(FSS_STENCFUNC_ALWAYS) | STENCOP_FAIL(FSS_STENCOP_KEEP) | STENCOP_ZFAIL(FSS_STENCOP_KEEP);
	stencilState |= (nFlags & CLEAR_STENCIL) ? STENCOP_PASS(FSS_STENCOP_REPLACE) : STENCOP_PASS(FSS_STENCOP_KEEP);

	m_clearPass.SetDepthTarget(pDepthTex);
	m_clearPass.SetViewport(viewport);
	m_clearPass.BeginAddingPrimitives();

	// allocate number of required primitives first
	for (int i = m_clearPrimitives.size(); i < numRects; ++i)
		m_clearPrimitives.push_back(CRenderPrimitive());

	for (int i = 0; i < numRects; ++i)
	{
		auto& prim = m_clearPrimitives[i];
		PreparePrimitive(prim, renderState, stencilState, Col_Black, cDepth, cStencil, pRects[i], viewport);

		m_clearPass.AddPrimitive(&prim);
	}
	
	m_clearPass.Execute();
#endif
}

void CClearRegionPass::Execute(CTexture* pTex, const ColorF& cClear, const uint numRects, const RECT* pRects)
{
#if (CRY_RENDERER_DIRECT3D >= 111)
	CDeviceCommandListRef commandList = GetDeviceObjectFactory().GetCoreCommandList();
	commandList.GetGraphicsInterface()->ClearSurface(pTex->GetDevTexture()->LookupRTV(EDefaultResourceViews::RenderTarget), cClear, numRects, pRects);
#else
	if (!numRects || (numRects == 1 && pRects->left <= 0 && pRects->top <= 0 && pRects->right >= pTex->GetWidthNonVirtual() && pRects->bottom >= pTex->GetHeightNonVirtual()))
	{
		// Full screen clear, no need to do custom pass
		CDeviceCommandListRef commandList = GetDeviceObjectFactory().GetCoreCommandList();
		commandList.GetGraphicsInterface()->ClearSurface(pTex->GetDevTexture()->LookupRTV(EDefaultResourceViews::RenderTarget), cClear);
		return;
	}

	D3DViewPort viewport;
	viewport.TopLeftX = viewport.TopLeftY = 0.0f;
	viewport.Width  = (float)pTex->GetWidth();
	viewport.Height = (float)pTex->GetHeight();
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	int renderState = GS_NODEPTHTEST;
	int stencilState = STENC_FUNC(FSS_STENCFUNC_ALWAYS) | STENCOP_FAIL(FSS_STENCOP_KEEP) | STENCOP_ZFAIL(FSS_STENCOP_KEEP);

	m_clearPass.SetRenderTarget(0, pTex);
	m_clearPass.SetViewport(viewport);
	m_clearPass.BeginAddingPrimitives();

	// allocate number of required primitives first
	for (int i = m_clearPrimitives.size(); i < numRects; ++i)
		m_clearPrimitives.push_back(CRenderPrimitive());

	for (int i = 0; i < numRects; ++i)
	{
		auto& prim = m_clearPrimitives[i];
		PreparePrimitive(prim, renderState, stencilState, cClear, 0, 0, pRects[i], viewport);

		m_clearPass.AddPrimitive(&prim);
	}

	m_clearPass.Execute();
#endif
}

void CClearRegionPass::PreparePrimitive(CRenderPrimitive& prim, int renderState, int stencilState, const ColorF& cClear, float cDepth, int stencilRef, const RECT& rect, const D3DViewPort& targetViewport)
{
	static CCryNameTSCRC techClear("Clear");

	prim.SetFlags(CRenderPrimitive::eFlags_ReflectShaderConstants);
	prim.SetTechnique(CShaderMan::s_ShaderCommon, techClear, 0);
	prim.SetRenderState(renderState);
	prim.SetStencilState(stencilState, stencilRef);
	prim.SetCustomVertexStream(m_quadVertices, EDefaultInputLayouts::P3F, sizeof(SVF_P3F));
	prim.SetDrawInfo(eptTriangleList, 0, 0, 6);
	prim.Compile(m_clearPass);

	auto& constantManager = prim.GetConstantManager();
	constantManager.BeginNamedConstantUpdate();

	float clipSpaceL = rect.left / targetViewport.Width  *  2.0f - 1.0f;
	float clipSpaceT = rect.top / targetViewport.Height * -2.0f + 1.0f;
	float clipSpaceR = rect.right / targetViewport.Width  *  2.0f - 1.0f;
	float clipSpaceB = rect.bottom / targetViewport.Height * -2.0f + 1.0f;

	Vec4 vClearRect;
	vClearRect.x = (clipSpaceR - clipSpaceL) * 0.5f;
	vClearRect.y = (clipSpaceT - clipSpaceB) * 0.5f;
	vClearRect.z = (clipSpaceR + clipSpaceL) * 0.5f;
	vClearRect.w = (clipSpaceT + clipSpaceB) * 0.5f;

	static CCryNameR paramClearRect("vClearRect");
	constantManager.SetNamedConstant(paramClearRect, vClearRect, eHWSC_Vertex);

	static CCryNameR paramClearDepth("vClearDepth");
	constantManager.SetNamedConstant(paramClearDepth, Vec4(cDepth, 0, 0, 0), eHWSC_Vertex);

	static CCryNameR paramClearColor("vClearColor");
	constantManager.SetNamedConstant(paramClearColor, cClear.toVec4(), eHWSC_Pixel);

	constantManager.EndNamedConstantUpdate();
	
}

void CAnisotropicVerticalBlurPass::Execute(CTexture* pTex, int nAmount, float fScale, float fDistribution, bool bAlphaOnly)
{
	if (!pTex)
	{
		return;
	}

	std::unique_ptr<SDynTexture> pBlurTempTex = stl::make_unique<SDynTexture>(pTex->GetWidth(), pTex->GetHeight(), pTex->GetDstFormat(), eTT_2D, FT_STATE_CLAMP | FT_USAGE_RENDERTARGET, "TempBlurAnisoVertRT");

	if (!pBlurTempTex)
	{
		return;
	}

	pBlurTempTex->Update(pTex->GetWidth(), pTex->GetHeight());

	if (!pBlurTempTex->m_pTexture)
	{
		return;
	}

	// TODO: remove after removing old graphics pipeline.
	// Get current viewport
	int iTempX, iTempY, iWidth, iHeight;
	gRenDev->GetViewport(&iTempX, &iTempY, &iWidth, &iHeight);
	gcpRendD3D->RT_SetViewport(0, 0, pTex->GetWidth(), pTex->GetHeight());

	static CCryNameTSCRC techName("AnisotropicVertical");
	static CCryNameR paramName("blurParams0");

	D3DViewPort viewport;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = static_cast<float>(pTex->GetWidth());
	viewport.Height = static_cast<float>(pTex->GetHeight());
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// setup texture offsets, for texture sampling
	float s1 = 1.0f / (float)pTex->GetWidth();
	float t1 = 1.0f / (float)pTex->GetHeight();

	Vec4 pWeightsPS;
	pWeightsPS.x = 0.25f * t1;
	pWeightsPS.y = 0.5f * t1;
	pWeightsPS.z = 0.75f * t1;
	pWeightsPS.w = 1.0f * t1;
	pWeightsPS *= -fScale;

	for (int p(1); p <= nAmount; ++p)
	{
		// Vertical
		{
			auto& pass = m_passBlurAnisotropicVertical[0];

			pass.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_PS);
			pass.SetTechnique(CShaderMan::s_shPostEffects, techName, 0);

			pass.SetRenderTarget(0, pBlurTempTex->m_pTexture);
			pass.SetViewport(viewport);

			pass.SetState(GS_NODEPTHTEST);

			pass.SetTextureSamplerPair(0, pTex, EDefaultSamplerStates::BilinearClamp);

			pass.BeginConstantUpdate();

			pass.SetConstant(paramName, pWeightsPS);

			pass.Execute();
		}

		pWeightsPS *= 2.0f;

		// Vertical
		{
			auto& pass = m_passBlurAnisotropicVertical[1];

			pass.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_PS);
			pass.SetTechnique(CShaderMan::s_shPostEffects, techName, 0);

			pass.SetRenderTarget(0, pTex);
			pass.SetViewport(viewport);

			pass.SetState(GS_NODEPTHTEST);

			pass.SetTextureSamplerPair(0, pBlurTempTex->m_pTexture, EDefaultSamplerStates::BilinearClamp);

			pass.BeginConstantUpdate();

			pass.SetConstant(paramName, pWeightsPS);

			pass.Execute();
		}
	}

	// TODO: remove after removing old graphics pipeline.
	// Restore previous viewport
	gcpRendD3D->RT_SetViewport(iTempX, iTempY, iWidth, iHeight);
}
