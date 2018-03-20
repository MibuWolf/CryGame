﻿using System;
using CryEngine.Common;

namespace CryEngine
{
	public class DynamicsStatus : BasePhysicsStatus<pe_status_dynamics>
	{
		//TODO confirm if PartId and PartIndex are indeed PartId's and PartIndices.

		/// <summary>
		/// The ID of the part this status refers to.
		/// </summary>
		public int PartId { get; private set; }
		
		/// <summary>
		/// The index of the part this status refers to.
		/// </summary>
		public int PartIndex { get; private set; }
		
		/// <summary>
		/// The velocity of the PhysicsObject
		/// </summary>
		public Vector3 Velocity { get; private set; }

		/// <summary>
		/// The angular velocity of the PhysicsObject
		/// </summary>
		public Vector3 AngularVelocity { get; private set; }

		/// <summary>
		/// The linear acceleration of the PhysicsObject
		/// </summary>
		public Vector3 LinearAcceleration { get; private set; }

		/// <summary>
		/// The angular acceleration of the PhysicsObject
		/// </summary>
		public Vector3 AngularAcceleration { get; private set; }

		/// <summary>
		/// The center of mass of the PhysicsObject
		/// </summary>
		public Vector3 CenterOfMass { get; private set; }

		/// <summary>
		/// Percentage of the entity that is underwater; 0..1. not supported for individual parts
		/// </summary>
		public float SubmergedFraction { get; private set; }

		/// <summary>
		/// Entity's or part's mass
		/// </summary>
		public float Mass { get; private set; }

		/// <summary>
		/// Kinetic energy; only supported by PhysicalizationType.Articulated currently
		/// </summary>
		public float Energy { get; private set; }

		/// <summary>
		/// The amount of collision contacts the PhysicsObject has
		/// </summary>
		public int ContactsCount { get; private set; }

		internal override pe_status_dynamics ToNativeStatus()
		{
			return new pe_status_dynamics();
		}

		internal override void NativeToManaged<T>(T baseNative)
		{
			if(baseNative == null)
			{
				throw new ArgumentNullException(nameof(baseNative));
			}

			var native = baseNative as pe_status_dynamics;
			if(native == null)
			{
				Log.Error<DynamicsStatus>("Expected pe_status_dynamics but received {0}!", baseNative.GetType());
				return;
			}

			PartId = native.partid;
			PartIndex = native.ipart;
			Velocity = native.v;
			AngularVelocity = native.w;
			LinearAcceleration = native.a;
			AngularVelocity = native.wa;
			CenterOfMass = native.centerOfMass;
			SubmergedFraction = native.submergedFraction;
			Mass = native.mass;
			Energy = native.energy;
			ContactsCount = native.nContacts;
		}
	}
}
