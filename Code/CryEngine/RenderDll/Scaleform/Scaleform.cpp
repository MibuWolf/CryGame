// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "StdAfx.h"

#include "../Common/Renderer.h"

//////////////////////////////////////////////////////////////////////////
void CRenderer::RT_FlashRender(IFlashPlayer_RenderProxy* pPlayer, bool stereo)
{
	m_pRT->RC_FlashRender(pPlayer, stereo);
}

void CRenderer::RT_FlashRenderPlaybackLockless(IFlashPlayer_RenderProxy* pPlayer, int cbIdx, bool stereo, bool finalPlayback)
{
	m_pRT->RC_FlashRenderPlaybackLockless(pPlayer, cbIdx, stereo, finalPlayback);
}

void CRenderer::RT_FlashRemoveTexture(ITexture* pTexture)
{
	pTexture->Release();
}
