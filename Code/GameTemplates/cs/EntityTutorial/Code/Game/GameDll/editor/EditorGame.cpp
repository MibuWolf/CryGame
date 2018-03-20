// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.


#include "StdAfx.h"
#include "EditorGame.h"
#include "startup/GameStartup.h"
#include "IGameRulesSystem.h"
#include "ILevelSystem.h"


extern "C"
{
	GAME_API IGameStartup* CreateGameStartup();
};


CEditorGame::CEditorGame()
	: m_pGame(nullptr)
	, m_pGameStartup(nullptr)
	, m_bEnabled(false)
	, m_bGameMode(false)
	, m_bPlayer(false)
	, m_bSaturateScreen(false)
{
}

CEditorGame::~CEditorGame()
{
}

bool CEditorGame::Init(ISystem* pSystem, IGameToEditorInterface* pGameToEditorInterface)
{
	SSystemInitParams startupParams;
	startupParams.bEditor = true;
	startupParams.pSystem = pSystem;
	startupParams.bExecuteCommandLine = false;

	m_pGameStartup = CreateGameStartup();
	m_pGame = m_pGameStartup->Init(startupParams);
	m_pGame->GetIGameFramework()->InitEditor(pGameToEditorInterface);

	gEnv->bServer = true;
	gEnv->bMultiplayer = false;

#if CRY_PLATFORM_DESKTOP
	gEnv->SetIsClient(true);
#endif

	SetGameMode(false);
	ConfigureNetContext(true);

	return true;
}

int CEditorGame::Update(bool haveFocus, unsigned int updateFlags)
{
	if (m_bSaturateScreen)
	{
		SaturateScreen();
	}

	return m_pGameStartup->Update(haveFocus, updateFlags);
}

void CEditorGame::SaturateScreen()
{
	static float currentSaturation = 0.0f;
	currentSaturation += gEnv->pTimer->GetFrameTime() * 10.0f;
	
	if (currentSaturation > g_PI2)
	{
		currentSaturation = 0.0f;
		m_bSaturateScreen = false;
	}

	gEnv->p3DEngine->SetPostEffectParam("Global_User_Saturation", (cosf(currentSaturation) + 1.0f) * 0.5f, true);
}

void CEditorGame::Shutdown()
{
	EnablePlayer(false);
	SetGameMode(false);
	m_pGameStartup->Shutdown();
}

void CEditorGame::EnablePlayer(bool bPlayer)
{
	bool spawnPlayer = false;
	if (m_bPlayer != bPlayer)
	{
		spawnPlayer = m_bPlayer = bPlayer;
	}
	if (!SetGameMode(m_bGameMode))
	{
		gEnv->pLog->LogWarning("Failed setting game mode");
	}
	else if (m_bEnabled && spawnPlayer)
	{
		if (!m_pGame->GetIGameFramework()->BlockingSpawnPlayer())
		{
			gEnv->pLog->LogWarning("Failed spawning player");
		}
	}
}

bool CEditorGame::SetGameMode(bool bGameMode)
{
	m_bGameMode = bGameMode;
	bool ok = ConfigureNetContext(m_bPlayer);

	if (ok)
	{
		if (gEnv->IsEditor())
		{
			m_pGame->EditorResetGame(bGameMode);
		}
		m_bSaturateScreen = bGameMode;
		m_pGame->GetIGameFramework()->OnEditorSetGameMode(bGameMode);
	}
	else
	{
		gEnv->pLog->LogWarning("Failed configuring net context");
	}

	return ok;
}

IEntity* CEditorGame::GetPlayer()
{
	return gEnv->pEntitySystem->GetEntity(LOCAL_PLAYER_ENTITY_ID);
}

void CEditorGame::SetPlayerPosAng(Vec3 pos, Vec3 viewDir)
{
	IEntity* pPlayer = GetPlayer();
	if (pPlayer)
	{
		pPlayer->SetPosRotScale(pos, Quat::CreateRotationVDir(viewDir), Vec3(1.0f), ENTITY_XFORM_EDITOR);
	}
}

void CEditorGame::HidePlayer(bool bHide)
{
	IEntity* pPlayer = GetPlayer();
	if (pPlayer)
	{
		pPlayer->Hide(bHide);
	}
}

bool CEditorGame::ConfigureNetContext(bool on)
{
	bool ok = false;

	IGameFramework* pGameFramework = m_pGame->GetIGameFramework();

	if (on == m_bEnabled)
	{
		ok = true;
	}
	else if (on)
	{
		SGameContextParams ctx;

		SGameStartParams gameParams;
		gameParams.flags = eGSF_Server
			| eGSF_NoSpawnPlayer
			| eGSF_Client
			| eGSF_NoLevelLoading
			| eGSF_BlockingClientConnect
			| eGSF_NoGameRules
			| eGSF_NoQueries;

		gameParams.flags |= eGSF_LocalOnly;
		gameParams.connectionString = "";
		gameParams.hostname = "localhost";
		gameParams.port = 60695;
		gameParams.pContextParams = &ctx;
		gameParams.maxPlayers = 1;

		if (pGameFramework->StartGameContext(&gameParams))
			ok = true;
	}
	else
	{
		pGameFramework->EndGameContext();
		gEnv->pNetwork->SyncWithGame(eNGS_Shutdown);
		ok = true;
	}

	m_bEnabled = on && ok;
	return ok;
}

void CEditorGame::OnBeforeLevelLoad()
{
	EnablePlayer(false);
	ConfigureNetContext(true);
	m_pGame->GetIGameFramework()->GetIGameRulesSystem()->CreateGameRules("SinglePlayer");
	m_pGame->GetIGameFramework()->GetILevelSystem()->OnLoadingStart(0);
}

void CEditorGame::OnAfterLevelLoad(const char* levelName, const char* levelFolder)
{
	ILevelInfo* pLevel = m_pGame->GetIGameFramework()->GetILevelSystem()->SetEditorLoadedLevel(levelName);
	m_pGame->GetIGameFramework()->GetILevelSystem()->OnLoadingComplete(pLevel);
	EnablePlayer(true);
}

IFlowSystem* CEditorGame::GetIFlowSystem()
{
	return m_pGame->GetIGameFramework()->GetIFlowSystem();
}

IGameTokenSystem* CEditorGame::GetIGameTokenSystem()
{
	return m_pGame->GetIGameFramework()->GetIGameTokenSystem();
}
