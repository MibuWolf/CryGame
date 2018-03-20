// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  Created:     27/10/2014 by Filipe amim
//  Description:
// -------------------------------------------------------------------------
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <CrySerialization/IArchive.h>
#include <CrySerialization/Decorators/Resources.h>
#include <CrySerialization/Enum.h>
#include "ParticleSystem/ParticleFeature.h"

CRY_PFX2_DBG

namespace pfx2
{

EParticleDataType PDT(EPDT_Tile, uint8);

SERIALIZATION_ENUM_DEFINE(EVariantMode, ,
                          Random,
                          Ordered
                          )

class CFeatureAppearanceTextureTiling : public CParticleFeature
{
public:
	CRY_PFX2_DECLARE_FEATURE

	CFeatureAppearanceTextureTiling()
		: m_variantMode(EVariantMode::Random)
		, CParticleFeature(gpu_pfx2::eGpuFeatureType_Dummy) {}

	uint VariantCount() const
	{
		return m_tileCount / max(1u, uint(m_anim.m_frameCount));
	}

	virtual void AddToComponent(CParticleComponent* pComponent, SComponentParams* pParams) override
	{
		pParams->m_shaderData.m_tileSize[0] = 1.0f / float(m_tilesX);
		pParams->m_shaderData.m_tileSize[1] = 1.0f / float(m_tilesY);
		pParams->m_shaderData.m_frameCount = float(m_tileCount);
		pParams->m_shaderData.m_firstTile = float(m_firstTile);
		pParams->m_textureAnimation = m_anim;

		if (m_anim.IsAnimating() && m_anim.m_frameBlending)
			pParams->m_renderStateFlags |= OS_ANIM_BLEND;

		if (VariantCount() > 1)
		{
			pComponent->AddParticleData(EPDT_Tile);
			if (m_variantMode == EVariantMode::Ordered)
				pComponent->AddParticleData(EPDT_SpawnId);
			pComponent->AddToUpdateList(EUL_InitUpdate, this);
		}
	}

	virtual void InitParticles(const SUpdateContext& context) override
	{
		CRY_PROFILE_FUNCTION(PROFILE_PARTICLE);

		if (m_variantMode == EVariantMode::Random)
			AssignTiles<EVariantMode::Random>(context);
		else
			AssignTiles<EVariantMode::Ordered>(context);
	}

	virtual void Serialize(Serialization::IArchive& ar) override
	{
		CParticleFeature::Serialize(ar);
		ar(m_tilesX, "TilesX", "Tiles X");
		ar(m_tilesY, "TilesY", "Tiles Y");
		ar(m_tileCount, "TileCount", "Tile Count");
		ar(m_firstTile, "FirstTile", "First Tile");
		ar(m_variantMode, "VariantMode", "Variant Mode");
		ar(m_anim, "Animation", "Animation");
	}

private:
	UBytePos          m_tilesX;
	UBytePos          m_tilesY;
	UBytePos          m_tileCount;
	UByte             m_firstTile;
	EVariantMode      m_variantMode;
	STextureAnimation m_anim;

	template<EVariantMode mode>
	void AssignTiles(const SUpdateContext& context)
	{
		CParticleContainer& container = context.m_container;
		TIOStream<uint8> tiles = container.GetTIOStream<uint8>(EPDT_Tile);
		TIStream<uint> spawnIds = container.GetTIStream<uint>(EPDT_SpawnId);
		uint variantCount = VariantCount();

		CRY_PFX2_FOR_SPAWNED_PARTICLES(context);
		uint32 tile;
		if (mode == EVariantMode::Random)
		{
			tile = context.m_spawnRng.Rand();
		}
		else if (mode == EVariantMode::Ordered)
		{
			tile = spawnIds.Load(particleId);
		}
		tile %= variantCount;
		tile *= m_anim.m_frameCount;
		tiles.Store(particleId, tile);
		CRY_PFX2_FOR_END;
	}

};

CRY_PFX2_IMPLEMENT_FEATURE(CParticleFeature, CFeatureAppearanceTextureTiling, "Appearance", "Texture Tiling", colorAppearance);

class CFeatureAppearanceMaterial : public CParticleFeature
{
public:
	CRY_PFX2_DECLARE_FEATURE

