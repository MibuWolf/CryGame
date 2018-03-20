// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  Created:     06/04/2014 by Filipe amim
//  Description:
// -------------------------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////

#ifndef PARTICLESYSTEM_H
#define PARTICLESYSTEM_H

#pragma once

#include <CryExtension/ClassWeaver.h>
#include "ParticleCommon.h"
#include "ParticleDebug.h"
#include "ParticleJobManager.h"
#include "ParticleProfiler.h"
#include "ParticleEmitter.h"

namespace pfx2
{
class CParticleSystem : public Cry3DEngineBase, public IParticleSystem
{
	CRYINTERFACE_SIMPLE(IParticleSystem)
	CRYGENERATE_SINGLETONCLASS_GUID(CParticleSystem, "CryEngine_ParticleSystem", "cd8d738d-54b4-46f7-82ba-23ba999cf2ac"_cry_guid)

	CParticleSystem();
	virtual ~CParticleSystem() {}

private:
	typedef std::vector<_smart_ptr<CParticleEmitter>>                                                                     TParticleEmitters;
	typedef std::unordered_map<string, _smart_ptr<CParticleEffect>, stl::hash_stricmp<string>, stl::hash_stricmp<string>> TEffectNameMap;

public:
	// IParticleSystem
	PParticleEffect         CreateEffect() override;
	PParticleEffect         ConvertEffect(const ::IParticleEffect* pOldEffect, bool bReplace) override;
	void                    RenameEffect(PParticleEffect pEffect, cstr name) override;
	PParticleEffect         FindEffect(cstr name) override;
	PParticleEmitter        CreateEmitter(PParticleEffect pEffect) override;
	uint                    GetNumFeatureParams() const override;
	SParticleFeatureParams& GetFeatureParam(uint featureIdx) const override;
	TParticleAttributesPtr  CreateParticleAttributes() const override;

	void                    OnFrameStart() override;
	void                    Update() override;
	void                    Reset() override;

	void                    Serialize(TSerialize ser) override;

	void                    GetStats(SParticleStats& stats) override;
	void                    GetMemoryUsage(ICrySizer* pSizer) const override;
	// ~IParticleSystem

	PParticleEffect      LoadEffect(cstr effectName);
	TParticleHeap&       GetMemHeap(uint32 threadId = ~0) { return m_memHeap[threadId + 1]; }
	CParticleJobManager& GetJobManager()                  { return m_jobManager; }
	CParticleProfiler&   GetProfiler()                    { return m_profiler; }
	void                 SyncronizeUpdateKernels();
	void                 DeferredRender();
	float                DisplayDebugStats(Vec2 displayLocation, float lineHeight);

	void                 ClearRenderResources();

	static float         GetMaxAngularDensity(const CCamera& camera)
	{
		return camera.GetAngularResolution() / max(GetCVars()->e_ParticlesMinDrawPixels, 0.125f) * 2.0f;
	}
	QuatT                GetLastCameraPose() const { return m_lastCameraPose; }
	QuatT                GetCameraMotion() const { return m_cameraMotion; }
	TParticleEmitters    GetActiveEmitters() const { return m_emitters; }

private:
	void              UpdateGpuRuntimesForEmitter(CParticleEmitter* pEmitter);
	void              TrimEmitters();
	void              InvalidateCachedRenderObjects();
	CParticleEffect*  CastEffect(const PParticleEffect& pEffect) const;
	CParticleEmitter* CastEmitter(const PParticleEmitter& pEmitter) const;

	// PFX1 to PFX2
	string ConvertPfx1Name(cstr oldEffectName);
	// ~PFX1 to PFX2

private:
	SParticleStats             m_stats;
	CParticleJobManager        m_jobManager;
	CParticleProfiler          m_profiler;
	TEffectNameMap             m_effects;
	TParticleEmitters          m_emitters;
	TParticleEmitters          m_newEmitters;
	std::vector<TParticleHeap> m_memHeap;
	QuatT                      m_lastCameraPose = ZERO;
	QuatT                      m_cameraMotion = ZERO;
	uint                       m_nextEmitterId;
	int32                      m_lastSysSpec;
};

std::vector<SParticleFeatureParams>& GetFeatureParams();

ILINE CParticleSystem*               GetPSystem()
{
	static std::shared_ptr<IParticleSystem> pSystem(GetIParticleSystem());
	return static_cast<CParticleSystem*>(pSystem.get());
};

uint                                 GetVersion(Serialization::IArchive& ar);

}

#endif // PARTICLESYSTEM_H
