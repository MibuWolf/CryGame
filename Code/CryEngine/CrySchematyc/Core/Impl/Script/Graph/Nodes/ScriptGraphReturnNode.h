// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

#include <CrySchematyc/Runtime/RuntimeGraph.h>
#include <CrySchematyc/Utils/GUID.h>

#include "Script/Graph/ScriptGraphNodeModel.h"

namespace Schematyc
{
class CScriptGraphReturnNode : public CScriptGraphNodeModel
{
private:

	struct EInputIdx
	{
		enum : uint32
		{
			In = 0,
			FirstParam
		};
	};

public:

	CScriptGraphReturnNode();

	// CScriptGraphNodeModel
	virtual CryGUID GetTypeGUID() const override;
	virtual void  CreateLayout(CScriptGraphNodeLayout& layout) override;
	virtual void  Compile(SCompilerContext& context, IGraphNodeCompiler& compiler) const override;
	// ~CScriptGraphNodeModel

	static void Register(CScriptGraphNodeFactory& factory);

private:

	static SRuntimeResult Execute(SRuntimeContext& context, const SRuntimeActivationParams& activationParams);

public:

	static const CryGUID ms_typeGUID;
};
}
