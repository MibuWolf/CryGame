// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  Created:     29/09/2014 by Filipe amim
//  Description:
// -------------------------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "ParticleSystem/ParticleEmitter.h"
#include <CrySerialization/SmartPtr.h>
#include "FeatureColor.h"
#include "TimeSource.h"

CRY_PFX2_DBG

namespace pfx2
{

EParticleDataType PDT(EPDT_Color, UCol, 1, BHasInit(true));

void IColorModifier::Serialize(Serialization::IArchive& ar)
{
	ar(m_enabled);
}

CFeatureFieldColor::CFeatureFieldColor() : m_color(255, 255, 255), CParticleFeature(gpu_pfx2::eGpuFeatureType_Color) {}

void CFeatureFieldColor::AddToComponent(CParticleComponent* pComponent, SComponentParams* pParams)
{
	m_modInit.clear();
	m_modUpdate.clear();

	pComponent->AddToUpdateList(EUL_InitUpdate, this);
	pComponent->AddParticleData(EPDT_Color);

	for (auto& pModifier : m_modifiers)
	{
		if (pModifier && pModifier->IsEnabled())
			pModifier->AddToParam(pComponent, this);
	}
	if (!m_modUpdate.empty())
	{
		pComponent->AddParticleData(InitType(EPDT_Color));
		pComponent->AddToUpdateList(EUL_Update, this);
	}

	if (auto pInt = GetGpuInterface())
	{
		const int numSamples = gpu_pfx2::kNumModifierSamples;
		Vec3 samples[numSamples];
		Sample(samples, numSamples);
		gpu_pfx2::SFeatureParametersColorTable table;
		table.samples = samples;
		table.numSamples = numSamples;
		pInt->SetParameters(table);
	}
}

void CFeatureFieldColor::Serialize(Serialization::IArchive& ar)
{
	CParticleFeature::Serialize(ar);
	SModParticleField modContext;
	Serialization::SContext _modContext(ar, static_cast<IParamModContext*>(&modContext));
	struct SerStruct
	{
		SerStruct(std::vector<PColorModifier>& modifiers, ColorB& color)
			: m_modifiers(modifiers), m_color(color) {}
		void Serialize(Serialization::IArchive& ar)
		{
			ar(m_color, "Color", "^");
			ar(m_modifiers, "Modifiers", "^");
		}
		std::vector<PColorModifier>& m_modifiers;
		ColorB&                      m_color;
	} serStruct(m_modifiers, m_color);
	ar(serStruct, "Color", "Color");
}

void CFeatureFieldColor::InitParticles(const SUpdateContext& context)
{
	CRY_PFX2_PROFILE_DETAIL;

	CParticleContainer& container = context.m_container;
	IOColorStream colors = container.GetIOColorStream(EPDT_Color);

	UCol uColor;
	uColor.dcolor = m_color.pack_argb8888() | 0xff000000;
	UColv baseColor = ToUColv(uColor);
	CRY_PFX2_FOR_SPAWNED_PARTICLEGROUP(context)
	{
		colors.Store(particleGroupId, baseColor);
	}
	CRY_PFX2_FOR_END

	SUpdateRange spawnRange = context.m_container.GetSpawnedRange();
	for (auto& pModifier : m_modInit)
		pModifier->Modify(context, spawnRange, colors);

	container.CopyData(InitType(EPDT_Color), EPDT_Color, container.GetSpawnedRange());
}

void CFeatureFieldColor::Update(const SUpdateContext& context)
{
	CRY_PFX2_PROFILE_DETAIL;

	CParticleContainer& container = context.m_container;
	IOColorStream colors = container.GetIOColorStream(EPDT_Color);

	for (auto& pModifier : m_modUpdate)
		pModifier->Modify(context, context.m_updateRange, colors);
}

void CFeatureFieldColor::AddToInitParticles(IColorModifier* pMod)
{
	if (std::find(m_modInit.begin(), m_modInit.end(), pMod) == m_modInit.end())
		m_modInit.push_back(pMod);
}

void CFeatureFieldColor::AddToUpdate(IColorModifier* pMod)
{
	if (std::find(m_modUpdate.begin(), m_modUpdate.end(), pMod) == m_modUpdate.end())
		m_modUpdate.push_back(pMod);
}

void CFeatureFieldColor::Sample(Vec3* samples, const int numSamples)
{
	Vec3 baseColor(m_color.r / 255.f, m_color.g / 255.f, m_color.b / 255.f);

	for (int i = 0; i < numSamples; ++i)
		samples[i] = baseColor;
	for (auto& pModifier : m_modUpdate)
		pModifier->Sample(samples, numSamples);
}

CRY_PFX2_IMPLEMENT_FEATURE(CParticleFeature, CFeatureFieldColor, "Field", "Color", colorField);

//////////////////////////////////////////////////////////////////////////
// CColorRandom

class CColorRandom : public IColorModifier
{
public:

