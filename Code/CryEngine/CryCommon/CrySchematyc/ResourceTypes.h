// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// #SchematycTODO : Move serialization utils to separate header?

#pragma once

#include <CrySerialization/IArchive.h>
#include <CrySerialization/Decorators/ResourceFilePath.h>
#include <CrySerialization/Decorators/Resources.h>
#include <CrySerialization/Decorators/ResourcesAudio.h>
#include <CrySerialization/Decorators/ActionButton.h>

#include <CrySchematyc/Reflection/TypeDesc.h>

namespace Schematyc
{
namespace SerializationUtils
{

template<Serialization::ResourceFilePath(* SERIALIZER) (string&)> struct SResourceNameSerializer
{
	inline SResourceNameSerializer() {}

	inline SResourceNameSerializer(const char* _szValue)
		: value(_szValue)
	{}

	inline bool operator==(const SResourceNameSerializer& rhs) const
	{
		return value == rhs.value;
	}

	string value;
};

template<Serialization::ResourceFilePath(* SERIALIZER) (string&)> inline bool Serialize(Serialization::IArchive& archive, SResourceNameSerializer<SERIALIZER>& value, const char* szName, const char* szLabel)
{
	archive(SERIALIZER(value.value), szName, szLabel);
	return true;
}

template<Serialization::ResourceSelector<string>(* SELECTOR)(string&)> struct SResourceNameSelector
{
	inline SResourceNameSelector() {}

	inline SResourceNameSelector(const char* _szValue)
		: value(_szValue)
	{}

	inline bool operator==(const SResourceNameSelector& rhs) const
	{
		return value == rhs.value;
	}

	string value;
};

template<Serialization::ResourceSelector<string>(* SELECTOR)(string&)> inline bool Serialize(Serialization::IArchive& archive, SResourceNameSelector<SELECTOR>& value, const char* szName, const char* szLabel)
{
	archive(SELECTOR(value.value), szName, szLabel);
	return true;
}
} // SerializationUtils

typedef SerializationUtils::SResourceNameSelector<&Serialization::MaterialPicker<string>> MaterialFileName;

inline void ReflectType(CTypeDesc<MaterialFileName>& desc)
{
	desc.SetGUID("2c8887a2-0878-42cc-8cee-a0595baa0820"_cry_guid);
	desc.SetLabel("MaterialFileName");
	desc.SetDescription("Material file name");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::TextureFilename<string>> TextureFileName;

inline void ReflectType(CTypeDesc<TextureFileName>& desc)
{
	desc.SetGUID("A35E2F8F-F546-4B43-A158-1A896D2AF9A5"_cry_guid);
	desc.SetLabel("TextureFileName");
	desc.SetDescription("Texture file name");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::GeomCachePicker<string>> GeomCacheFileName;

inline void ReflectType(CTypeDesc<GeomCacheFileName>& desc)
{
	desc.SetGUID("{BE0FF234-DCA3-4379-A0F4-AE8FA54AA550}"_cry_guid);
	desc.SetLabel("Alembic File Name");
	desc.SetDescription("Path to an alembic file");
}

typedef SerializationUtils::SResourceNameSerializer<&Serialization::GeomPath> GeomFileName;

inline void ReflectType(CTypeDesc<GeomFileName>& desc)
{
	desc.SetGUID("bd6f2953-1127-4cdd-bfe7-79f98c97058c"_cry_guid);
	desc.SetLabel("GeomFileName");
	desc.SetDescription("Geometry file name");
}

typedef SerializationUtils::SResourceNameSerializer<&Serialization::SkinName> SkinName;

inline void ReflectType(CTypeDesc<SkinName>& desc)
{
	desc.SetGUID("243e1764-0bca-4af4-8a24-bf8a2fc8f9c8"_cry_guid);
	desc.SetLabel("SkinName");
	desc.SetDescription("Skin name");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::CharacterPath<string>> CharacterFileName;

inline void ReflectType(CTypeDesc<CharacterFileName>& desc)
{
	desc.SetGUID("cb3189c1-92de-4851-b26a-22894ec039b0"_cry_guid);
	desc.SetLabel("CharacterFileName");
	desc.SetDescription("Character file name");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::AnimationPath<string>> LowLevelAnimationName;

inline void ReflectType(CTypeDesc<LowLevelAnimationName>& desc)
{
	desc.SetGUID("{7BAA3267-E7BB-4B02-A223-2CB03F6BF35A}"_cry_guid);
	desc.SetLabel("Animation Name");
	desc.SetDescription("Name of an animation exported to the engine");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::ParticlePicker<string>> ParticleEffectName;

inline void ReflectType(CTypeDesc<ParticleEffectName>& desc)
{
	desc.SetGUID("a6f326e7-d3d3-428a-92c9-97185f003bec"_cry_guid);
	desc.SetLabel("ParticleEffectName");
	desc.SetDescription("Particle effect name");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::AudioSwitch<string>> AudioSwitchName;

inline void ReflectType(CTypeDesc<AudioSwitchName>& desc)
{
	desc.SetGUID("43ae9bb4-9831-449d-b082-3e37b71ec872"_cry_guid);
	desc.SetLabel("AudioSwitchName");
	desc.SetDescription("Audio switch name");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::AudioSwitchState<string>> AudioSwitchStateName;

inline void ReflectType(CTypeDesc<AudioSwitchStateName>& desc)
{
	desc.SetGUID("1877be04-a1db-490c-b6a2-47f99cac7fc2"_cry_guid);
	desc.SetLabel("AudioSwitchStateName");
	desc.SetDescription("Audio switch state name");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::AudioRTPC<string>> AudioRtpcName;

inline void ReflectType(CTypeDesc<AudioRtpcName>& desc)
{
	desc.SetGUID("730c191c-531f-48ae-bba9-5c1d8216b701"_cry_guid);
	desc.SetLabel("AudioRtpcName");
	desc.SetDescription("Audio Rtpc name");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::DialogName<string>> DialogName;

inline void ReflectType(CTypeDesc<DialogName>& desc)
{
	desc.SetGUID("839adde8-cc62-4636-9f26-16dd4236855f"_cry_guid);
	desc.SetLabel("DialogName");
	desc.SetDescription("Dialog name");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::EntityClassName<string>> EntityClassName;

inline void ReflectType(CTypeDesc<EntityClassName>& desc)
{
	desc.SetGUID("c366ed5c-5242-453f-bb2d-7247f7be2655"_cry_guid);
	desc.SetLabel("EntityClassName");
	desc.SetDescription("Entity class name");
}

typedef SerializationUtils::SResourceNameSerializer<&Serialization::MannequinAnimationDatabasePath> MannequinAnimationDatabasePath;

inline void ReflectType(CTypeDesc<MannequinAnimationDatabasePath>& desc)
{
	desc.SetGUID("{F21E3B5D-8C0D-423E-81BF-DE72B7DC72A8}"_cry_guid);
	desc.SetLabel("Mannequin Animation Database");
	desc.SetDescription("Path to a Mannequin animation database (.adb)");
}

typedef SerializationUtils::SResourceNameSerializer<&Serialization::MannequinControllerDefinitionPath> MannequinControllerDefinitionPath;

inline void ReflectType(CTypeDesc<MannequinControllerDefinitionPath>& desc)
{
	desc.SetGUID("{22B719FF-8B61-43CC-A997-B5292866CD0C}"_cry_guid);
	desc.SetLabel("Mannequin Controller Definition");
	desc.SetDescription("Path to a Mannequin controller definition");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::ForceFeedbackIdName<string>> ForceFeedbackId;

inline void ReflectType(CTypeDesc<ForceFeedbackId>& desc)
{
	desc.SetGUID("40dce92c-9532-4c02-8752-4afac04331ef"_cry_guid);
	desc.SetLabel("ForceFeedbackId");
	desc.SetDescription("Force feedback id");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::ActionMapName<string>> ActionMapName;

inline void ReflectType(CTypeDesc<ActionMapName>& desc)
{
	desc.SetGUID("{E67FEB06-ECF5-4CED-A03B-D5DA9536AB63}"_cry_guid);
	desc.SetLabel("Action Map Name");
	desc.SetDescription("Action map name");
}

typedef SerializationUtils::SResourceNameSelector<&Serialization::ActionMapActionName<string>> ActionMapActionName;

inline void ReflectType(CTypeDesc<ActionMapActionName>& desc)
{
	desc.SetGUID("{F9D3217F-2394-4059-BF1E-92CC73EA202E}"_cry_guid);
	desc.SetLabel("Action Map Action Name");
	desc.SetDescription("Action map action name");
}
typedef SerializationUtils::SResourceNameSelector<&Serialization::SurfaceTypeName<string>> SurfaceTypeName;

inline void ReflectType(CTypeDesc<SurfaceTypeName>& desc)
{
	desc.SetGUID("{14776399-DF80-43AF-A9C0-0544DB464A95}"_cry_guid);
	desc.SetLabel("Surface Type Name");
	desc.SetDescription("Name of a surface type with distinct physical parameters guiding the physics engine");
}

template<typename Functor>
inline void ReflectType(CTypeDesc<Serialization::FunctorActionButton<Functor>>& desc)
{
	desc.SetGUID("B5329286-CC95-4A0D-8996-9FC19B445ACB"_cry_guid);
	desc.SetLabel("Action");
	desc.SetDescription("Triggers an action in native code");
}

} // Schematyc
