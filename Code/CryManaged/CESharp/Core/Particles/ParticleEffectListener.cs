// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

using System;
using CryEngine.Common;

namespace CryEngine
{
	// TODO Implement ParticleEffectListener
	public sealed class ParticleEffectListener
	{
		internal IParticleEffectListener NativeHandle { get; private set; }

		internal ParticleEffectListener(IParticleEffectListener nativeHandle)
		{
			NativeHandle = nativeHandle;
		}
	}
}
