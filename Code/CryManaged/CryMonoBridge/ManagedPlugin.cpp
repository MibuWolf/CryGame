// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "ManagedPlugin.h"

#include "MonoRuntime.h"
#include "Wrappers/AppDomain.h"
#include "Wrappers/MonoLibrary.h"
#include "Wrappers/MonoClass.h"
#include "Wrappers/MonoMethod.h"

CManagedPlugin::TComponentFactoryMap* CManagedPlugin::s_pCurrentlyRegisteringFactory = nullptr;

CManagedPlugin::CManagedPlugin(const char* szBinaryPath)
	: m_pLibrary(nullptr)
	, m_libraryPath(szBinaryPath)
{
	gEnv->pSystem->GetISystemEventDispatcher()->RegisterListener(this, "CManagedPlugin");
}

CManagedPlugin::CManagedPlugin(CMonoLibrary* pLibrary)
	: m_pLibrary(pLibrary)
	, m_libraryPath(pLibrary->GetFilePath())
{
	gEnv->pSystem->GetISystemEventDispatcher()->RegisterListener(this, "CManagedPlugin");
}

CManagedPlugin::~CManagedPlugin()
{
	gEnv->pGameFramework->RemoveNetworkedClientListener(*this);

	gEnv->pSystem->GetISystemEventDispatcher()->RemoveListener(this);

	if (m_pMonoObject != nullptr)
	{
		if (CMonoClass* pClass = m_pMonoObject->GetClass())
		{
			if (std::shared_ptr<CMonoMethod> pShutdownMethod = pClass->FindMethod("Shutdown"))
			{
				pShutdownMethod->Invoke(m_pMonoObject.get());
			}
		}
	}
}

void CManagedPlugin::InitializePlugin()
{
	std::shared_ptr<CMonoClass> pReflectionHelper = GetMonoRuntime()->GetCryCoreLibrary()->GetTemporaryClass("CryEngine", "ReflectionHelper");
	CRY_ASSERT(pReflectionHelper != nullptr);

	void* pPluginSearchArgs[1]{ m_pLibrary->GetManagedObject() };

	std::shared_ptr<CMonoObject> pPluginInstanceName = pReflectionHelper->FindMethodWithDesc("FindPluginInstance(Assembly)")->InvokeStatic(pPluginSearchArgs);
	if (pPluginInstanceName != nullptr)
	{
		std::shared_ptr<CMonoString> pPluginClassName = pPluginInstanceName->ToString();
		const char* szPluginClassName = pPluginClassName->GetString();

		m_name = PathUtil::GetExt(szPluginClassName);
		const string nameSpace = PathUtil::RemoveExtension(szPluginClassName);

		CMonoClass* pPluginClass = m_pLibrary->GetClass(nameSpace, m_name);
		if (pPluginClass != nullptr)
		{
			m_pMonoObject = std::static_pointer_cast<CMonoObject>(pPluginClass->CreateInstance());

			gEnv->pGameFramework->AddNetworkedClientListener(*this);
		}
	}

	m_guid = CryGUID::FromString(mono_image_get_guid(m_pLibrary->GetImage()));

	if (m_name.size() == 0)
	{
		m_name = PathUtil::GetFileName(m_pLibrary->GetFilePath());
	}

	CMonoLibrary* pCoreAssembly = static_cast<CMonoLibrary*>(GetMonoRuntime()->GetCryCoreLibrary());
	std::shared_ptr<CMonoClass> pEngineClass = pCoreAssembly->GetTemporaryClass("CryEngine", "Engine");

	s_pCurrentlyRegisteringFactory = &m_entityComponentFactoryMap;

	//Scan for types in the assembly
	void* pRegisterArgs[1] = { m_pLibrary->GetManagedObject() };
	pEngineClass->FindMethodWithDesc("ScanAssembly(Assembly)")->InvokeStatic(pRegisterArgs);

	s_pCurrentlyRegisteringFactory = nullptr;

	// Register any potential Schematyc types
	gEnv->pSchematyc->GetEnvRegistry().RegisterPackage(stl::make_unique<CSchematycPackage>(*this));

	if (m_pMonoObject != nullptr)
	{
		if (CMonoClass* pClass = m_pMonoObject->GetClass())
		{
			if (std::shared_ptr<CMonoMethod> pMethod = pClass->FindMethod("Initialize"))
			{
				pMethod->Invoke(m_pMonoObject.get());
			}
		}
	}

	// scans for console command attributes
	std::shared_ptr<CMonoClass> attributeManager = GetMonoRuntime()->GetCryCoreLibrary()->GetTemporaryClass("CryEngine.Attributes", "ConsoleCommandAttributeManager");
	CRY_ASSERT(attributeManager != nullptr);
	void* args[1];
	args[0] = m_pLibrary->GetManagedObject(); //load the plug-in assembly to scans for ConsoleCommandRegisterAttribute
	attributeManager->FindMethod("RegisterAttribute", 1)->Invoke(nullptr, args);

	//scans for console variable attributes
	std::shared_ptr<CMonoClass> consoleVariableAttributeManager = GetMonoRuntime()->GetCryCoreLibrary()->GetTemporaryClass("CryEngine.Attributes", "ConsoleVariableAttributeManager");
	CRY_ASSERT(consoleVariableAttributeManager != nullptr);
	void* args2[1];
	args2[0] = m_pLibrary->GetManagedObject(); //load the plug-in assembly to scans for ConsoleVariableAttribute
	consoleVariableAttributeManager->FindMethod("RegisterAttribute", 1)->Invoke(nullptr, args2);
}