	virtual void AddToParam(CParticleComponent* pComponent, CFeatureFieldColor* pParam)
	{
		pParam->AddToInitParticles(this);
	}

	virtual void Serialize(Serialization::IArchive& ar)
	{
		IColorModifier::Serialize(ar);
		ar(m_luminance, "Luminance", "Luminance");
		ar(m_rgb, "RGB", "RGB");
	}

	virtual void Modify(const SUpdateContext& context, const SUpdateRange& range, IOColorStream stream) const
	{
		CRY_PFX2_PROFILE_DETAIL;

		if (m_luminance && m_rgb)
			DoModify<true, true>(context, range, stream);
		else if (m_luminance)
			DoModify<true, false>(context, range, stream);
		if (m_rgb)
			DoModify<false, true>(context, range, stream);
	}

private:

	template<bool doLuminance, bool doRGB>
	void DoModify(const SUpdateContext& context, const SUpdateRange& range, IOColorStream stream) const
	{
		SChaosKeyV::Range randRange(1.0f - m_luminance, 1.0f);
		floatv rgb = ToFloatv(m_rgb),
		       unrgb = ToFloatv(1.0f - m_rgb);

		CRY_PFX2_FOR_RANGE_PARTICLESGROUP(range);
		{
			ColorFv color = ToColorFv(stream.Load(particleGroupId));
			if (doLuminance)
			{
				const floatv lum = context.m_spawnRngv.Rand(randRange);
				color = color * lum;
			}
			if (doRGB)
			{
				ColorFv randColor(
					context.m_spawnRngv.RandUNorm(),
					context.m_spawnRngv.RandUNorm(),
					context.m_spawnRngv.RandUNorm());
				color = color * unrgb + randColor * rgb;
			}
			stream.Store(particleGroupId, ColorFvToUColv(color));
		}
		CRY_PFX2_FOR_END;
	}

	UFloat m_luminance;
	UFloat m_rgb;
};

SERIALIZATION_CLASS_NAME(IColorModifier, CColorRandom, "ColorRandom", "Color Random");

//////////////////////////////////////////////////////////////////////////
// CColorCurve

class CColorCurve : public CTimeSource, public IColorModifier
{
public:
	virtual void AddToParam(CParticleComponent* pComponent, CFeatureFieldColor* pParam)
	{
		if (m_spline.HasKeys())
			CTimeSource::AddToParam(pComponent, pParam, this);
	}

	virtual void Serialize(Serialization::IArchive& ar)
	{
		IColorModifier::Serialize(ar);
		CTimeSource::SerializeInplace(ar);
		string desc = ar.isEdit() ? GetSourceDescription() : "";
		Serialization::SContext _splineContext(ar, desc.data());
		ar(m_spline, "ColorCurve", "Color Curve");
	}

	virtual void Modify(const SUpdateContext& context, const SUpdateRange& range, IOColorStream stream) const
	{
		CRY_PFX2_PROFILE_DETAIL;
		CTimeSource::Dispatch<CColorCurve>(context, range, stream, EMD_PerParticle);
	}

	template<typename TTimeKernel>
	void DoModify(const SUpdateContext& context, const SUpdateRange& range, IOColorStream stream, const TTimeKernel& timeKernel) const
	{
		const floatv rate = ToFloatv(m_timeScale);
		const floatv offset = ToFloatv(m_timeBias);

		CRY_PFX2_FOR_RANGE_PARTICLESGROUP(range);
		{
			const floatv sample = MAdd(timeKernel.Sample(particleGroupId), rate, offset);
			const ColorFv color0 = ToColorFv(stream.Load(particleGroupId));
			const ColorFv curve = m_spline.Interpolate(sample);
			const ColorFv color1 = color0 * curve;
			stream.Store(particleGroupId, ColorFvToUColv(color1));
		}
		CRY_PFX2_FOR_END;
	}

