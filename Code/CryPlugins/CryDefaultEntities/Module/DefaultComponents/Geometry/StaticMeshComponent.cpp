#include "StdAfx.h"
#include "StaticMeshComponent.h"

#include <Cry3DEngine/IRenderNode.h>

namespace Cry
{
namespace DefaultComponents
{
void CStaticMeshComponent::Register(Schematyc::CEnvRegistrationScope& componentScope)
{
	// Functions
	{
		auto pFunction = SCHEMATYC_MAKE_ENV_FUNCTION(&CStaticMeshComponent::LoadFromDisk, "{E900651C-169C-4FF2-B987-CFC5613D51EB}"_cry_guid, "LoadFromDisk");
		pFunction->SetDescription("Load the mesh from disk");
		pFunction->SetFlags({ Schematyc::EEnvFunctionFlags::Member, Schematyc::EEnvFunctionFlags::Construction });
		componentScope.Register(pFunction);
	}
	{
		auto pFunction = SCHEMATYC_MAKE_ENV_FUNCTION(&CStaticMeshComponent::ResetObject, "{86AA9C32-3D54-4622-8032-C99C194FF24A}"_cry_guid, "ResetObject");
		pFunction->SetDescription("Resets the object");
		pFunction->SetFlags({ Schematyc::EEnvFunctionFlags::Member, Schematyc::EEnvFunctionFlags::Construction });
		componentScope.Register(pFunction);
	}
	{
		auto pFunction = SCHEMATYC_MAKE_ENV_FUNCTION(&CStaticMeshComponent::SetMeshType, "{6C4F6C80-2F84-4C9B-ADBF-5C131EFDD98A}"_cry_guid, "SetType");
		pFunction->BindInput(1, 'type', "Type");
		pFunction->SetDescription("Changes the type of the object");
		pFunction->SetFlags({ Schematyc::EEnvFunctionFlags::Member });
		componentScope.Register(pFunction);
	}
}

void CStaticMeshComponent::Initialize()
{
	LoadFromDisk();
	ResetObject();
}

void CStaticMeshComponent::LoadFromDisk()
{
	if (m_filePath.value.size() > 0)
	{
		m_pCachedStatObj = gEnv->p3DEngine->LoadStatObj(m_filePath.value);
	}
	else
	{
		m_pCachedStatObj = nullptr;
	}
}

void CStaticMeshComponent::SetObject(IStatObj* pObject, bool bSetDefaultMass)
{
	m_pCachedStatObj = pObject;
	m_filePath.value = m_pCachedStatObj->GetFilePath();

	if (bSetDefaultMass)
	{
		if (!m_pCachedStatObj->GetPhysicalProperties(m_physics.m_mass, m_physics.m_density))
		{
			m_physics.m_mass = 10;
		}
	}
}

void CStaticMeshComponent::ResetObject()
{
	if (m_pCachedStatObj != nullptr)
	{
		m_pEntity->SetStatObj(m_pCachedStatObj, GetOrMakeEntitySlotId() | ENTITY_SLOT_ACTUAL, false);
		if (!m_materialPath.value.empty())
		{
			if (IMaterial* pMaterial = gEnv->p3DEngine->GetMaterialManager()->LoadMaterial(m_materialPath.value, false))
			{
				m_pEntity->SetSlotMaterial(GetEntitySlotId(), pMaterial);
			}
		}
	}
	else
	{
		FreeEntitySlot();
	}
}

void CStaticMeshComponent::ProcessEvent(SEntityEvent& event)
{
	if (event.event == ENTITY_EVENT_COMPONENT_PROPERTY_CHANGED)
	{
		LoadFromDisk();
		ResetObject();

		// Update Editor UI to show the default object material
		if (m_materialPath.value.empty() && m_pCachedStatObj != nullptr)
		{
			if (IMaterial* pMaterial = m_pCachedStatObj->GetMaterial())
			{
				m_materialPath = pMaterial->GetName();
			}
		}
	}

	CBaseMeshComponent::ProcessEvent(event);
}

void CStaticMeshComponent::SetFilePath(const char* szPath)
{
	m_filePath.value = szPath;
}
}} // namespace Cry::DefaultComponents
