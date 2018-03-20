// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#ifndef D3D_COLOR_GRADING_CONTROLLER_H
#define D3D_COLOR_GRADING_CONTROLLER_H

#pragma once

#include "../Common/IColorGradingControllerInt.h"

class CD3D9Renderer;

struct SColorGradingMergeParams
{
	Vec4   pColorMatrix[3];
	Vec4   pLevels[2];
	Vec4   pFilterColor;
	Vec4   pSelectiveColor[2];
	uint64 nFlagsShaderRT;
};

class CColorGradingControllerD3D : public IColorGradingControllerInt
{
	friend class CColorGradingStage;

public:
	// IColorGradingController interface
	virtual int  LoadColorChart(const char* pChartFilePath) const;
	virtual void UnloadColorChart(int texID) const;

	virtual void SetLayers(const SColorChartLayer* pLayers, uint32 numLayers);

public:
	// IColorGradingController internal interface
	virtual void RT_SetLayers(const SColorChartLayer* pLayers, uint32 numLayers);

public:
	CColorGradingControllerD3D(CD3D9Renderer* pRenderer);
	virtual ~CColorGradingControllerD3D();

	bool            Update(const SColorGradingMergeParams* pMergeParams = 0);
	CTexture*       GetColorChart() const;
	void            DrawDebugInfo() const;

	bool            LoadStaticColorChart(const char* pChartFilePath);
	const CTexture* GetStaticColorChart() const;

	void            FreeMemory();

	int             GetColorChartSize() const;

private:
	typedef std::vector<SColorChartLayer> Layers;

private:
	bool      ValidateColorChart(const CTexture* pChart) const;
	CTexture* LoadColorChartInt(const char* pChartFilePath) const;
	bool      InitResources();
	void      DrawLayer(float x, float y, float w, float h, CTexture* pChart, float blendAmount, const char* pLayerName) const;

private:
	Layers                       m_layers;
	CD3D9Renderer*               m_pRenderer;

	// TODO: remove once we don't need r_graphicspipeline=0 anymore
	CTexture*                    m_pChartIdentity;
	CTexture*                    m_pChartStatic;
	CTexture*                    m_pChartToUse;
	CTexture*                    m_pMergeLayers[2];
};

#endif // #ifndef D3D_COLOR_GRADING_CONTROLLER_H