	virtual void Sample(Vec3* samples, int samplePoints) const
	{
		for (int i = 0; i < samplePoints; ++i)
		{
			const float point = (float) i / samplePoints;
			Vec3 color0 = samples[i];
			ColorF curve = m_spline.Interpolate(point);
			Vec3 color1(color0.x * curve.r, color0.y * curve.g, color0.z * curve.b);
			samples[i] = color1;
		}
	}
private:
	CParticleColorSpline m_spline;
};

SERIALIZATION_CLASS_NAME(IColorModifier, CColorCurve, "ColorCurve", "Color Curve");

//////////////////////////////////////////////////////////////////////////
// CColorAttribute

class CColorAttribute : public IColorModifier
{
public:
	CColorAttribute()
		: m_scale(1.0f)
		, m_bias(0.0f)
		, m_gamma(1.0f) 
		, m_spawnOnly(false) {}

	virtual void AddToParam(CParticleComponent* pComponent, CFeatureFieldColor* pParam)
	{
		if (m_spawnOnly)
			pParam->AddToInitParticles(this);
		else
			pParam->AddToUpdate(this);
	}

	virtual void Serialize(Serialization::IArchive& ar)
	{
		IColorModifier::Serialize(ar);
		ar(m_name, "Name", "Attribute Name");
		ar(m_scale, "Scale", "Scale");
		ar(m_bias, "Bias", "Bias");
		ar(m_gamma, "Gamma", "Gamma");
		ar(m_spawnOnly, "SpawnOnly", "Spawn Only");
	}

	virtual void Modify(const SUpdateContext& context, const SUpdateRange& range, IOColorStream stream) const
	{
		CRY_PFX2_PROFILE_DETAIL;

		CParticleContainer& container = context.m_container;
		const CAttributeInstance& attributes = context.m_runtime.GetEmitter()->GetAttributeInstance();
		auto attributeId = attributes.FindAttributeIdByName(m_name.c_str());
		ColorF attribute = attributes.GetAsColorF(attributeId, ColorF(1.0f, 1.0f, 1.0f));
		attribute.r = pow(attribute.r, m_gamma) * m_scale + m_bias;
		attribute.g = pow(attribute.g, m_gamma) * m_scale + m_bias;
		attribute.b = pow(attribute.b, m_gamma) * m_scale + m_bias;
		const ColorFv value = ToColorFv(attribute);

		CRY_PFX2_FOR_RANGE_PARTICLESGROUP(range)
		{
			const ColorFv color0 = ToColorFv(stream.Load(particleGroupId));
			const ColorFv color1 = color0 * value;
			stream.Store(particleGroupId, ColorFvToUColv(color1));
		}
		CRY_PFX2_FOR_END;
	}

private:
	string m_name;
	SFloat m_scale;
	SFloat m_bias;
	UFloat m_gamma;
	bool   m_spawnOnly;
};

SERIALIZATION_CLASS_NAME(IColorModifier, CColorAttribute, "Attribute", "Attribute");

//////////////////////////////////////////////////////////////////////////
// CColorInherit

class CColorInherit : public IColorModifier
{
public:
	CColorInherit()
		: m_spawnOnly(true) {}

	virtual void AddToParam(CParticleComponent* pComponent, CFeatureFieldColor* pParam)
	{
		if (m_spawnOnly)
			pParam->AddToInitParticles(this);
		else
			pParam->AddToUpdate(this);
	}

	virtual void Serialize(Serialization::IArchive& ar)
	{
		IColorModifier::Serialize(ar);
		ar(m_spawnOnly, "SpawnOnly", "Spawn Only");
	}

	virtual void Modify(const SUpdateContext& context, const SUpdateRange& range, IOColorStream stream) const
	{
		CRY_PFX2_PROFILE_DETAIL;

		CParticleContainer& container = context.m_container;
		CParticleContainer& parentContainer = context.m_parentContainer;
		if (!parentContainer.HasData(EPDT_Color))
			return;
		IPidStream parentIds = context.m_container.GetIPidStream(EPDT_ParentId);
		IColorStream parentColors = parentContainer.GetIColorStream(EPDT_Color, UCol{ {~0u} });

		CRY_PFX2_FOR_RANGE_PARTICLESGROUP(range)
		{
			const TParticleIdv parentId = parentIds.Load(particleGroupId);
			const ColorFv color0 = ToColorFv(stream.Load(particleGroupId));
			const ColorFv parent = ToColorFv(parentColors.Load(parentId));
			const ColorFv color1 = color0 * parent;
			stream.Store(particleGroupId, ColorFvToUColv(color1));
		}
		CRY_PFX2_FOR_END;
	}

private:
	bool m_spawnOnly;
};

SERIALIZATION_CLASS_NAME(IColorModifier, CColorInherit, "Inherit", "Inherit");

}
