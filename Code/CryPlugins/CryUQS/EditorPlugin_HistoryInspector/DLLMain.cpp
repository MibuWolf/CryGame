// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"

#define VC_EXTRALEAN
#include <afxwin.h>
#include <afxext.h>

#include <CryCore/Platform/platform.h>
#include <CryCore/Platform/platform_impl.inl>

#include <IEditor.h>
#include "IPlugin.h"
#include "IEditorClassFactory.h"

#include "../EditorCommon/QtViewPane.h"

#include "Editor/MainEditorWindow.h"

#define UQS_EDITOR_NAME "UQS History"

REGISTER_VIEWPANE_FACTORY_AND_MENU(CMainEditorWindow, UQS_EDITOR_NAME, "Game", true, "Universal Query System")

class CUqsEditorHistoryInespectorPlugin : public IPlugin
{
	enum
	{
		Version = 1,
	};

public:
	CUqsEditorHistoryInespectorPlugin()
	{
	}

	~CUqsEditorHistoryInespectorPlugin()
	{
	}

	virtual int32       GetPluginVersion() override                          { return DWORD(Version); }
	virtual const char* GetPluginName() override                             { return UQS_EDITOR_NAME; }
	virtual const char* GetPluginDescription() override						 { return ""; }
	// ~IPlugin
};

REGISTER_PLUGIN(CUqsEditorHistoryInespectorPlugin)

