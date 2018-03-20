// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  Created:     06/02/2015 by Filipe amim
//  Description:
// -------------------------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <CryMath/FinalizingSpline.h>
#include "ParticleCommon.h"
#include "ParticleDebug.h"
#include "ParticleContainer.h"
#include "ParticleComponent.h"
#include "ParticleComponentRuntime.h"
#include "ParticleEffect.h"
#include "ParticleEmitter.h"
#include "Features/FeatureCollision.h"

CRY_PFX2_DBG

namespace pfx2
{

#if !defined(_RELEASE)

namespace
{

const ColorB black(0, 0, 0);
const ColorB white(255, 255, 255);
const ColorF whiteF(1.0f, 1.0f, 1.0f);
const ColorB lightOrange(255, 198, 128);
const ColorB red(192, 23, 32);
const ColorB darkRed(96, 0, 0);

const float startPos = 1.0f / 40.0f;
const float barSz = 1.0f / 192.0f;
const float barGap = 1.0f / 24.0f;

void DebugDrawEffect(CParticleEffect* pEffect, size_t effectBarIdx)
{
#if 0
	// emitter bars
	IRenderer* pRender = gEnv->pRenderer;
	IRenderAuxGeom* pRenderAux = gEnv->pRenderer->GetIRenderAuxGeom();
	Vec2 screenSz = Vec2(float(gEnv->pRenderer->GetWidth()), float(gEnv->pRenderer->GetHeight()));

	size_t numEmitters = pEffect->GetEmitterCount();
	Vec2 pixSz = Vec2(1.0f / gEnv->pRenderer->GetWidth(), 1.0f / gEnv->pRenderer->GetHeight());
	const Vec2 emitterSz = Vec2(barSz, 0.9f / numEmitters);
	const Vec2 emitterLoc = Vec2(effectBarIdx * barGap + startPos, startPos);

	Vec3 pos = Vec2(effectBarIdx * barGap + startPos, startPos);
	for (size_t i = 0; i < numEmitters; ++i)
	{
		AABB box;
		box.min = pos;
		box.max.x = box.min.x + std::max(emitterSz.x, pixSz.x);
		box.max.y = box.min.y + std::max(emitterSz.y - pixSz.y, pixSz.y);
		ColorB color = pEffect->GetEmitter(i).m_active ? red : darkRed;
		pRenderAux->DrawAABB(box, true, color, eBBD_Faceted);
		pos.y += emitterSz.y;
	}

	pRender->Draw2dLabel(
	  screenSz.x * (effectBarIdx * barGap + startPos),
	  screenSz.y * 0.975f, 1.0f, whiteF, false,
	  pEffect->GetName());
#endif
}

void DebugDrawComponentRuntime(CParticleComponentRuntime* pRuntime, size_t emitterBarIdx, size_t barIdx)
{
	IRenderAuxGeom* pRenderAux = gEnv->pRenderer->GetIRenderAuxGeom();
	SAuxGeomRenderFlags prevFlags = pRenderAux->GetRenderFlags();
	SAuxGeomRenderFlags curFlags = prevFlags;
	Vec2 screenSz = Vec2(float(gEnv->pRenderer->GetWidth()), float(gEnv->pRenderer->GetHeight()));
	curFlags.SetMode2D3DFlag(e_Mode2D);
	curFlags.SetDepthTestFlag(e_DepthTestOff);
	curFlags.SetDepthWriteFlag(e_DepthWriteOff);
	pRenderAux->SetRenderFlags(curFlags);

	const SUpdateContext context = SUpdateContext(pRuntime);
	const CParticleContainer& container = context.m_container;
	const SComponentParams& params = context.m_params;
	const size_t numInstances = pRuntime->GetNumInstances();
	IPidStream parentIds = container.GetIPidStream(EPDT_ParentId);
	TIStream<uint8> states = container.GetTIStream<uint8>(EPDT_State);
	IFStream normAges = container.GetIFStream(EPDT_NormalAge);
	const Vec2 pixSz = Vec2(1.0f / gEnv->pRenderer->GetWidth(), 1.0f / gEnv->pRenderer->GetHeight());
	const Vec2 partSz = Vec2(barSz, 0.9f / container.GetMaxParticles());
	const Vec2 contLoc = Vec2(barIdx * barGap + barSz * 3.0f + startPos, startPos);
	const float off = barSz * 0.5f;
	Vec2 pos;

	// instance bars
	if (numInstances)
	{
		const Vec2 instSz = Vec2(barSz, 0.9f / numInstances);
		const Vec2 instLoc = Vec2(barIdx * barGap + startPos, startPos);
		pos = instLoc;
		for (size_t i = 0; i < numInstances; ++i)
		{
			AABB box;
			box.min = pos;
			box.max.x = box.min.x + std::max(instSz.x, pixSz.x);
			box.max.y = box.min.y + std::max(instSz.y - pixSz.y, pixSz.y);
			ColorF color = ColorF(0.0f, 0.5f, 1.0f);
			pRenderAux->DrawAABB(box, true, ColorB(color), eBBD_Faceted);
			pos.y += instSz.y;
		}
	}

	// particle bars
	pos = contLoc;
	CRY_PFX2_FOR_ACTIVE_PARTICLES(context)
	{
		AABB box;
		// box.min = pos;
		box.min.x = contLoc.x;
		box.min.y = contLoc.y + partSz.y * particleId;
		box.max.x = box.min.x + std::max(partSz.x, pixSz.x);
		box.max.y = box.min.y + std::max(partSz.y - pixSz.y, pixSz.y);
		ColorB color = black;
		const uint8 state = states.Load(particleId);
		const float age = normAges.Load(particleId);
		if (state == ES_Alive)
			color = ColorB(ColorF(age, 1.0f - age, 0.0f));
		pRenderAux->DrawAABB(box, true, color, eBBD_Faceted);
	}
	CRY_PFX2_FOR_END;

	if (params.IsSecondGen())
	{
		const CParticleContainer& parentContainer = pRuntime->GetParentContainer();
		const Vec2 parentPartSz = Vec2(barSz, 0.9f / parentContainer.GetMaxParticles());
		const Vec2 parentContLoc = Vec2((emitterBarIdx + params.m_parentId) * barGap + barSz * 3.0f + startPos, startPos);

		// parenting lines
		CRY_PFX2_FOR_ACTIVE_PARTICLES(context)
		{
			const TParticleId parentId = parentIds.Load(particleId);
			if (parentId == gInvalidId)
				continue;
			Vec2 from = contLoc + partSz * 0.5f + Vec2(-off, partSz.y * particleId);
			Vec2 to = parentContLoc + parentPartSz * 0.5f + Vec2(off, parentPartSz.y * parentId);
			pRenderAux->DrawLine(from, lightOrange, to, black, 1.0f);
		}
		CRY_PFX2_FOR_END;
	}

	IRenderAuxText::Draw2dLabel(
	  screenSz.x * (barIdx * barGap + startPos),
	  screenSz.y * 0.95f, 1.0f, whiteF, false,
	  "%s", pRuntime->GetComponent()->GetName());

	pRenderAux->SetRenderFlags(prevFlags);
}

void DebugDrawComponentCollisions(CParticleComponentRuntime* pRuntime)
{
	IRenderAuxGeom* pRenderAux = gEnv->pRenderer->GetIRenderAuxGeom();
	SAuxGeomRenderFlags prevFlags = pRenderAux->GetRenderFlags();
	SAuxGeomRenderFlags curFlags = prevFlags;
	Vec2 screenSz = Vec2(float(gEnv->pRenderer->GetWidth()), float(gEnv->pRenderer->GetHeight()));
	curFlags.SetMode2D3DFlag(e_Mode3D);
	curFlags.SetDepthTestFlag(e_DepthTestOn);
	curFlags.SetDepthWriteFlag(e_DepthWriteOn);
	pRenderAux->SetRenderFlags(curFlags);

	const auto context = SUpdateContext(pRuntime);
	const CParticleContainer& container = pRuntime->GetContainer();
	if (!container.HasData(EPDT_ContactPoint))
		return;
	const TIStream<SContactPoint> contactPoints = container.GetTIStream<SContactPoint>(EPDT_ContactPoint);

	CRY_PFX2_FOR_ACTIVE_PARTICLES(context)
	{
		SContactPoint contact = contactPoints.Load(particleId);

		if (contact.m_totalCollisions == 0)
			continue;

		ColorB color = ColorB(0, 0, 0);
		if (contact.m_state.ignore)
			color = ColorB(128, 128, 128);
		else if (contact.m_state.sliding)
			color = ColorB(255, 128, 64);
		else
			color = ColorB(64, 128, 255);

		pRenderAux->DrawSphere(contact.m_point, 0.05f, color);
		pRenderAux->DrawLine(
			contact.m_point, color,
			contact.m_point + contact.m_normal*0.25f, color);
	}
	CRY_PFX2_FOR_END;

	pRenderAux->SetRenderFlags(prevFlags);
}

#if CRY_PLATFORM_SSE2

void DrawLineSSE(IRenderAuxGeom* pRenderAux, __m128 x, __m128 y, ColorB color)
{
	#if 0
	pRenderAux->DrawLine(
	  Vec2(x.m128_f32[3], (1.0f - y.m128_f32[3]) * 0.9f + 0.05f), color,
	  Vec2(x.m128_f32[2], (1.0f - y.m128_f32[2]) * 0.9f + 0.05f), color, 1.0f);
	pRenderAux->DrawLine(
	  Vec2(x.m128_f32[2], (1.0f - y.m128_f32[2]) * 0.9f + 0.05f), color,
	  Vec2(x.m128_f32[1], (1.0f - y.m128_f32[1]) * 0.9f + 0.05f), color, 1.0f);
	pRenderAux->DrawLine(
	  Vec2(x.m128_f32[1], (1.0f - y.m128_f32[1]) * 0.9f + 0.05f), color,
	  Vec2(x.m128_f32[0], (1.0f - y.m128_f32[0]) * 0.9f + 0.05f), color, 1.0f);
	#endif
}

#endif

void DebugOptSpline()
{
#if 0
	typedef spline::OptSpline<float> TOptSplineF;

	static bool init = false;
	static TOptSplineF optSpline;
	static CParticleSpline particleSpline;
	if (!init)
	{
		init = true;

		CParticleSpline::CSourceSpline source;
		CParticleSpline::CSourceSpline::key_type key;

		key.flags =
		  (SPLINE_KEY_TANGENT_LINEAR << SPLINE_KEY_TANGENT_IN_SHIFT) |
		  (SPLINE_KEY_TANGENT_NONE << SPLINE_KEY_TANGENT_OUT_SHIFT);
		key.time = 0.1f;
		key.value = 0.2f;
		source.insert_key(key);
		key.time = 0.25f;
		key.value = 1.0f;
		source.insert_key(key);
		key.time = 0.7f;
		key.value = 0.5f;
		source.insert_key(key);
		key.time = 0.95f;
		key.value = 0.0f;
		source.insert_key(key);

		source.update();
		optSpline.from_source(source);
		particleSpline.FromSpline(source);
	}

	IRenderer* pRender = gEnv->pRenderer;
	IRenderAuxGeom* pRenderAux = gEnv->pRenderer->GetIRenderAuxGeom();
	SAuxGeomRenderFlags prevFlags = pRenderAux->GetRenderFlags();
	SAuxGeomRenderFlags curFlags = prevFlags;
	Vec2 screenSz = Vec2(float(gEnv->pRenderer->GetWidth()), float(gEnv->pRenderer->GetHeight()));
	curFlags.SetMode2D3DFlag(e_Mode2D);
	pRenderAux->SetRenderFlags(curFlags);

	const int numSamples = 100;
	const float sampleSz = 1.0f / numSamples;
	float prevValue;

	optSpline.interpolate(0.0f, prevValue);
	for (float t = sampleSz; t <= 1.0f; t += sampleSz)
	{
		float curValue;
		optSpline.interpolate(t, curValue);
		pRenderAux->DrawLine(
		  Vec2(t - sampleSz, (1.0f - prevValue) * 0.9f + 0.05f), red,
		  Vec2(t, (1.0f - curValue) * 0.9f + 0.05f), red, 1.0f);
		prevValue = curValue;
	}

	prevValue = particleSpline.Interpolate(0.0f);
	for (float t = sampleSz; t <= 1.0f; t += sampleSz)
	{
		float curValue;
		curValue = particleSpline.Interpolate(t);
		pRenderAux->DrawLine(
		  Vec2(t - sampleSz, (1.0f - prevValue) * 0.9f + 0.05f), lightOrange,
		  Vec2(t, (1.0f - curValue) * 0.9f + 0.05f), lightOrange, 1.0f);
		prevValue = curValue;
	}

	const ColorB blue(128, 198, 255);
	for (float t = 0.0f; t < 1.0f; t += sampleSz * 4.0f)
	{
		__m128 timeSSE = _mm_set_ps(t, t + sampleSz, t + sampleSz * 2.0f, t + sampleSz * 3.0f);
		__m128 curValueSSE = particleSpline.Interpolate(timeSSE);
		DrawLineSSE(pRenderAux, timeSSE, curValueSSE, blue);
	}

	pRenderAux->SetRenderFlags(prevFlags);
#endif
}

}

void DebugParticleSystem(const std::vector<_smart_ptr<CParticleEmitter>>& activeEmitters)
{
	DebugOptSpline();

	CVars* pCVars = static_cast<C3DEngine*>(gEnv->p3DEngine)->GetCVars();
	const bool internalDebug = (pCVars->e_ParticlesDebug & AlphaBit('t')) != 0;
	static volatile uint debugContainers = 0;
	static volatile uint debugCollisions = 1;

	if (internalDebug)
	{
		IRenderer* pRender = gEnv->pRenderer;
		IRenderAuxGeom* pRenderAux = gEnv->pRenderer->GetIRenderAuxGeom();

		size_t emitterBarIdx = 0;
		size_t barIdx = 0;
		for (CParticleEmitter* pEmitter : activeEmitters)
		{
			CParticleEffect* pEffect = pEmitter->GetCEffect();
			TComponentId lastComponentIt = pEffect->GetNumComponents();
			for (TComponentId componentId = 0; componentId < lastComponentIt; ++componentId)
			{
				auto pComponentRuntime = pEmitter->GetRuntimes()[componentId].pRuntime->GetCpuRuntime();
				if (!pComponentRuntime)
					continue;
				if (!pComponentRuntime->GetComponent()->IsEnabled())
					continue;
				if (debugContainers)
					DebugDrawComponentRuntime(pComponentRuntime, emitterBarIdx, barIdx++);
				if (debugCollisions)
					DebugDrawComponentCollisions(pComponentRuntime);
			}
			emitterBarIdx = barIdx;
		}
	}
}

#else

void DebugParticleSystem(const std::vector<_smart_ptr<CParticleEmitter>>& activeEmitters) {}

#endif

}
