// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "StdAfx.h"
#include <CrySerialization/Decorators/Resources.h>
#include <CrySerialization/Math.h>
#include "ParticleSystem/ParticleFeature.h"

CRY_PFX2_DBG

namespace pfx2
{

EParticleDataType PDT(EPDT_MeshGeometry, IMeshObj*);

extern EParticleDataType EPDT_Alpha, EPDT_Color;


SERIALIZATION_ENUM_DEFINE(ESizeMode, : uint8,
                          Size,
                          Scale
                          )

SERIALIZATION_ENUM_DEFINE(EOriginMode, : uint8,
                          Origin,
                          Center
                          )

SERIALIZATION_ENUM_DEFINE(EPiecesMode, : uint8,
                          Whole,
                          RandomPiece,
                          AllPieces
                          )

SERIALIZATION_ENUM_DEFINE(EPiecePlacement, : uint8,
                          Standard,
                          SubPlacement,
                          CenteredSubPlacement
                          )

class CFeatureRenderMeshes : public CParticleFeature, public Cry3DEngineBase
{
public:
	CRY_PFX2_DECLARE_FEATURE

	CFeatureRenderMeshes()
		: m_scale(1.0f, 1.0f, 1.0f)
		, m_sizeMode(ESizeMode::Scale)
		, m_originMode(EOriginMode::Origin)
		, m_piecesMode(EPiecesMode::RandomPiece)
		, m_piecePlacement(EPiecePlacement::Standard)
	{}

	virtual void Serialize(Serialization::IArchive& ar) override
	{
		CParticleFeature::Serialize(ar);
		ar(Serialization::ModelFilename(m_meshName), "Mesh", "Mesh");
		ar(m_scale, "Scale", "Scale");
		ar(m_sizeMode, "SizeMode", "Size Mode");
		ar(m_originMode, "OriginMode", "Origin Mode");
		ar(m_piecesMode, "PiecesMode", "Pieces Mode");
		ar(m_piecePlacement, "PiecePlacement", "Piece Placement");
	}

	virtual EFeatureType GetFeatureType() override { return EFT_Render; }

	virtual void         AddToComponent(CParticleComponent* pComponent, SComponentParams* pParams) override
	{
		pParams->m_pMesh = m_pStaticObject = Get3DEngine()->LoadStatObj(m_meshName.c_str(), NULL, NULL, m_piecesMode == EPiecesMode::Whole);
		pParams->m_meshCentered = m_originMode == EOriginMode::Center;
		if (m_pStaticObject)
		{
			pComponent->AddToUpdateList(EUL_RenderDeferred, this);
			pComponent->AddParticleData(EPVF_Position);
			pComponent->AddParticleData(EPQF_Orientation);

			m_aSubObjects.clear();
			if (m_piecesMode != EPiecesMode::Whole)
			{
				int subObjectCount = m_pStaticObject->GetSubObjectCount();
				for (int i = 0; i < subObjectCount; ++i)
				{
					if (IStatObj::SSubObject* pSub = m_pStaticObject->GetSubObject(i))
						if (pSub->nType == STATIC_SUB_OBJECT_MESH && pSub->pStatObj && pSub->pStatObj->GetRenderMesh())
						{
							if (string(pSub->name).Right(5) == "_main")
								continue;
							m_aSubObjects.push_back(pSub);
						}
				}

				if (m_aSubObjects.size() > 0)
				{
					// Require per-particle sub-objects
					assert(m_aSubObjects.size() < 256);
					pComponent->AddToUpdateList(EUL_InitUpdate, this);
					pComponent->AddParticleData(EPDT_MeshGeometry);
					if (m_piecesMode == EPiecesMode::AllPieces)
					{
						pComponent->AddParticleData(EPDT_SpawnId);
						pParams->m_scaleParticleCount *= m_aSubObjects.size();
					}
				}
			}
		}
	}