	CFeatureAppearanceMaterial()
		: CParticleFeature(gpu_pfx2::eGpuFeatureType_Dummy) {}

	virtual void AddToComponent(CParticleComponent* pComponent, SComponentParams* pParams) override
	{
		if (!m_materialName.empty())
			pParams->m_pMaterial = gEnv->p3DEngine->GetMaterialManager()->LoadMaterial(m_materialName.c_str());
		if (!m_textureName.empty())
			pParams->m_diffuseMap = m_textureName;
	}

	virtual void Serialize(Serialization::IArchive& ar) override
	{
		using Serialization::MaterialPicker;
		using Serialization::TextureFilename;
		CParticleFeature::Serialize(ar);
		ar(MaterialPicker(m_materialName), "Material", "Material");
		ar(TextureFilename(m_textureName), "Texture", "Texture");
	}

	virtual uint GetNumResources() const override 
	{ 
		return !m_materialName.empty() || !m_textureName.empty() ? 1 : 0; 
	}

	virtual const char* GetResourceName(uint resourceId) const override
	{ 
		// Material has priority over the texture.
		return !m_materialName.empty() ? m_materialName.c_str() : m_textureName.c_str();
	}

private:
	string m_materialName;
	string m_textureName;
};

CRY_PFX2_IMPLEMENT_FEATURE(CParticleFeature, CFeatureAppearanceMaterial, "Appearance", "Material", colorAppearance);

static const float kiloScale = 1000.0f;
static const float toLightUnitScale = kiloScale / RENDERER_LIGHT_UNIT_SCALE;

class CFeatureAppearanceLighting : public CParticleFeature
{
public:
	CRY_PFX2_DECLARE_FEATURE

	CFeatureAppearanceLighting()
		: m_diffuse(1.0f)
		, m_backLight(0.0f)
		, m_emissive(0.0f)
		, m_curvature(0.0f)
		, m_receiveShadows(false)
		, m_affectedByFog(true)
		, m_environmentLighting(true)
		, CParticleFeature(gpu_pfx2::eGpuFeatureType_Dummy) {}

	virtual void AddToComponent(CParticleComponent* pComponent, SComponentParams* pParams) override
	{
		// light energy normalizer for the light equation:
		//	L = max(0, cos(a)*(1-y)+y)
		//	cos(a) = dot(l, n)
		//	y = back lighting
		const float y = m_backLight;
		const float energyNorm = (y < 0.5) ? (1 - y) : (1.0f / (4.0f * y));
		pParams->m_shaderData.m_diffuseLighting = m_diffuse * energyNorm;
		pParams->m_shaderData.m_backLighting = m_backLight;
		pParams->m_shaderData.m_emissiveLighting = m_emissive * toLightUnitScale;
		pParams->m_shaderData.m_curvature = m_curvature;
		if (m_diffuse >= FLT_EPSILON)
			pParams->m_renderObjectFlags |= FOB_LIGHTVOLUME;
		if (m_receiveShadows)
			pParams->m_renderObjectFlags |= FOB_INSHADOW;
		if (!m_affectedByFog)
			pParams->m_renderObjectFlags |= FOB_NO_FOG;
		if (m_environmentLighting)
			pParams->m_renderStateFlags |= OS_ENVIRONMENT_CUBEMAP;
	}

	virtual void Serialize(Serialization::IArchive& ar) override
	{
		CParticleFeature::Serialize(ar);
		ar(m_diffuse, "Diffuse", "Diffuse");
		ar(m_backLight, "BackLight", "Back Light");
		ar(m_emissive, "Emissive", "Emissive (kcd/m2)");
		ar(m_curvature, "Curvature", "Curvature");
		ar(m_receiveShadows, "ReceiveShadows", "Receive Shadows");
		ar(m_affectedByFog, "AffectedByFog", "Affected by Fog");
		ar(m_environmentLighting, "EnvironmentLighting", "Environment Lighting");
		if (ar.isInput())
			VersionFix(ar);
	}

private:
	void VersionFix(Serialization::IArchive& ar)
	{
		uint version = GetVersion(ar);
		if (version == 1)
		{
			m_emissive.Set(m_emissive / toLightUnitScale);
		}
		else if (version < 10)
		{
			if (ar(m_diffuse, "Albedo"))
				m_diffuse.Set(m_diffuse * 0.01f);
		}
	}

