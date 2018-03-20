#include "StdAfx.h"
#include "PointLightComponent.h"

#include <CrySystem/IProjectManager.h>
#include <CryGame/IGameFramework.h>
#include <ILevelSystem.h>
#include <Cry3DEngine/IRenderNode.h>

namespace Cry
{
	namespace DefaultComponents
	{
		void CPointLightComponent::Register(Schematyc::CEnvRegistrationScope& componentScope)
		{
			// Functions
			{
				auto pFunction = SCHEMATYC_MAKE_ENV_FUNCTION(&CPointLightComponent::Enable, "{415ECD3F-880C-4647-92CB-2A8F2321709D}"_cry_guid, "Enable");
				pFunction->SetDescription("Enables or disables the light component");
				pFunction->SetFlags({ Schematyc::EEnvFunctionFlags::Member, Schematyc::EEnvFunctionFlags::Construction });
				pFunction->BindInput(1, 'ena', "Enable");
				componentScope.Register(pFunction);
			}
		}

		void CPointLightComponent::Initialize()
		{
			if (!m_bActive)
			{
				FreeEntitySlot();

				return;
			}

			CDLight light;

			light.m_nLightStyle = m_animations.m_style;
			light.SetAnimSpeed(m_animations.m_speed);

			light.SetPosition(ZERO);
			light.m_Flags = DLF_DEFERRED_LIGHT | DLF_POINT;

			light.m_fRadius = m_radius;

			light.SetLightColor(m_color.m_color * m_color.m_diffuseMultiplier);
			light.SetSpecularMult(m_color.m_specularMultiplier);

			light.m_fHDRDynamic = 0.f;

			if (m_options.m_bAffectsOnlyThisArea)
				light.m_Flags |= DLF_THIS_AREA_ONLY;

			if (m_options.m_bIgnoreVisAreas)
				light.m_Flags |= DLF_IGNORES_VISAREAS;

			if (m_options.m_bVolumetricFogOnly)
				light.m_Flags |= DLF_VOLUMETRIC_FOG_ONLY;

			if (m_options.m_bAffectsVolumetricFog)
				light.m_Flags |= DLF_VOLUMETRIC_FOG;

			if (m_options.m_bAmbient)
				light.m_Flags |= DLF_AMBIENT;

			if (m_shadows.m_castShadowSpec != EMiniumSystemSpec::Disabled && (int)gEnv->pSystem->GetConfigSpec() >= (int)m_shadows.m_castShadowSpec)
			{
				light.m_Flags |= DLF_CASTSHADOW_MAPS;

				light.SetShadowBiasParams(1.f, 1.f);
				light.m_fShadowUpdateMinRadius = light.m_fRadius;

				float shadowUpdateRatio = 1.f;
				light.m_nShadowUpdateRatio = max((uint16)1, (uint16)(shadowUpdateRatio * (1 << DL_SHADOW_UPDATE_SHIFT)));
			}
			else
				light.m_Flags &= ~DLF_CASTSHADOW_MAPS;

			light.m_fAttenuationBulbSize = m_options.m_attenuationBulbSize;

			light.m_fFogRadialLobe = m_options.m_fogRadialLobe;

			// Load the light source into the entity
			m_pEntity->LoadLight(GetOrMakeEntitySlotId(), &light);

			uint32 slotFlags = m_pEntity->GetSlotFlags(GetEntitySlotId());
			UpdateGIModeEntitySlotFlags((uint8)m_options.m_giMode, slotFlags);
			m_pEntity->SetSlotFlags(GetEntitySlotId(), slotFlags);
		}

		void CPointLightComponent::ProcessEvent(SEntityEvent& event)
		{
			if (event.event == ENTITY_EVENT_COMPONENT_PROPERTY_CHANGED)
			{
				Initialize();
			}
		}

		uint64 CPointLightComponent::GetEventMask() const
		{
			return BIT64(ENTITY_EVENT_COMPONENT_PROPERTY_CHANGED);
		}

#ifndef RELEASE
		void CPointLightComponent::Render(const IEntity& entity, const IEntityComponent& component, SEntityPreviewContext &context) const
		{
			if (context.bSelected)
			{
				Matrix34 slotTransform = GetWorldTransformMatrix();

				Vec3 p0, p1;
				float step = 10.0f / 180 * gf_PI;
				float angle;

				Vec3 pos = slotTransform.GetTranslation();

				// Z Axis
				p0 = pos;
				p1 = pos;
				p0.x += m_radius * sin(0.0f);
				p0.y += m_radius * cos(0.0f);
				for (angle = step; angle < 360.0f / 180 * gf_PI + step; angle += step)
				{
					p1.x = pos.x + m_radius * sin(angle);
					p1.y = pos.y + m_radius * cos(angle);
					p1.z = pos.z;
					gEnv->pRenderer->GetIRenderAuxGeom()->DrawLine(p0, context.debugDrawInfo.color, p1, context.debugDrawInfo.color);
					p0 = p1;
				}

				// X Axis
				p0 = pos;
				p1 = pos;
				p0.y += m_radius * sin(0.0f);
				p0.z += m_radius * cos(0.0f);
				for (angle = step; angle < 360.0f / 180 * gf_PI + step; angle += step)
				{
					p1.x = pos.x;
					p1.y = pos.y + m_radius * sin(angle);
					p1.z = pos.z + m_radius * cos(angle);
					gEnv->pRenderer->GetIRenderAuxGeom()->DrawLine(p0, context.debugDrawInfo.color, p1, context.debugDrawInfo.color);
					p0 = p1;
				}

				// Y Axis
				p0 = pos;
				p1 = pos;
				p0.x += m_radius * sin(0.0f);
				p0.z += m_radius * cos(0.0f);
				for (angle = step; angle < 360.0f / 180 * gf_PI + step; angle += step)
				{
					p1.x = pos.x + m_radius * sin(angle);
					p1.y = pos.y;
					p1.z = pos.z + m_radius * cos(angle);
					gEnv->pRenderer->GetIRenderAuxGeom()->DrawLine(p0, context.debugDrawInfo.color, p1, context.debugDrawInfo.color);
					p0 = p1;
				}
			}
		}
#endif

		void CPointLightComponent::Enable(bool bEnable)
		{
			m_bActive = bEnable; 

			Initialize();
		}
	}
}