	virtual void InitParticles(const SUpdateContext& context) override
	{
		CRY_PROFILE_FUNCTION(PROFILE_PARTICLE);

		CParticleContainer& container = context.m_container;
		TIOStream<IMeshObj*> meshes = container.GetTIOStream<IMeshObj*>(EPDT_MeshGeometry);
		TIStream<uint> spawnIds = container.GetTIStream<uint>(EPDT_SpawnId);
		IOVec3Stream positions = container.GetIOVec3Stream(EPVF_Position);
		IOQuatStream orientations = container.GetIOQuatStream(EPQF_Orientation);
		IFStream sizes = container.GetIFStream(EPDT_Size, 1.0f);
		uint pieceCount = m_aSubObjects.size();
		Vec3 center = m_pStaticObject->GetAABB().GetCenter();

		CRY_PFX2_FOR_SPAWNED_PARTICLES(context)
		{
			uint piece;
			if (m_piecesMode == EPiecesMode::RandomPiece)
			{
				piece = context.m_spawnRng.Rand();
			}
			else if (m_piecesMode == EPiecesMode::AllPieces)
			{
				piece = spawnIds.Load(particleId);
			}
			piece %= pieceCount;

			IMeshObj* mesh = m_aSubObjects[piece]->pStatObj;
			meshes.Store(particleId, mesh);

			if (m_piecePlacement != EPiecePlacement::Standard)
			{
				Vec3 position = positions.Load(particleId);
				Quat orientation = orientations.Load(particleId);
				const float size = sizes.Load(particleId);

				if (m_piecePlacement == EPiecePlacement::CenteredSubPlacement)
				{
					// Offset by main object center
					position -= orientation * center * size;
				}

				// Place pieces according to sub-transforms; scale is ignored
				Matrix34 const& localTM = m_aSubObjects[piece]->localTM;

				position += orientation * localTM.GetTranslation() * size;
				orientation = orientation * Quat(localTM);

				if (context.m_params.m_meshCentered)
				{
					Vec3 subCenter = m_aSubObjects[piece]->pStatObj->GetAABB().GetCenter();
					position += orientation * subCenter * size;
				}

				positions.Store(particleId, position);
				orientations.Store(particleId, orientation);
			}
		}
		CRY_PFX2_FOR_END;
	}

	virtual void Render(CParticleEmitter* pEmitter, ICommonParticleComponentRuntime* pCommonComponentRuntime, CParticleComponent* pComponent, const SRenderContext& renderContext) override
	{
		CRY_PROFILE_FUNCTION(PROFILE_PARTICLE);

		CParticleComponentRuntime* pComponentRuntime = pCommonComponentRuntime->GetCpuRuntime();
		if (!pComponentRuntime)
			return;
		auto context = SUpdateContext(pComponentRuntime);
		auto& passInfo = renderContext.m_passInfo;
		SRendParams renderParams = renderContext.m_renderParams;

		const CParticleContainer& container = context.m_container;
		const IVec3Stream positions = container.GetIVec3Stream(EPVF_Position);
		const IQuatStream orientations = container.GetIQuatStream(EPQF_Orientation);
		const IFStream alphas = container.GetIFStream(EPDT_Alpha, 1.0f);
		const IFStream sizes = container.GetIFStream(EPDT_Size, 1.0f);
		const TIStream<IMeshObj*> meshes = container.GetTIStream<IMeshObj*>(EPDT_MeshGeometry);
		const Vec3 camPosition = passInfo.GetCamera().GetPosition();
		const bool hasAlphas = container.HasData(EPDT_Alpha);
		const bool hasPieces = container.HasData(EPDT_MeshGeometry);

		IMeshObj* pMeshObj = m_pStaticObject;

		const AABB bBox = m_pStaticObject->GetAABB();
		const float invObjectSize = (m_sizeMode == ESizeMode::Size) ? rsqrt_fast(bBox.GetRadiusSqr()) : 1.0f;
		Vec3 offset = -bBox.GetCenter();

		renderParams.dwFObjFlags |= FOB_TRANS_MASK;

		CRY_PFX2_FOR_ACTIVE_PARTICLES(context)
		{
			const Vec3 position = positions.Load(particleId);
			const Quat orientation = orientations.Load(particleId);
			const float size = sizes.Load(particleId);
			const Vec3 scale = m_scale * size * invObjectSize;

			Matrix34 wsMatrix(scale, orientation, position);

			if (hasPieces)
			{
				pMeshObj = meshes.Load(particleId);
				if (!pMeshObj)
					continue;
				offset = -pMeshObj->GetAABB().GetCenter();
			}

			if (context.m_params.m_meshCentered)
				wsMatrix.SetTranslation(wsMatrix * offset);

			renderParams.fAlpha = alphas.SafeLoad(particleId);

			// Trigger transparency rendering if any particles can be transparent.
			if (!passInfo.IsShadowPass() && hasAlphas)
				renderParams.fAlpha *= 0.999f;

			renderParams.fDistance = camPosition.GetDistance(position);
			renderParams.pMatrix = &wsMatrix;
			renderParams.pInstance = &non_const(*this);
			pMeshObj->Render(renderParams, passInfo);
		}
		CRY_PFX2_FOR_END;
	}

	virtual uint GetNumResources() const override
	{
		return m_meshName.empty() ? 0 : 1;
	}

	virtual const char* GetResourceName(uint resourceId) const override
	{
		return m_meshName.c_str();
	}

private:
	string                             m_meshName;
	Vec3                               m_scale;
	ESizeMode                          m_sizeMode;
	EOriginMode                        m_originMode;
	EPiecesMode                        m_piecesMode;
	EPiecePlacement                    m_piecePlacement;

	_smart_ptr<IStatObj>               m_pStaticObject;
	std::vector<IStatObj::SSubObject*> m_aSubObjects;
};

CRY_PFX2_IMPLEMENT_FEATURE(CParticleFeature, CFeatureRenderMeshes, "Render", "Meshes", colorRender);

}
