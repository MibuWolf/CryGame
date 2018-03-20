// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

namespace Serialization
{

inline bool Serialize(Serialization::IArchive& ar, Serialization::ResourceFilePath& value, const char* name, const char* label)
{
	if (ar.isEdit())
		return ar(Serialization::SStruct::forEdit(value), name, label);
	else
		return ar(*value.path, name, label);
}

}
