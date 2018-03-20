#pragma once

#include "PhysicsPrimitiveComponent.h"

class CPlugin_CryDefaultEntities;

namespace Cry
{
	namespace DefaultComponents
	{
		class CSpherePrimitiveComponent
			: public CPhysicsPrimitiveComponent
		{
		protected:
			friend CPlugin_CryDefaultEntities;
			static void Register(Schematyc::CEnvRegistrationScope& componentScope);

		public:
			static void ReflectType(Schematyc::CTypeDesc<CSpherePrimitiveComponent>& desc)
			{
				desc.SetGUID("{106FACAD-4099-4993-A9CC-F558A8586307}"_cry_guid);
				desc.SetEditorCategory("Physics");
				desc.SetLabel("Sphere Collider");
				desc.SetDescription("Adds a sphere primitive to the physical entity, requires a Simple Physics or Character Controller component.");
				desc.SetIcon("icons:ObjectTypes/object.ico");
				desc.SetComponentFlags({ IEntityComponent::EFlags::Transform, IEntityComponent::EFlags::Socket, IEntityComponent::EFlags::Attach });

				desc.AddMember(&CSpherePrimitiveComponent::m_radius, 'radi', "Radius", "Radius", "Radius of the sphere", 1.f);

				desc.AddMember(&CSpherePrimitiveComponent::m_mass, 'mass', "Mass", "Mass", "Mass of the object in kg, note that this cannot be set at the same time as density. Both being set to 0 means no physics.", 10.f);
				desc.AddMember(&CSpherePrimitiveComponent::m_density, 'dens', "Density", "Density", "Density of the object, note that this cannot be set at the same time as mass. Both being set to 0 means no physics.", 0.f);

				desc.AddMember(&CSpherePrimitiveComponent::m_surfaceTypeName, 'surf', "Surface", "Surface Type", "Surface type assigned to this object, determines its physical properties", "");
				desc.AddMember(&CSpherePrimitiveComponent::m_bReactToCollisions, 'coll', "ReactToCollisions", "React to Collisions", "Whether the part will react to collisions, or only report them", true);
			}

			CSpherePrimitiveComponent() {}
			virtual ~CSpherePrimitiveComponent() {}

			virtual IGeometry* CreateGeometry() const final
			{
				primitives::sphere primitive;
				primitive.center = ZERO;
				primitive.r = m_radius;

				return gEnv->pPhysicalWorld->GetGeomManager()->CreatePrimitive((int)primitive.type, &primitive);
			}

			Schematyc::Range<0, 100000> m_radius = 1.f;
		};
	}
}