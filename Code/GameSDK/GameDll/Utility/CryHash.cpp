// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "CryHash.h"

CryHashStringId CryHashStringId::GetIdForName( const char* _name )
{
	CRY_ASSERT(_name);

	return CryHashStringId(_name);
}
