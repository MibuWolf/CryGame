// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "StdAfx.h"
#include "DepthOfField.h"
#include "DriverD3D.h"
#include "D3DPostProcess.h"

void CDepthOfFieldStage::Init()
{
}

float NGon_Rad(float theta, float n)
{
	return cosf(PI / n) / cosf(theta - (2 * PI / n) * floorf((n * theta + PI) / (2 * PI)));
}

// Shirley's concentric mapping
Vec4 CDepthOfFieldStage::ToUnitDisk(Vec4& origin, float blades, float fstop)
{
	float max_fstops = 8;
	float min_fstops = 1;
	float normalizedStops = 1.0f; //clamp_tpl((fstop - max_fstops) / (max_fstops - min_fstops), 0.0f, 1.0f);

	float phi;
	float r;
	const float a = 2 * origin.x - 1;
	const float b = 2 * origin.y - 1;
	if (abs(a) > abs(b)) // Use squares instead of absolute values
	{
		r = a;
		phi = (PI / 4.0f) * (b / (a + 1e-6f));
	}
	else
	{
		r = b;
		phi = (PI / 2.0f) - (PI / 4.0f) * (a / (b + 1e-6f));
	}

	float rr = r * powf(NGon_Rad(phi, blades), normalizedStops);
	rr = abs(rr) * (rr > 0 ? 1.0f : -1.0f);

	//normalizedStops *= -0.4f * PI;
	return Vec4(rr * cosf(phi + normalizedStops), rr * sinf(phi + normalizedStops), 0.0f, 0.0f);
}

