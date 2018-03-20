// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

namespace CryEngine.Game
{
	/// <summary>
	/// Add-in entry point will be re-instantiated in runtime, whenever the assembly is updated (e.g. Re-compiled)
	/// </summary>
	public class MyPlugin : ICryEnginePlugin
	{
		public override void Initialize()
		{
			base.Initialize();
			Game.Initialize();
		}

		public override void Shutdown()
		{
			base.Shutdown();
			Game.Shutdown();
		}
	}
}
