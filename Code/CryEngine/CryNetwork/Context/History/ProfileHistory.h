// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#ifndef __PROFILEHISTORY_H__
#define __PROFILEHISTORY_H__

#pragma once

#include "History.h"

class CProfileHistory : public CHistory
{
public:
	CProfileHistory(CContextView*);

	virtual bool ReadCurrentValue(const SReceiveContext& ctx, bool commit);
	virtual void HandleEvent(const SHistoricalEvent& event);

private:
	virtual bool NeedToSync(const SSyncContext& ctx, CSyncContext* pSyncContext, const CRegularlySyncedItem& item);
	virtual bool CanSync(const SSyncContext& ctx, CSyncContext* pSyncContext, const CRegularlySyncedItem& item);
	virtual bool SendCurrentValue(const SSendContext& ctx, CSyncContext* pSyncContext, uint32& newValue);
};

#endif