	UFloat     m_diffuse;
	UUnitFloat m_backLight;
	UFloat10   m_emissive;
	UUnitFloat m_curvature;
	bool       m_receiveShadows;
	bool       m_affectedByFog;
	bool       m_environmentLighting;
};

CRY_PFX2_IMPLEMENT_FEATURE(CParticleFeature, CFeatureAppearanceLighting, "Appearance", "Lighting", colorAppearance);

SERIALIZATION_DECLARE_ENUM(EBlendMode,
                           Opaque = 0,
                           Alpha = OS_ALPHA_BLEND,
                           Additive = OS_ADD_BLEND,
                           Multiplicative = OS_MULTIPLY_BLEND
                           )

class CFeatureAppearanceBlending : public CParticleFeature
{
public:
	CRY_PFX2_DECLARE_FEATURE

	CFeatureAppearanceBlending()
		: m_blendMode(EBlendMode::Alpha)
		, CParticleFeature(gpu_pfx2::eGpuFeatureType_Dummy) {}

	virtual void AddToComponent(CParticleComponent* pComponent, SComponentParams* pParams) override
	{
		pParams->m_renderStateFlags = (pParams->m_renderStateFlags & ~OS_TRANSPARENT) | (int)m_blendMode;
	}

	virtual void Serialize(Serialization::IArchive& ar) override
	{
		CParticleFeature::Serialize(ar);
		ar(m_blendMode, "BlendMode", "Blend Mode");
	}

private:
	EBlendMode m_blendMode;
};

CRY_PFX2_IMPLEMENT_FEATURE(CParticleFeature, CFeatureAppearanceBlending, "Appearance", "Blending", colorAppearance);

class CFeatureAppearanceSoftIntersect : public CParticleFeature
{
public:
	CRY_PFX2_DECLARE_FEATURE

	CFeatureAppearanceSoftIntersect()
		: m_softNess(1.0f)
		, CParticleFeature(gpu_pfx2::eGpuFeatureType_Dummy) {}

	virtual void AddToComponent(CParticleComponent* pComponent, SComponentParams* pParams) override
	{
		if (m_softNess > 0.f)
		{
			pParams->m_renderObjectFlags |= FOB_SOFT_PARTICLE;
			pParams->m_shaderData.m_softnessMultiplier = m_softNess;
		}
	}

	virtual void Serialize(Serialization::IArchive& ar) override
	{
		CParticleFeature::Serialize(ar);
		ar(m_softNess, "Softness", "Softness");
	}

private:
	UFloat m_softNess;
};

CRY_PFX2_IMPLEMENT_FEATURE(CParticleFeature, CFeatureAppearanceSoftIntersect, "Appearance", "SoftIntersect", colorAppearance);

class CFeatureAppearanceVisibility : public CParticleFeature, SVisibilityParams
{
public:
	CRY_PFX2_DECLARE_FEATURE

	CFeatureAppearanceVisibility()
		: m_drawNear(false)
		, m_drawOnTop(false)
	{}

	virtual void AddToComponent(CParticleComponent* pComponent, SComponentParams* pParams) override
	{
		pParams->m_visibility.Combine(*this);
		if (m_drawNear)
			pParams->m_renderObjectFlags |= FOB_NEAREST;
		if (m_drawOnTop)
			pParams->m_renderStateFlags |= OS_NODEPTH_TEST;
	}

	virtual void Serialize(Serialization::IArchive& ar) override
	{
		CParticleFeature::Serialize(ar);
		SERIALIZE_VAR(ar, m_maxScreenSize);
		SERIALIZE_VAR(ar, m_minCameraDistance);
		SERIALIZE_VAR(ar, m_maxCameraDistance);
		SERIALIZE_VAR(ar, m_viewDistanceMultiple);
		SERIALIZE_VAR(ar, m_indoorVisibility);
		SERIALIZE_VAR(ar, m_waterVisibility);
		SERIALIZE_VAR(ar, m_drawNear);
		SERIALIZE_VAR(ar, m_drawOnTop);
	}

private:
	bool m_drawNear;
	bool m_drawOnTop;
};

CRY_PFX2_IMPLEMENT_FEATURE(CParticleFeature, CFeatureAppearanceVisibility, "Appearance", "Visibility", colorAppearance);

}
