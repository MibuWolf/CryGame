// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"

// *INDENT-OFF* - <hard to read code and declarations due to inconsistent indentation>

// - needs to be included once per DLL module
// - otherwise, you'd get linker errors regarding CryAssert, memory manager, etc.
// - not required when building a .lib
#include <CryCore/Platform/platform_impl.inl>

namespace UQS
{
	namespace Core
	{

		//===================================================================================
		//
		// CHubPlugin
		//
		//===================================================================================

		class CHubPlugin : public IHubPlugin, public ISystemEventListener
		{
			CRYINTERFACE_BEGIN()
			CRYINTERFACE_ADD(IHubPlugin)
			CRYINTERFACE_ADD(ICryPlugin)
			CRYINTERFACE_END()

			CRYGENERATE_SINGLETONCLASS_GUID(CHubPlugin, "Plugin_UQS", "2a2f00e0-f068-4baf-b31b-b3c8f78b3477"_cry_guid)

			CHubPlugin();
			virtual ~CHubPlugin();

		private:
			// ICryPlugin (forwarded by IHubPlugin)
			virtual const char*                  GetName() const override;
			virtual const char*                  GetCategory() const override;
			virtual bool                         Initialize(SSystemGlobalEnvironment& env, const SSystemInitParams& initParams) override;
			virtual void                         OnPluginUpdate(EPluginUpdateType updateType) override;
			// ~ICryPlugin

			// IHubPlugin
			virtual void                         RegisterHubPluginEventListener(IHubPluginEventListener* pListenerToRegister) override;
			virtual void                         UnregisterHubPluginEventListener(IHubPluginEventListener* pListenerToUnregister) override;
			virtual IHub&                        GetHubImplementation() override;
			virtual void                         TearDownHubImplementation() override;
			// ~IHubPlugin

			// ISystemEventListener
			virtual void                         OnSystemEvent(ESystemEvent event, UINT_PTR wparam, UINT_PTR lparam) override;
			// ~ISystemEventListener

		private:
			std::unique_ptr<CHub>                m_pHub;
			std::list<IHubPluginEventListener*>  m_hubPluginEventListeners;
		};

		CRYREGISTER_SINGLETON_CLASS(CHubPlugin)

		// the ctor and dtor were declared by some code-weaver macros (specifically by _ENFORCE_CRYFACTORY_USAGE() which is used by CRYGENERATE_SINGLETONCLASS())

		CHubPlugin::CHubPlugin()
		{
			m_updateFlags = 0;  // the ctor of ICryPlugin base class should have done that, but didn't
			GetISystem()->GetISystemEventDispatcher()->RegisterListener(this,"CHubPlugin");
		}

		CHubPlugin::~CHubPlugin()
		{
			GetISystem()->GetISystemEventDispatcher()->RemoveListener(this);
		}

		const char* CHubPlugin::GetName() const
		{
			return "UQS";
		}

		const char* CHubPlugin::GetCategory() const
		{
			return "Plugin";
		}

		bool CHubPlugin::Initialize(SSystemGlobalEnvironment& env, const SSystemInitParams& initParams)
		{
			// Notice: we currently do *not* yet instantiate the UQS Hub here, because some of its sub-systems rely on the presence of sub-systems of ISystem, like IInput, which
			// are still NULL at this point in time. So instead, we instantiate the UQS Hub upon ESYSTEM_EVENT_CRYSYSTEM_INIT_DONE (see OnSystemEvent()).
			return true;
		}

		void CHubPlugin::OnPluginUpdate(EPluginUpdateType updateType)
		{
			// nothing (the game or editor shall call IHub::Update() directly whenever it wants queries to get processed)
		}

		void CHubPlugin::RegisterHubPluginEventListener(IHubPluginEventListener* pListenerToRegister)
		{
			stl::push_back_unique(m_hubPluginEventListeners, pListenerToRegister);
		}

		void CHubPlugin::UnregisterHubPluginEventListener(IHubPluginEventListener* pListenerToUnregister)
		{
			m_hubPluginEventListeners.remove(pListenerToUnregister);
		}

		IHub& CHubPlugin::GetHubImplementation()
		{
			// if this assert fails, then some client code tried to access the one and only UQS Hub instance before ESYSTEM_EVENT_CRYSYSTEM_INIT_DONE (where it gets instantiated).
			assert(m_pHub.get());
			return *m_pHub.get();
		}

		void CHubPlugin::TearDownHubImplementation()
		{
			if (m_pHub.get())
			{
				//
				// notify all listeners
				// (notice that since we're using a std::list, the currently being called listener is even allowed to unregister himself
				//  during the callback without affecting the remaining iterators)
				//

				for (auto it = m_hubPluginEventListeners.begin(); it != m_hubPluginEventListeners.end(); )
				{
					IHubPluginEventListener* pListener = *it++;
					pListener->OnHubPluginEvent(IHubPluginEventListener::HubIsAboutToGetTornDown);
				}

				//
				// prematurely cancel all running queries
				//

				m_pHub->GetQueryManager().CancelAllRunningQueriesDueToUpcomingTearDownOfHub();

				//
				// destroy the one and only Hub instance
				//

				m_pHub.reset();
			}
		}

		void CHubPlugin::OnSystemEvent(ESystemEvent event, UINT_PTR wparam, UINT_PTR lparam)
		{
			switch (event)
			{
			case ESYSTEM_EVENT_CRYSYSTEM_INIT_DONE:
				assert(!m_pHub.get());
				m_pHub.reset(new CHub);
				break;
			}
		}

	}
}
