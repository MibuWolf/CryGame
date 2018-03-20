// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

using System.Reflection;
using CryEngine.Resources;
using CryEngine.Components;
using CryEngine.FlowSystem;
using CryEngine.Common;
using CryEngine.EntitySystem;

namespace CryEngine.SampleApp
{
    /// <summary>
    /// Add-in entry point will be re-instantiated in runtime, whenever the assembly is updated (e.g. Re-compiled)
    /// </summary>
    public class Program : ICryEngineAddIn
	{
        Application app;

        public void Initialize(InterDomainHandler handler)
		{
			app = Application.Instantiate<SampleApp>();
		}

        public void StartGame()
        {
            
        }

        public void EndGame()
        {
            
        }

		/// <summary>
		/// Will be called if a RunScene FlowNode was triggered insode game.
		/// </summary>
		/// <param name="scene">A wrapper object containing the FlowNode's data.</param>
		public void OnFlowNodeSignal(FlowNode node, PropertyInfo signal)
		{
            
		}

		public void Shutdown()
		{
            app.Shutdown(false);
		}
	}
}
