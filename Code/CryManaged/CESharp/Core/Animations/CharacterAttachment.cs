﻿// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

using CryEngine.Common;

namespace CryEngine.Animations
{
	//Wraps the native IAttachment class
	
	public class CharacterAttachment
	{
		/// <summary>
		/// The position of the attachment in world-space.
		/// </summary>
		/// <value>The world position.</value>
		public Vector3 WorldPosition
		{
			get
			{
				var worldTransform = NativeHandle.GetAttWorldAbsolute();
				return worldTransform.t;
			}
		}

		/// <summary>
		/// The rotation of the attachment in world-space.
		/// </summary>
		/// <value>The world rotation.</value>
		public Quaternion WorldRotation
		{
			get
			{
				var worldTransform = NativeHandle.GetAttWorldAbsolute();
				return worldTransform.q;
			}
		}

		internal IAttachment NativeHandle{ get; private set; }

		internal CharacterAttachment(IAttachment nativeAttachment)
		{
			NativeHandle = nativeAttachment;
		}
	}
}
