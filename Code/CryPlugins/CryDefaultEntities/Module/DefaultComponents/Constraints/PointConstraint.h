#pragma once

#include <CrySchematyc/MathTypes.h>
#include <CrySchematyc/Reflection/TypeDesc.h>
#include <CrySchematyc/Env/IEnvRegistrar.h>

class CPlugin_CryDefaultEntities;

namespace Cry
{
	namespace DefaultComponents
	{
		class CPointConstraintComponent
			: public IEntityComponent
		{
		protected:
			friend CPlugin_CryDefaultEntities;
			static void Register(Schematyc::CEnvRegistrationScope& componentScope);

			// IEntityComponent
			virtual void Initialize() final;

			virtual void ProcessEvent(SEntityEvent& event) final;
			virtual uint64 GetEventMask() const final;
			// ~IEntityComponent

		public:
			virtual ~CPointConstraintComponent();

			static void ReflectType(Schematyc::CTypeDesc<CPointConstraintComponent>& desc)
			{
				desc.SetGUID("{4A010113-38A2-4CB0-8C98-BC1956675F40}"_cry_guid);
				desc.SetEditorCategory("Physics Constraints");
				desc.SetLabel("Point Constraint");
				desc.SetDescription("Constrains the physical object to a point");
				//desc.SetIcon("icons:ObjectTypes/object.ico");
				desc.SetComponentFlags({ IEntityComponent::EFlags::Transform, IEntityComponent::EFlags::Socket, IEntityComponent::EFlags::Attach });

				desc.AddMember(&CPointConstraintComponent::m_bActive, 'actv', "Active", "Active", "Whether or not the constraint should be added on component reset", true);
				desc.AddMember(&CPointConstraintComponent::m_axis, 'axis', "Axis", "Axis", "Axis around which the physical entity is constrained", Vec3(0.f, 0.f, 1.f));

				desc.AddMember(&CPointConstraintComponent::m_rotationLimitsX0, 'rlx0', "RotationLimitsX0", "Minimum X Angle", nullptr, 0.0_degrees);
				desc.AddMember(&CPointConstraintComponent::m_rotationLimitsX1, 'rlx1', "RotationLimitsX1", "Maximum X Angle", nullptr, 360.0_degrees);
				desc.AddMember(&CPointConstraintComponent::m_rotationLimitsYZ0, 'rly0', "RotationLimitsYZ0", "Minimum YZ Angle", nullptr, 0.0_degrees);
				desc.AddMember(&CPointConstraintComponent::m_rotationLimitsYZ1, 'rly1', "RotationLimitsYZ1", "Maximum YZ Angle", nullptr, 360.0_degrees);

				desc.AddMember(&CPointConstraintComponent::m_damping, 'damp', "Damping", "Damping", nullptr, 0.f);
			}

			virtual void ConstrainToEntity(Schematyc::ExplicitEntityId targetEntityId, bool bDisableCollisionsWith)
			{
				if ((EntityId)targetEntityId != INVALID_ENTITYID)
				{
					if (IEntity* pTargetEntity = gEnv->pEntitySystem->GetEntity((EntityId)targetEntityId))
					{
						if (IPhysicalEntity* pPhysicalEntity = pTargetEntity->GetPhysicalEntity())
						{
							ConstrainTo(pPhysicalEntity, bDisableCollisionsWith);
						}
					}
				}
			}

			virtual void ConstrainToPoint()
			{
				ConstrainTo(WORLD_ENTITY);
			}

			virtual void ConstrainTo(IPhysicalEntity* pOtherEntity, bool bDisableCollisionsWith = false)
			{
				if (m_constraintIds.size() > 0)
				{
					Remove();
				}

				if (IPhysicalEntity* pConstraintOwner = m_pEntity->GetPhysicalEntity())
				{
					// Constraints can only be added to rigid-based entities
					if (pConstraintOwner->GetType() != PE_RIGID && pConstraintOwner->GetType() != PE_WHEELEDVEHICLE)
					{
						if (pOtherEntity == WORLD_ENTITY)
						{
							CryWarning(VALIDATOR_MODULE_GAME, VALIDATOR_WARNING, "Tried to add point constraint to non-rigid or vehicle entity!");
							return;
						}

						// Swap the pointers around
						IPhysicalEntity* pTemp = pConstraintOwner;
						pConstraintOwner = pOtherEntity;
						pOtherEntity = pTemp;

#ifndef RELEASE
						// Validate the same check again
						if (pConstraintOwner->GetType() != PE_RIGID && pConstraintOwner->GetType() != PE_WHEELEDVEHICLE)
						{
							CryWarning(VALIDATOR_MODULE_GAME, VALIDATOR_WARNING, "Tried to add point constraint to non-rigid or vehicle entities!");
							return;
						}
#endif
					}

					Matrix34 slotTransform = GetWorldTransformMatrix();

					pe_action_add_constraint constraint;
					constraint.flags = world_frames | constraint_no_tears;
					constraint.pt[0] = constraint.pt[1] = slotTransform.GetTranslation();
					constraint.qframe[0] = constraint.qframe[1] = Quat(slotTransform) * Quat::CreateRotationV0V1(Vec3(1, 0, 0), m_axis);
					constraint.xlimits[0] = m_rotationLimitsX0.ToRadians();
					constraint.xlimits[1] = m_rotationLimitsX1.ToRadians();
					constraint.yzlimits[0] = m_rotationLimitsYZ0.ToRadians();
					constraint.yzlimits[1] = m_rotationLimitsYZ1.ToRadians();
					constraint.damping = m_damping;

					if (bDisableCollisionsWith && pOtherEntity != WORLD_ENTITY && pOtherEntity != pConstraintOwner)
					{
						constraint.flags |= constraint_ignore_buddy | constraint_inactive;

						constraint.pBuddy = pConstraintOwner;
						pOtherEntity->Action(&constraint);
						m_constraintIds.emplace_back(gEnv->pPhysicalWorld->GetPhysicalEntityId(pOtherEntity), pConstraintOwner->Action(&constraint));

						constraint.flags &= ~constraint_inactive;
					}

					constraint.pBuddy = pOtherEntity;

					int ownerId = gEnv->pPhysicalWorld->GetPhysicalEntityId(pConstraintOwner);
					m_constraintIds.emplace_back(ownerId, pConstraintOwner->Action(&constraint));
				}
			}

			virtual void Remove()
			{
				for (std::pair<int, int> constraintIdPair : m_constraintIds)
				{
					if (IPhysicalEntity* pPhysicalEntity = gEnv->pPhysicalWorld->GetPhysicalEntityById(constraintIdPair.first))
					{
						pe_action_update_constraint constraint;
						constraint.idConstraint = constraintIdPair.second;
						constraint.bRemove = 1;
						pPhysicalEntity->Action(&constraint);
					}
				}

				m_constraintIds.clear();
			}

			virtual void Activate(bool bActivate)
			{
				m_bActive = bActivate;

				m_pEntity->UpdateComponentEventMask(this);
			}
			bool IsActive() const { return m_bActive; }

			virtual void SetAxis(Vec3& axis) { m_axis = axis; }
			const Vec3& GetAxis() const { return m_axis; }

			virtual void SetRotationLimitsX(CryTransform::CAngle minValue, CryTransform::CAngle maxValue) { m_rotationLimitsX0 = minValue; m_rotationLimitsX1 = maxValue; }
			CryTransform::CAngle GetRotationLimitXMin() const { return m_rotationLimitsX0; }
			CryTransform::CAngle GetRotationLimitXMax() const { return m_rotationLimitsX1; }

			virtual void SetRotationLimitsyz(CryTransform::CAngle minValue, CryTransform::CAngle maxValue) { m_rotationLimitsYZ0 = minValue; m_rotationLimitsYZ1 = maxValue; }
			CryTransform::CAngle GetRotationLimitYZMin() const { return m_rotationLimitsYZ0; }
			CryTransform::CAngle GetRotationLimitYZMax() const { return m_rotationLimitsYZ1; }

			virtual void SetDamping(float damping) { m_damping = damping; }
			float GetDamping() const { return m_damping; }

		protected:
			void Reset();

		protected:
			bool m_bActive = false;

			Schematyc::UnitLength<Vec3> m_axis = Vec3(0, 0, 1);

			CryTransform::CClampedAngle<-360, 360> m_rotationLimitsX0 = 0.0_degrees;
			CryTransform::CClampedAngle<-360, 360> m_rotationLimitsX1 = 360.0_degrees;
			CryTransform::CClampedAngle<-360, 360> m_rotationLimitsYZ0 = 0.0_degrees;
			CryTransform::CClampedAngle<-360, 360> m_rotationLimitsYZ1 = 360.0_degrees;

			Schematyc::Range<-10000, 10000> m_damping = 0.f;

			std::vector<std::pair<int, int>> m_constraintIds;
		};

	}
}