void CDepthOfFieldStage::Execute()
{
	CD3D9Renderer* rd = gcpRendD3D;

	CDepthOfField* pDofRenderTech = (CDepthOfField*)PostEffectMgr()->GetEffect(ePFX_eDepthOfField);
	SDepthOfFieldParams dofParams = pDofRenderTech->GetParams();

	if (dofParams.vFocus.w < 0.0001f)
		return;

	PROFILE_LABEL_SCOPE("DOF");

	CShader* pShader = CShaderMan::s_shPostMotionBlur;

	Vec4 vFocus = dofParams.vFocus;
	vFocus.w *= 2;  // For backwards compatibility

	const float fFNumber = 8;
	const float fNumApertureSides = 8;

	if (dofParams.bGameMode)
	{
		dofParams.vFocus.x = 1.0f / (vFocus.z + 1e-6f);
		dofParams.vFocus.y = -vFocus.y / (vFocus.z + 1e-6f);

		dofParams.vFocus.z = -1.0f / (vFocus.w + 1e-6f);
		dofParams.vFocus.w = vFocus.x / (vFocus.w + 1e-6f);
	}
	else
	{
		dofParams.vFocus.x = 1.0f / (vFocus.y + 1e-6f);
		dofParams.vFocus.y = -vFocus.z / (vFocus.y + 1e-6f);

		dofParams.vFocus.z = 1.0f / (vFocus.x + 1e-6f);
		dofParams.vFocus.w = -vFocus.z / (vFocus.x + 1e-6f);
	}

	Vec4 vDofParams0 = dofParams.vFocus;

	const float fNearestDofScaleBoost = dofParams.bGameMode ? CRenderer::CV_r_dofMinZBlendMult : 1.0f;
	Vec4 vDofParams1 = Vec4(CRenderer::CV_r_dofMinZ + dofParams.vMinZParams.x, CRenderer::CV_r_dofMinZScale + dofParams.vMinZParams.y, fNearestDofScaleBoost, vFocus.w);

	// For better blending later
	m_passCopySceneTarget.Execute(CTexture::s_ptexHDRTarget, CTexture::s_ptexSceneTarget);

	CTexture* pTexDofLayersTmp[2] = { CTexture::s_ptexHDRTargetScaledTmp[0], CTexture::s_ptexHDRTargetScaledTempRT[0] };

	assert(pTexDofLayersTmp[0]->GetWidth() == CTexture::s_ptexHDRDofLayers[0]->GetWidth() && pTexDofLayersTmp[0]->GetHeight() == CTexture::s_ptexHDRDofLayers[0]->GetHeight());
	assert(pTexDofLayersTmp[1]->GetWidth() == CTexture::s_ptexHDRDofLayers[1]->GetWidth() && pTexDofLayersTmp[1]->GetHeight() == CTexture::s_ptexHDRDofLayers[1]->GetHeight());
	assert(pTexDofLayersTmp[0]->GetPixelFormat() == CTexture::s_ptexHDRDofLayers[0]->GetPixelFormat() && pTexDofLayersTmp[1]->GetPixelFormat() == CTexture::s_ptexHDRDofLayers[1]->GetPixelFormat());

	static CCryNameR dofFocusParam0Name("vDofParamsFocus0");
	static CCryNameR dofFocusParam1Name("vDofParamsFocus1");
	static CCryNameR dofTapsName("g_Taps");

	{
		// 1st downscale stage
		{
			PROFILE_LABEL_SCOPE("DOWNSCALE LAYERS");

			if (m_passLayerDownscale.InputChanged())
			{
				static CCryNameTSCRC techNameDownscale("DownscaleDof");
				m_passLayerDownscale.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_PS);
				m_passLayerDownscale.SetTechnique(pShader, techNameDownscale, 0);
				m_passLayerDownscale.SetRenderTarget(0, CTexture::s_ptexHDRDofLayers[0]);  // Near
				m_passLayerDownscale.SetRenderTarget(1, CTexture::s_ptexHDRDofLayers[1]);  // Far
				m_passLayerDownscale.SetRenderTarget(2, CTexture::s_ptexSceneCoC[0]);      // CoC Near/Far
				m_passLayerDownscale.SetState(GS_NODEPTHTEST);
				m_passLayerDownscale.SetTextureSamplerPair(0, CTexture::s_ptexZTarget, EDefaultSamplerStates::PointClamp);
				m_passLayerDownscale.SetTextureSamplerPair(1, CTexture::s_ptexHDRTarget, EDefaultSamplerStates::LinearClamp);
				m_passLayerDownscale.SetRequirePerViewConstantBuffer(true);
			}

			m_passLayerDownscale.BeginConstantUpdate();
			Vec4 vParams = Vec4((float)CTexture::s_ptexHDRDofLayers[0]->GetWidth(), (float)CTexture::s_ptexHDRDofLayers[0]->GetHeight(),
			                    1.0f / (float)CTexture::s_ptexHDRDofLayers[0]->GetWidth(), 1.0f / (float)CTexture::s_ptexHDRDofLayers[0]->GetHeight());
			m_passLayerDownscale.SetConstant(dofFocusParam0Name, vDofParams0, eHWSC_Pixel);
			m_passLayerDownscale.SetConstant(dofFocusParam1Name, vDofParams1, eHWSC_Pixel);
			m_passLayerDownscale.Execute();
		}

		// 2nd downscale stage (tile min CoC)
		{
			PROFILE_LABEL_SCOPE("MIN COC DOWNSCALE");
			for (uint32 i = 1; i < MIN_DOF_COC_K; i++)
			{
				if (m_passTileMinCoC[i].InputChanged())
				{
					static CCryNameTSCRC techNameTileMinCoC("TileMinCoC");
					m_passTileMinCoC[i].SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_VS);
					m_passTileMinCoC[i].SetTechnique(pShader, techNameTileMinCoC, 0);
					m_passTileMinCoC[i].SetRenderTarget(0, CTexture::s_ptexSceneCoC[i]);  // Near
					m_passTileMinCoC[i].SetState(GS_NODEPTHTEST);
					m_passTileMinCoC[i].SetTextureSamplerPair(0, CTexture::s_ptexSceneCoC[i - 1], EDefaultSamplerStates::LinearClamp);
				}

				m_passTileMinCoC[i].BeginConstantUpdate();
				const Vec4 vParams = Vec4((float)CTexture::s_ptexSceneCoC[i - 1]->GetWidth(), (float)CTexture::s_ptexSceneCoC[i - 1]->GetHeight(),
				                          1.0f / (float)CTexture::s_ptexSceneCoC[i - 1]->GetWidth(), 1.0f / (float)CTexture::s_ptexSceneCoC[i - 1]->GetHeight());
				m_passTileMinCoC[i].SetConstant(dofFocusParam1Name, vParams, eHWSC_Vertex);
				m_passTileMinCoC[i].Execute();
			}
		}
	}

	{
		// 1st gather pass
		{
			const int32 nSquareTapsSide = 7;
			const float fRecipTaps = 1.0f / ((float)nSquareTapsSide - 1.0f);

			Vec4 vTaps[nSquareTapsSide * nSquareTapsSide];
			for (int32 y = 0; y < nSquareTapsSide; ++y)
			{
				for (int32 x = 0; x < nSquareTapsSide; ++x)
				{
					Vec4 t = Vec4(x * fRecipTaps, y * fRecipTaps, 0, 0);
					vTaps[x + y * nSquareTapsSide] = ToUnitDisk(t, fNumApertureSides, fFNumber);
				}
			}

			PROFILE_LABEL_SCOPE("FAR/NEAR LAYER");

			if (m_passGather0.InputChanged())
			{
				static CCryNameTSCRC techDOF("Dof");
				m_passGather0.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_PS);
				m_passGather0.SetTechnique(pShader, techDOF, 0);
				m_passGather0.SetRenderTarget(0, pTexDofLayersTmp[0]);
				m_passGather0.SetRenderTarget(1, pTexDofLayersTmp[1]);
				m_passGather0.SetRenderTarget(2, CTexture::s_ptexSceneCoCTemp);
				m_passGather0.SetState(GS_NODEPTHTEST);

				m_passGather0.SetTextureSamplerPair(0, CTexture::s_ptexZTargetScaled, EDefaultSamplerStates::PointClamp);
				m_passGather0.SetTextureSamplerPair(1, CTexture::s_ptexHDRDofLayers[0], EDefaultSamplerStates::LinearClamp);
				m_passGather0.SetTextureSamplerPair(2, CTexture::s_ptexHDRDofLayers[1], EDefaultSamplerStates::LinearClamp);
				m_passGather0.SetTextureSamplerPair(3, CTexture::s_ptexSceneCoC[0], EDefaultSamplerStates::LinearClamp);
				m_passGather0.SetTextureSamplerPair(4, CTexture::s_ptexSceneCoC[MIN_DOF_COC_K - 1], EDefaultSamplerStates::PointClamp);
			}

			m_passGather0.BeginConstantUpdate();
			vDofParams1.z = nSquareTapsSide * nSquareTapsSide;
			m_passGather0.SetConstant(dofFocusParam0Name, vDofParams0, eHWSC_Pixel);
			m_passGather0.SetConstant(dofFocusParam1Name, vDofParams1, eHWSC_Pixel);
			m_passGather0.SetConstantArray(dofTapsName, vTaps, nSquareTapsSide * nSquareTapsSide, eHWSC_Pixel);
			m_passGather0.Execute();
		}

		// 2nd gather iteration
		{
			const int32 nSquareTapsSide = 3;
			const float fRecipTaps = 1.0f / ((float)nSquareTapsSide - 1.0f);

			Vec4 vTaps[nSquareTapsSide * nSquareTapsSide];
			for (int32 y = 0; y < nSquareTapsSide; ++y)
			{
				for (int32 x = 0; x < nSquareTapsSide; ++x)
				{
					Vec4 t = Vec4(x * fRecipTaps, y * fRecipTaps, 0, 0);
					vTaps[x + y * nSquareTapsSide] = ToUnitDisk(t, fNumApertureSides, fFNumber);
				}
			}

			PROFILE_LABEL_SCOPE("FAR/NEAR LAYER ITERATION");

			if (m_passGather1.InputChanged())
			{
				static CCryNameTSCRC techDOF("Dof");
				m_passGather1.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_PS);
				m_passGather1.SetTechnique(pShader, techDOF, g_HWSR_MaskBit[HWSR_SAMPLE0]);
				m_passGather1.SetRenderTarget(0, CTexture::s_ptexHDRDofLayers[0]);
				m_passGather1.SetRenderTarget(1, CTexture::s_ptexHDRDofLayers[1]);
				m_passGather1.SetRenderTarget(2, CTexture::s_ptexSceneCoC[0]);
				m_passGather1.SetState(GS_NODEPTHTEST);

				m_passGather1.SetTextureSamplerPair(0, CTexture::s_ptexZTargetScaled, EDefaultSamplerStates::PointClamp);
				m_passGather1.SetTextureSamplerPair(1, pTexDofLayersTmp[0], EDefaultSamplerStates::LinearClamp);
				m_passGather1.SetTextureSamplerPair(2, pTexDofLayersTmp[1], EDefaultSamplerStates::LinearClamp);
				m_passGather1.SetTextureSamplerPair(3, CTexture::s_ptexSceneCoCTemp, EDefaultSamplerStates::PointClamp);  // TODO: Point filtering good here?
				m_passGather1.SetTextureSamplerPair(4, CTexture::s_ptexSceneCoC[MIN_DOF_COC_K - 1], EDefaultSamplerStates::PointClamp);
			}

			m_passGather1.BeginConstantUpdate();
			vDofParams1.z = nSquareTapsSide * nSquareTapsSide;
			m_passGather1.SetConstant(dofFocusParam0Name, vDofParams0, eHWSC_Pixel);
			m_passGather1.SetConstant(dofFocusParam1Name, vDofParams1, eHWSC_Pixel);
			m_passGather1.SetConstantArray(dofTapsName, vTaps, nSquareTapsSide * nSquareTapsSide, eHWSC_Pixel);
			m_passGather1.Execute();
		}

		// Final composition
		{
			PROFILE_LABEL_SCOPE("COMPOSITE");

			if (m_passComposition.InputChanged())
			{
				static CCryNameTSCRC techCompositeDof("CompositeDof");
				m_passComposition.SetPrimitiveFlags(CRenderPrimitive::eFlags_ReflectShaderConstants_PS);
				m_passComposition.SetTechnique(pShader, techCompositeDof, 0);
				m_passComposition.SetRenderTarget(0, CTexture::s_ptexHDRTarget);
				m_passComposition.SetState(GS_NODEPTHTEST);

				m_passComposition.SetTextureSamplerPair(0, CTexture::s_ptexZTarget, EDefaultSamplerStates::PointClamp);
				m_passComposition.SetTextureSamplerPair(1, CTexture::s_ptexHDRDofLayers[0], EDefaultSamplerStates::LinearClamp);
				m_passComposition.SetTextureSamplerPair(2, CTexture::s_ptexHDRDofLayers[1], EDefaultSamplerStates::LinearClamp);
				m_passComposition.SetTextureSamplerPair(3, CTexture::s_ptexSceneCoCTemp, EDefaultSamplerStates::LinearClamp);
				m_passComposition.SetTextureSamplerPair(4, CTexture::s_ptexSceneTarget, EDefaultSamplerStates::PointClamp);
			}

			m_passComposition.BeginConstantUpdate();
			m_passComposition.SetConstant(dofFocusParam0Name, vDofParams0, eHWSC_Pixel);
			m_passComposition.SetConstant(dofFocusParam1Name, vDofParams1, eHWSC_Pixel);
			m_passComposition.Execute();
		}
	}
}