void CManagedPlugin::Load(CAppDomain* pPluginDomain)
{
	if (m_pLibrary == nullptr)
	{
		m_pLibrary = pPluginDomain->LoadLibrary(m_libraryPath);
		if (m_pLibrary == nullptr)
		{
			gEnv->pLog->LogError("[Mono] could not initialize plug-in '%s'!", m_libraryPath.c_str());
			return;
		}
	}

	InitializePlugin();
}

void CManagedPlugin::RegisterSchematycPackageContents(Schematyc::IEnvRegistrar& registrar) const
{
	Schematyc::CEnvRegistrationScope scope = registrar.Scope(IEntity::GetEntityScopeGUID());
	{
		for (auto it = m_entityComponentFactoryMap.begin(); it != m_entityComponentFactoryMap.end(); ++it)
		{
			Schematyc::CEnvRegistrationScope componentScope = scope.Register(it->second);
			// Functions
		}
	}
}

void CManagedPlugin::OnSystemEvent(ESystemEvent event, UINT_PTR wparam, UINT_PTR lparam)
{
	switch (event)
	{
	case ESYSTEM_EVENT_LEVEL_LOAD_END:
	{
		if (m_pMonoObject != nullptr)
		{
			if (CMonoClass* pClass = m_pMonoObject->GetClass())
			{
				if (std::shared_ptr<CMonoMethod> pMethod = pClass->FindMethod("OnLevelLoaded"))
				{
					pMethod->Invoke(m_pMonoObject.get());
				}
			}
		}
	}
	break;
	case ESYSTEM_EVENT_EDITOR_GAME_MODE_CHANGED:
	{
		if (wparam == 1)
		{
			if (m_pMonoObject != nullptr)
			{
				if (CMonoClass* pClass = m_pMonoObject->GetClass())
				{
					if (std::shared_ptr<CMonoMethod> pMethod = pClass->FindMethod("OnGameStart"))
					{
						pMethod->Invoke(m_pMonoObject.get());
					}
				}
			}
		}
		else if (m_pMonoObject != nullptr)
		{
			if (CMonoClass* pClass = m_pMonoObject->GetClass())
			{
				if (std::shared_ptr<CMonoMethod> pMethod = pClass->FindMethod("OnGameStop"))
				{
					pMethod->Invoke(m_pMonoObject.get());
				}
			}
		}
	}
	break;
	case ESYSTEM_EVENT_LEVEL_GAMEPLAY_START:
	{
		if (m_pMonoObject != nullptr)
		{
			if (CMonoClass* pClass = m_pMonoObject->GetClass())
			{
				if (std::shared_ptr<CMonoMethod> pMethod = pClass->FindMethod("OnGameStart"))
				{
					pMethod->Invoke(m_pMonoObject.get());
				}
			}
		}
	}
	break;
	case ESYSTEM_EVENT_LEVEL_UNLOAD:
	{
		if (m_pMonoObject != nullptr)
		{
			if (CMonoClass* pClass = m_pMonoObject->GetClass())
			{
				if (std::shared_ptr<CMonoMethod> pMethod = pClass->FindMethod("OnGameStop"))
				{
					pMethod->Invoke(m_pMonoObject.get());
				}
			}
		}
	}
	break;
	}
}

bool CManagedPlugin::OnClientConnectionReceived(int channelId, bool bIsReset)
{
	if (m_pMonoObject != nullptr)
	{
		if (CMonoClass* pClass = m_pMonoObject->GetClass())
		{
			if (std::shared_ptr<CMonoMethod> pMethod = pClass->FindMethod("OnClientConnectionReceived"))
			{
				void* pParameters[1];
				pParameters[0] = &channelId;

				pMethod->Invoke(m_pMonoObject.get(), pParameters);
			}
		}
		
	}

	return true;
}

bool CManagedPlugin::OnClientReadyForGameplay(int channelId, bool bIsReset)
{
	if (m_pMonoObject != nullptr)
	{
		if (CMonoClass* pClass = m_pMonoObject->GetClass())
		{
			if (std::shared_ptr<CMonoMethod> pMethod = pClass->FindMethod("OnClientReadyForGameplay"))
			{
				void* pParameters[1];
				pParameters[0] = &channelId;

				pMethod->Invoke(m_pMonoObject.get(), pParameters);
			}
		}
	}

	return true;
}

void CManagedPlugin::OnClientDisconnected(int channelId, EDisconnectionCause cause, const char* description, bool bKeepClient)
{
	if (m_pMonoObject != nullptr)
	{
		if (CMonoClass* pClass = m_pMonoObject->GetClass())
		{
			if (std::shared_ptr<CMonoMethod> pMethod = pClass->FindMethod("OnClientDisconnected"))
			{
				void* pParameters[1];
				pParameters[0] = &channelId;
				pMethod->Invoke(m_pMonoObject.get(), pParameters);
			}
		}
	}
}
