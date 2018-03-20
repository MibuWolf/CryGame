// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  File name:   FlowGraphProxy.h
//  Version:     v1.00
//  Created:     6/6/2005 by Timur.
//  Compilers:   Visual Studio.NET 2003
//  Description:
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#ifndef __FlowGraphProxy_h__
#define __FlowGraphProxy_h__
#pragma once

#include <CryNetwork/ISerialize.h>

//////////////////////////////////////////////////////////////////////////
// Description:
//    Handles sounds in the entity.
//////////////////////////////////////////////////////////////////////////
class CEntityComponentFlowGraph : public IEntityFlowGraphComponent
{
	CRY_ENTITY_COMPONENT_CLASS_GUID(CEntityComponentFlowGraph, IEntityFlowGraphComponent, "CEntityComponentFlowGraph", "2bc1f44e-bd73-4ec5-bc8f-7982e75be23c"_cry_guid);

	CEntityComponentFlowGraph();
	virtual ~CEntityComponentFlowGraph();

public:
	//////////////////////////////////////////////////////////////////////////
	// IEntityComponent interface implementation.
	//////////////////////////////////////////////////////////////////////////
	virtual void Initialize() final;
	virtual void ProcessEvent(SEntityEvent& event) final;
	virtual uint64 GetEventMask() const final; // Need all events except pre physics update
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	// IEntityComponent interface implementation.
	//////////////////////////////////////////////////////////////////////////
	virtual EEntityProxy GetProxyType() const final { return ENTITY_PROXY_FLOWGRAPH; }
	virtual void         Release() final { delete this; };
	virtual void         LegacySerializeXML(XmlNodeRef& entityNode, XmlNodeRef& componentNode, bool bLoading) override final;
	virtual void         GameSerialize(TSerialize ser) final;
	virtual bool         NeedGameSerialize() final;
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	// IEntityFlowGraphComponent interface implementation.
	//////////////////////////////////////////////////////////////////////////
	virtual void        SetFlowGraph(IFlowGraph* pFlowGraph) final;
	virtual IFlowGraph* GetFlowGraph() final;
	virtual void        AddEventListener(IEntityEventListener* pListener) final;
	virtual void        RemoveEventListener(IEntityEventListener* pListener) final;
	//////////////////////////////////////////////////////////////////////////

	virtual void GetMemoryUsage(ICrySizer* pSizer) const final
	{
		pSizer->AddObject(this, sizeof(*this));
	}
private:
	void OnMove();

private:
	IFlowGraph* m_pFlowGraph;

	typedef std::list<IEntityEventListener*> Listeners;
	Listeners m_listeners;
};

#endif // __FlowGraphProxy_h__
