#include "StdAfx.h"
#include "ParticleComponent.h"

#include <Cry3DEngine/IRenderNode.h>

namespace Cry
{
namespace DefaultComponents
{
void CParticleComponent::Register(Schematyc::CEnvRegistrationScope& componentScope)
{
	{
		auto pFunction = SCHEMATYC_MAKE_ENV_FUNCTION(&CParticleComponent::Activate, "{14F978C0-2C56-40C9-95FB-6967936DBFF9}"_cry_guid, "Activate");
		pFunction->SetDescription("Enables / disables particle emitter");
		pFunction->SetFlags(Schematyc::EEnvFunctionFlags::Construction);
		pFunction->BindInput(1, 'actv', "Active");
		componentScope.Register(pFunction);
	}
	{
		auto pFunction = SCHEMATYC_MAKE_ENV_FUNCTION(&CParticleComponent::IsActive, "{6B376186-B2AB-46FD-86C9-9ED772159590}"_cry_guid, "IsActive");
		pFunction->SetDescription("Is particle emitter enabled?");
		pFunction->SetFlags(Schematyc::EEnvFunctionFlags::Construction);
		pFunction->BindOutput(0, 'actv', "Active");
		componentScope.Register(pFunction);
	}
}

void CParticleComponent::Initialize()
{
	if (m_effectName.value.size() > 0)
	{
		LoadEffect(m_bEnabled);
	}
	else
	{
		FreeEntitySlot();
	}
}

void CParticleComponent::ProcessEvent(SEntityEvent& event)
{
	switch (event.event)
	{
	case ENTITY_EVENT_COMPONENT_PROPERTY_CHANGED:
	{
		LoadEffect(m_bEnabled);
	}
	break;
	}
}

uint64 CParticleComponent::GetEventMask() const
{
	return BIT64(ENTITY_EVENT_COMPONENT_PROPERTY_CHANGED);
}

void CParticleComponent::SetEffectName(const char* szPath)
{
	m_effectName = szPath;
}

bool Serialize(Serialization::IArchive& archive, CParticleComponent::SAttributes& value, const char* szName, const char* szLabel)
{
	return archive(*value.m_pAttributes, szName, szLabel);
}

bool Serialize(Serialization::IArchive& archive, CParticleComponent::SSpawnParameters& value, const char* szName, const char* szLabel)
{
	return archive(value.m_spawnParams, szName, szLabel);
}
}
}
