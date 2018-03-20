// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "StdAfx.h"
#include "EntityNode.h"
#include "CharacterTrack.h"
#include "AnimSplineTrack.h"
#include "BoolTrack.h"
#include "Tracks.h"
#include <CrySystem/ISystem.h>
#include "LookAtTrack.h"
#include "CompoundSplineTrack.h"
#include "Movie.h"
#include <CryMath/PNoise3.h>
#include "AnimSequence.h"

#include <CryAudio/IAudioSystem.h>
#include <CryDynamicResponseSystem/IDynamicResponseSystem.h>

#include <CryAction/ILipSync.h>
#include <CryAnimation/ICryAnimation.h>
#include <CryAnimation/CryCharMorphParams.h>
#include <CryMath/Cry_Camera.h>

#include <CryAISystem/IAgent.h>
#include <CryAISystem/IAIObject.h>
#include <CryAISystem/IAIActor.h>
#include <CryGame/IGameFramework.h>

#include <../CryAction/IActorSystem.h>
#define HEAD_BONE_NAME "Bip01 Head"
#include <../CryAction/ICryMannequinDefs.h>
#include <../CryAction/ICryMannequin.h>
#include <../CryAction/IAnimatedCharacter.h>

#define s_nodeParamsInitialized s_nodeParamsInitializedEnt
#define s_nodeParams            s_nodeParamsEnt
#define AddSupportedParam       AddSupportedParamEnt

static const float EPSILON = 0.01f;
static float movie_physicalentity_animation_lerp;

static float movie_timeJumpTransitionTime = 1.f;

static const char* s_VariablePrefixes[] =
{
	"n",                                                        "i",               "b",           "f",            "s",         "ei", "es",
	"shader",                                                   "clr",             "color",       "vector",
	"snd",                                                      "sound",           "dialog",      "tex",          "texture",
	"obj",                                                      "object",          "file",        "aibehavior",
#ifdef USE_DEPRECATED_AI_CHARACTER_SYSTEM
	"aicharacter",
#endif
	"aipfpropertieslist",                                       "aientityclasses", "aiterritory",
	"aiwave",                                                   "text",            "equip",       "reverbpreset", "eaxpreset",
	"aianchor",                                                 "soclass",         "soclasses",   "sostate",      "sostates"
	                                                                                                              "sopattern", "soaction",        "sohelper",    "sonavhelper",
	"soanimhelper",                                             "soevent",         "sotemplate",  "customaction",
	"gametoken",                                                "seq_",            "mission_",    "seqid_",       "lightanimation_"
};

namespace
{
const char* kScriptTablePrefix = "ScriptTable:";

bool s_nodeParamsInitialized = false;
std::vector<CAnimNode::SParamInfo> s_nodeParams;

void AddSupportedParam(std::vector<CAnimNode::SParamInfo>& nodeParams, const char* sName, int paramId, EAnimValue valueType, int flags = 0)
{
	CAnimNode::SParamInfo param;
	param.name = sName;
	param.paramType = paramId;
	param.valueType = valueType;
	param.flags = (IAnimNode::ESupportedParamFlags)flags;
	nodeParams.push_back(param);
}

void NotifyEntityScript(const IEntity* pEntity, const char* funcName)
{
	IScriptTable* pEntityScript = pEntity->GetScriptTable();

	if (pEntityScript && pEntityScript->HaveValue(funcName))
	{
		Script::CallMethod(pEntityScript, funcName);
	}
}

// Quat::IsEquivalent has numerical problems with very similar values
bool CompareRotation(const Quat& q1, const Quat& q2, float epsilon)
{
	return (fabs_tpl(q1.v.x - q2.v.x) <= epsilon)
	       && (fabs_tpl(q1.v.y - q2.v.y) <= epsilon)
	       && (fabs_tpl(q1.v.z - q2.v.z) <= epsilon)
	       && (fabs_tpl(q1.w - q2.w) <= epsilon);
}

void NotifyEntityScript(const IEntity* pEntity, const char* funcName, const char* strParam)
{
	IScriptTable* pEntityScript = pEntity->GetScriptTable();

	if (pEntityScript && pEntityScript->HaveValue(funcName))
	{
		Script::CallMethod(pEntityScript, funcName, strParam);
	}
}

};

CAnimEntityNode::CAnimEntityNode(const int id) : CAnimNode(id)
{
	m_EntityId = 0;
	m_target = NULL;
	m_bWasTransRot = false;
	m_bIsAnimDriven = false;
	m_bInitialPhysicsStatus = false;

	m_pos(0, 0, 0);
	m_scale(1, 1, 1);
	m_rotate.SetIdentity();

	m_visible = true;

	m_lookAtTarget = "";
	m_lookAtEntityId = 0;
	m_lookAtLocalPlayer = false;
	m_allowAdditionalTransforms = true;
	m_lookPose = "";

	m_proceduralFacialAnimationEnabledOld = true;

	m_EntityIdTarget = 0;
	m_EntityIdSource = 0;

	m_baseAnimState.m_layerPlaysAnimation[0] = m_baseAnimState.m_layerPlaysAnimation[1] = m_baseAnimState.m_layerPlaysAnimation[2] = false;

	m_baseAnimState.m_lastAnimationKeys[0][0] = -1;
	m_baseAnimState.m_lastAnimationKeys[0][1] = -1;
	m_baseAnimState.m_lastAnimationKeys[1][0] = -1;
	m_baseAnimState.m_lastAnimationKeys[1][1] = -1;
	m_baseAnimState.m_lastAnimationKeys[2][0] = -1;
	m_baseAnimState.m_lastAnimationKeys[2][1] = -1;

	m_baseAnimState.m_bTimeJumped[0] = m_baseAnimState.m_bTimeJumped[1] = m_baseAnimState.m_bTimeJumped[2] = false;
	m_baseAnimState.m_jumpTime[0] = m_baseAnimState.m_jumpTime[1] = m_baseAnimState.m_jumpTime[2] = 0.0f;

#ifdef CHECK_FOR_TOO_MANY_ONPROPERTY_SCRIPT_CALLS
	m_OnPropertyCalls = 0;
#endif

	m_vInterpPos(0, 0, 0);
	m_interpRot.SetIdentity();
	SetSkipInterpolatedCameraNode(false);

	m_lastEntityKey = -1;
	m_lastDrsSignalKey = -1;
	m_iCurMannequinKey = -1;

	CAnimEntityNode::Initialize();
}

void CAnimEntityNode::Initialize()
{
	if (!s_nodeParamsInitialized)
	{
		s_nodeParamsInitialized = true;
		s_nodeParams.reserve(19);
		AddSupportedParam(s_nodeParams, "Position", eAnimParamType_Position, eAnimValue_Vector);
		AddSupportedParam(s_nodeParams, "Rotation", eAnimParamType_Rotation, eAnimValue_Quat);
		AddSupportedParam(s_nodeParams, "Scale", eAnimParamType_Scale, eAnimValue_Vector);
		AddSupportedParam(s_nodeParams, "Visibility", eAnimParamType_Visibility, eAnimValue_Bool);
		AddSupportedParam(s_nodeParams, "Event", eAnimParamType_Event, eAnimValue_Unknown);
		AddSupportedParam(s_nodeParams, "Audio/Trigger", eAnimParamType_AudioTrigger, eAnimValue_Unknown, eSupportedParamFlags_MultipleTracks);
		AddSupportedParam(s_nodeParams, "Audio/File", eAnimParamType_AudioFile, eAnimValue_Unknown, eSupportedParamFlags_MultipleTracks);
		AddSupportedParam(s_nodeParams, "Audio/Parameter", eAnimParamType_AudioParameter, eAnimValue_Float, eSupportedParamFlags_MultipleTracks);
		AddSupportedParam(s_nodeParams, "Audio/Switch", eAnimParamType_AudioSwitch, eAnimValue_Unknown, eSupportedParamFlags_MultipleTracks);
		AddSupportedParam(s_nodeParams, "Dynamic Response Signal", eAnimParamType_DynamicResponseSignal, eAnimValue_Unknown, eSupportedParamFlags_MultipleTracks);
		AddSupportedParam(s_nodeParams, "Animation", eAnimParamType_Animation, eAnimValue_Unknown, eSupportedParamFlags_MultipleTracks);
		AddSupportedParam(s_nodeParams, "Mannequin", eAnimParamType_Mannequin, eAnimValue_Unknown, eSupportedParamFlags_MultipleTracks);
		AddSupportedParam(s_nodeParams, "Expression", eAnimParamType_Expression, eAnimValue_Unknown, eSupportedParamFlags_MultipleTracks);
		AddSupportedParam(s_nodeParams, "Facial Sequence", eAnimParamType_FaceSequence, eAnimValue_Unknown);
		AddSupportedParam(s_nodeParams, "LookAt", eAnimParamType_LookAt, eAnimValue_Unknown);
		AddSupportedParam(s_nodeParams, "Noise", eAnimParamType_TransformNoise, eAnimValue_Vector4);
		AddSupportedParam(s_nodeParams, "Physicalize", eAnimParamType_Physicalize, eAnimValue_Bool);
		AddSupportedParam(s_nodeParams, "PhysicsDriven", eAnimParamType_PhysicsDriven, eAnimValue_Bool);
		AddSupportedParam(s_nodeParams, "Procedural Eyes", eAnimParamType_ProceduralEyes, eAnimValue_Bool);

		REGISTER_CVAR(movie_physicalentity_animation_lerp, 0.85f, 0, "Lerp value for animation-driven physical entities");

		REGISTER_CVAR(movie_timeJumpTransitionTime, 1.f, 0, "Jump transition time");
	}
}

void CAnimEntityNode::AddTrack(IAnimTrack* pTrack)
{
	const CAnimParamType& paramType = pTrack->GetParameterType();

	if (paramType == eAnimParamType_PhysicsDriven)
	{
		CBoolTrack* pPhysicsDrivenTrack = static_cast<CBoolTrack*>(pTrack);
		pPhysicsDrivenTrack->SetDefaultValue(TMovieSystemValue(false));
	}
	else if (paramType == eAnimParamType_ProceduralEyes)
	{
		CBoolTrack* pProceduralEyesTrack = static_cast<CBoolTrack*>(pTrack);
		pProceduralEyesTrack->SetDefaultValue(TMovieSystemValue(false));
	}

	CAnimNode::AddTrack(pTrack);
}

void CAnimEntityNode::UpdateDynamicParams()
{
	m_entityScriptPropertiesParamInfos.clear();
	m_nameToScriptPropertyParamInfo.clear();

	// editor stores *all* properties of *every* entity used in an AnimEntityNode, including to-display names, full lua paths, string maps for fast access, etc.
	// in pure game mode we just need to store the properties that we know are going to be used in a track, so we can save a lot of memory.
	if (gEnv->IsEditor())
	{
		UpdateDynamicParams_Editor();
	}
	else
	{
		UpdateDynamicParams_PureGame();
	}
}

void CAnimEntityNode::UpdateDynamicParams_Editor()
{
	IEntity* pEntity = GetEntity();

	if (pEntity)
	{
		IScriptTable* pScriptTable = pEntity->GetScriptTable();

		if (!pScriptTable)
		{
			return;
		}

		SmartScriptTable propertiesTable;

		if (pScriptTable->GetValue("Properties", propertiesTable))
		{
			FindDynamicPropertiesRec(propertiesTable, "Properties/", 0);
		}

		SmartScriptTable propertiesInstanceTable;

		if (pScriptTable->GetValue("PropertiesInstance", propertiesInstanceTable))
		{
			FindDynamicPropertiesRec(propertiesInstanceTable, "PropertiesInstance/", 0);
		}
	}
}

void CAnimEntityNode::UpdateDynamicParams_PureGame()
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	IScriptTable* pEntityScriptTable = pEntity->GetScriptTable();

	if (!pEntityScriptTable)
	{
		return;
	}

	for (uint32 i = 0; i < m_tracks.size(); ++i)
	{
		_smart_ptr<IAnimTrack> pTrack = m_tracks[i];
		CAnimParamType paramType(pTrack->GetParameterType());

		if (paramType.GetType() != eAnimParamType_ByString)
		{
			continue;
		}

		string paramName = paramType.GetName();
		string scriptTablePrefix = kScriptTablePrefix;

		if (scriptTablePrefix != paramName.Left(scriptTablePrefix.size()))
		{
			continue;
		}

		string path = paramName.Right(paramName.size() - scriptTablePrefix.size());

		string propertyName;
		SmartScriptTable propertyScriptTable;
		FindScriptTableForParameterRec(pEntityScriptTable, path, propertyName, propertyScriptTable);

		if (propertyScriptTable && !propertyName.empty())
		{
			SScriptPropertyParamInfo paramInfo;
			bool isUnknownTable = ObtainPropertyTypeInfo(propertyName.c_str(), propertyScriptTable, paramInfo);

			if (paramInfo.animNodeParamInfo.valueType == eAnimValue_Unknown)
			{
				return;
			}

			string strippedPath = path.Left(path.size() - propertyName.size());
			AddPropertyToParamInfoMap(propertyName.c_str(), strippedPath, paramInfo);
		}
	}
}

void CAnimEntityNode::FindScriptTableForParameterRec(IScriptTable* pScriptTable, const string& path, string& propertyName, SmartScriptTable& propertyScriptTable)
{
	size_t pos = path.find_first_of('/');

	if (pos == string::npos)
	{
		propertyName = path;
		propertyScriptTable = pScriptTable;
		return;
	}

	string tableName = path.Left(pos);
	pos++;
	string pathLeft = path.Right(path.size() - pos);

	ScriptAnyValue value;
	pScriptTable->GetValueAny(tableName.c_str(), value);
	assert(value.type == ANY_TTABLE);

	if (value.type == ANY_TTABLE)
	{
		FindScriptTableForParameterRec(value.table, pathLeft, propertyName, propertyScriptTable);
	}
}

void CAnimEntityNode::FindDynamicPropertiesRec(IScriptTable* pScriptTable, const string& currentPath, unsigned int depth)
{
	// There might be infinite recursion in the tables, so we need to limit the max recursion depth
	const unsigned int kMaxRecursionDepth = 5;

	IScriptTable::Iterator iter = pScriptTable->BeginIteration();

	while (pScriptTable->MoveNext(iter))
	{
		if (!iter.sKey || iter.sKey[0] == '_')  // Skip properties that start with an underscore
		{
			continue;
		}

		SScriptPropertyParamInfo paramInfo;
		bool isUnknownTable = ObtainPropertyTypeInfo(iter.sKey, pScriptTable, paramInfo);

		if (isUnknownTable && depth < kMaxRecursionDepth)
		{
			FindDynamicPropertiesRec(paramInfo.scriptTable, currentPath + iter.sKey + "/", depth + 1);
			continue;
		}

		if (paramInfo.animNodeParamInfo.valueType != eAnimValue_Unknown)
		{
			AddPropertyToParamInfoMap(iter.sKey, currentPath, paramInfo);
		}
	}

	pScriptTable->EndIteration(iter);
}

// fills some fields in paramInfo with the appropriate value related to the property defined by pScriptTable and pKey.
// returns true if the property is a table that should be parsed, instead of an atomic type  (simple vectors are treated like atomic types)
bool CAnimEntityNode::ObtainPropertyTypeInfo(const char* pKey, IScriptTable* pScriptTable, SScriptPropertyParamInfo& paramInfo)
{
	ScriptAnyValue value;
	pScriptTable->GetValueAny(pKey, value);
	paramInfo.scriptTable = pScriptTable;
	paramInfo.isVectorTable = false;
	paramInfo.animNodeParamInfo.valueType = eAnimValue_Unknown;
	bool isUnknownTable = false;

	switch (value.type)
	{
	case ANY_TNUMBER:
		{
			const bool hasBoolPrefix = (strlen(pKey) > 1) && (pKey[0] == 'b')
			                           && (pKey[1] != tolower(pKey[1]));
			paramInfo.animNodeParamInfo.valueType = hasBoolPrefix ? eAnimValue_Bool : eAnimValue_Float;
		}
		break;

	case ANY_TVECTOR:
		paramInfo.animNodeParamInfo.valueType = eAnimValue_Vector;
		break;

	case ANY_TBOOLEAN:
		paramInfo.animNodeParamInfo.valueType = eAnimValue_Bool;
		break;

	case ANY_TTABLE:
		// Threat table as vector if it contains x, y & z
		paramInfo.scriptTable = value.table;

		if (value.table->HaveValue("x") && value.table->HaveValue("y") && value.table->HaveValue("z"))
		{
			paramInfo.animNodeParamInfo.valueType = eAnimValue_Vector;
			paramInfo.scriptTable = value.table;
			paramInfo.isVectorTable = true;
		}
		else
		{
			isUnknownTable = true;
		}

		break;
	}

	return isUnknownTable;
}

void CAnimEntityNode::AddPropertyToParamInfoMap(const char* pKey, const string& currentPath, SScriptPropertyParamInfo& paramInfo)
{
	// Strip variable name prefix
	const char* strippedName = pKey;

	while (*strippedName && (*strippedName == tolower(*strippedName) || *strippedName == '_'))
	{
		++strippedName;
	}

	const size_t prefixLength = strippedName - pKey;

	// Check if stripped prefix is in list of valid variable prefixes
	bool foundPrefix = false;

	for (size_t i = 0; i < sizeof(s_VariablePrefixes) / sizeof(const char*); ++i)
	{
		if ((strlen(s_VariablePrefixes[i]) == prefixLength) &&
		    (memcmp(pKey, s_VariablePrefixes[i], prefixLength) == 0))
		{
			foundPrefix = true;
			break;
		}
	}

	// Only use stripped name if prefix is in list, otherwise use full name
	strippedName = foundPrefix ? strippedName : pKey;

	// If it is a vector check if we need to create a color track
	if (paramInfo.animNodeParamInfo.valueType == eAnimValue_Vector)
	{
		if ((prefixLength == 3 && memcmp(pKey, "clr", 3) == 0)
		    || (prefixLength == 5 && memcmp(pKey, "color", 5) == 0))
		{
			paramInfo.animNodeParamInfo.valueType = eAnimValue_RGB;
		}
	}

	paramInfo.variableName = pKey;
	paramInfo.displayName = currentPath + strippedName;
	paramInfo.animNodeParamInfo.name = &paramInfo.displayName[0];
	const string paramIdStr = kScriptTablePrefix + currentPath + pKey;
	paramInfo.animNodeParamInfo.paramType = CAnimParamType(paramIdStr);
	paramInfo.animNodeParamInfo.flags = eSupportedParamFlags_MultipleTracks;
	m_entityScriptPropertiesParamInfos.push_back(paramInfo);

	m_nameToScriptPropertyParamInfo[paramIdStr] = m_entityScriptPropertiesParamInfos.size() - 1;
}

void CAnimEntityNode::InitializeTrackDefaultValue(IAnimTrack* pTrack, const CAnimParamType& paramType)
{
	// Initialize new track to property value
	if (paramType.GetType() == eAnimParamType_ByString && pTrack && (strncmp(paramType.GetName(), kScriptTablePrefix, strlen(kScriptTablePrefix)) == 0))
	{
		IEntity* pEntity = GetEntity();

		if (!pEntity)
		{
			return;
		}

		TScriptPropertyParamInfoMap::iterator findIter = m_nameToScriptPropertyParamInfo.find(paramType.GetName());

		if (findIter != m_nameToScriptPropertyParamInfo.end())
		{
			SScriptPropertyParamInfo& param = m_entityScriptPropertiesParamInfos[findIter->second];

			float fValue = 0.0f;
			Vec3 vecValue = Vec3(0.0f, 0.0f, 0.0f);
			bool boolValue = false;

			switch (pTrack->GetValueType())
			{
			case eAnimValue_Float:
				param.scriptTable->GetValue(param.variableName, fValue);
				pTrack->SetDefaultValue(TMovieSystemValue(fValue));
				break;

			case eAnimValue_Vector:

			// fall through
			case eAnimValue_RGB:
				if (param.isVectorTable)
				{
					param.scriptTable->GetValue("x", vecValue.x);
					param.scriptTable->GetValue("y", vecValue.y);
					param.scriptTable->GetValue("z", vecValue.z);
				}
				else
				{
					param.scriptTable->GetValue(param.variableName, vecValue);
				}

				if (pTrack->GetValueType() == eAnimValue_RGB)
				{
					vecValue.x = clamp_tpl(vecValue.x, 0.0f, 1.0f);
					vecValue.y = clamp_tpl(vecValue.y, 0.0f, 1.0f);
					vecValue.z = clamp_tpl(vecValue.z, 0.0f, 1.0f);

					vecValue *= 255.0f;
				}

				pTrack->SetDefaultValue(TMovieSystemValue(vecValue));
				break;

			case eAnimValue_Bool:
				param.scriptTable->GetValue(param.variableName, boolValue);
				pTrack->SetDefaultValue(TMovieSystemValue(boolValue));
				break;
			}
		}
	}
}

void CAnimEntityNode::CreateDefaultTracks()
{
	CreateTrack(eAnimParamType_Position);
	CreateTrack(eAnimParamType_Rotation);
}

CAnimEntityNode::~CAnimEntityNode()
{
	ReleaseSounds();
}

unsigned int CAnimEntityNode::GetParamCount() const
{
	return CAnimEntityNode::GetParamCountStatic() + m_entityScriptPropertiesParamInfos.size();
}

CAnimParamType CAnimEntityNode::GetParamType(unsigned int nIndex) const
{
	SParamInfo info;

	if (!CAnimEntityNode::GetParamInfoStatic(nIndex, info))
	{
		const uint scriptParamsOffset = (uint)s_nodeParams.size();
		const uint end = (uint)s_nodeParams.size() + (uint)m_entityScriptPropertiesParamInfos.size();

		if (nIndex >= scriptParamsOffset && nIndex < end)
		{
			return m_entityScriptPropertiesParamInfos[nIndex - scriptParamsOffset].animNodeParamInfo.paramType;
		}

		return eAnimParamType_Invalid;
	}

	return info.paramType;
}

int CAnimEntityNode::GetParamCountStatic()
{
	return s_nodeParams.size();
}

bool CAnimEntityNode::GetParamInfoStatic(int nIndex, SParamInfo& info)
{
	if (nIndex >= 0 && nIndex < (int)s_nodeParams.size())
	{
		info = s_nodeParams[nIndex];
		return true;
	}

	return false;
}

bool CAnimEntityNode::GetParamInfoFromType(const CAnimParamType& paramId, SParamInfo& info) const
{
	for (int i = 0; i < (int)s_nodeParams.size(); i++)
	{
		if (s_nodeParams[i].paramType == paramId)
		{
			info = s_nodeParams[i];
			return true;
		}
	}

	for (size_t i = 0; i < m_entityScriptPropertiesParamInfos.size(); ++i)
	{
		if (m_entityScriptPropertiesParamInfos[i].animNodeParamInfo.paramType == paramId)
		{
			info = m_entityScriptPropertiesParamInfos[i].animNodeParamInfo;
			return true;
		}
	}

	return false;
}

const char* CAnimEntityNode::GetParamName(const CAnimParamType& param) const
{
	SParamInfo info;

	if (GetParamInfoFromType(param, info))
	{
		return info.name;
	}

	const char* pName = param.GetName();

	if (param.GetType() == eAnimParamType_ByString && pName && strncmp(pName, kScriptTablePrefix, strlen(kScriptTablePrefix)) == 0)
	{
		return pName + strlen(kScriptTablePrefix);
	}

	return "Unknown Entity Parameter";
}

IEntity* CAnimEntityNode::GetEntity()
{
	if (!m_EntityId)
	{
		m_EntityId = gEnv->pEntitySystem->FindEntityByGuid(m_entityGuid);
	}

	return gEnv->pEntitySystem->GetEntity(m_EntityId);
}

void CAnimEntityNode::SetEntityGuid(const EntityGUID& guid)
{
	m_entityGuid = guid;
	m_EntityId = gEnv->pEntitySystem->FindEntityByGuid(m_entityGuid);
	UpdateDynamicParams();
}

void CAnimEntityNode::SetEntityGuidTarget(const EntityGUID& guid)
{
	m_entityGuidTarget = guid;
	m_EntityIdTarget = gEnv->pEntitySystem->FindEntityByGuid(m_entityGuidTarget);
}

void CAnimEntityNode::SetEntityId(const int entityId)
{
	m_EntityId = entityId;
}

void CAnimEntityNode::SetEntityGuidSource(const EntityGUID& guid)
{
	m_entityGuidSource = guid;
	m_EntityIdSource = gEnv->pEntitySystem->FindEntityByGuid(m_entityGuidSource);
}

void CAnimEntityNode::StillUpdate()
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	int paramCount = NumTracks();

	for (int paramIndex = 0; paramIndex < paramCount; paramIndex++)
	{
		IAnimTrack* pTrack = m_tracks[paramIndex];

		switch (m_tracks[paramIndex]->GetParameterType().GetType())
		{
		case eAnimParamType_LookAt:
			{
				SAnimContext animContext;
				animContext.time = m_time;

				CLookAtTrack* pSelTrack = (CLookAtTrack*)pTrack;
				AnimateLookAt(pSelTrack, animContext);
			}
			break;
		}
	}
}

void CAnimEntityNode::EnableEntityPhysics(bool bEnable)
{
	if (IEntity* pEntity = GetEntity())
	{
		if (pEntity->IsPhysicsEnabled() != bEnable)
		{
			pEntity->EnablePhysics(bEnable);

			if (bEnable)
			{
				// Force xform update to move physics proxy. Physics ignores updates while physics are disabled.
				SEntityEvent event(ENTITY_EVENT_XFORM);
				event.nParam[0] = ENTITY_XFORM_POS | ENTITY_XFORM_ROT | ENTITY_XFORM_SCL;
				pEntity->SendEvent(event);
			}
		}
	}
}

void CAnimEntityNode::Animate(SAnimContext& animContext)
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	if (!pEntity->GetProxy(ENTITY_PROXY_ENTITYNODE))
	{
		pEntity->CreateProxy(ENTITY_PROXY_ENTITYNODE);
	}

	// Here in Animate always get local pos, rot, scale value instead of pEntity->GetScale, to avoid comparing Track View value, to Track View value, which will always be the same.
	// pEntity->GetPos,pEntity->GetRotation, pEntity->GetScale are updated before this, by delegated mode, from Track View key value
	// pPosTrack->GetValue == pEntity->GetPos etc

	Vec3 pos = m_pos;
	Quat rotate = m_rotate;
	Vec3 scale = m_scale;
	Vec4 noiseParam(0.f, 0.f, 0.f, 0.f);

	int entityUpdateFlags = 0;
	bool bScaleModified = false;
	bool bApplyNoise = false;
	bool bScriptPropertyModified = false;

	IAnimTrack* pPosTrack = NULL;
	IAnimTrack* pRotTrack = NULL;
	IAnimTrack* pScaleTrack = NULL;

	if (!animContext.bResetting)
	{
		StopExpressions();
	}

	PrecacheDynamic(animContext.time);

	int animCharacterLayer = 0;
	int animationTrack = 0;
	size_t numAudioFileTracks = 0;
	size_t numAudioSwitchTracks = 0;
	size_t numAudioTriggerTracks = 0;
	size_t numAudioParameterTracks = 0;
	const int trackCount = NumTracks();

	for (int paramIndex = 0; paramIndex < trackCount; paramIndex++)
	{
		CAnimParamType paramType = m_tracks[paramIndex]->GetParameterType();
		IAnimTrack* pTrack = m_tracks[paramIndex];

		const bool bMute = gEnv->IsEditor() && (pTrack->GetFlags() & IAnimTrack::eAnimTrackFlags_Muted);
		if ((pTrack->HasKeys() == false && pTrack->GetParameterType() != eAnimParamType_Visibility
		     && !IsSkipInterpolatedCameraNodeEnabled())
		    || (pTrack->GetFlags() & IAnimTrack::eAnimTrackFlags_Disabled)
		    || pTrack->IsMasked(animContext.trackMask))
		{
			continue;
		}

		switch (paramType.GetType())
		{
		case eAnimParamType_Position:
			pPosTrack = pTrack;

			if (!IsSkipInterpolatedCameraNodeEnabled())
			{
				pos = stl::get<Vec3>(pPosTrack->GetValue(animContext.time));

				if (!IsEquivalent(pos, GetPos(), 0.0001f))
				{
					entityUpdateFlags |= eUpdateEntity_Position;
				}
			}
			else
			{
				if (!IsEquivalent(pos, GetPos(), 0.0001f))
				{
					entityUpdateFlags |= eUpdateEntity_Position;
					pos = m_vInterpPos;
				}
			}

			break;

		case eAnimParamType_Rotation:
			pRotTrack = pTrack;

			if (!IsSkipInterpolatedCameraNodeEnabled())
			{
				rotate = stl::get<Quat>(pRotTrack->GetValue(animContext.time));

				if (!CompareRotation(rotate, GetRotate(), DEG2RAD(0.0001f)))
				{
					entityUpdateFlags |= eUpdateEntity_Rotation;
				}
			}
			else
			{
				if (!CompareRotation(rotate, GetRotate(), DEG2RAD(0.0001f)))
				{
					entityUpdateFlags |= eUpdateEntity_Rotation;
					rotate = m_interpRot;
				}
			}

			break;

		case eAnimParamType_TransformNoise:
			{
				noiseParam = stl::get<Vec4>(pTrack->GetValue(animContext.time));
				m_posNoise.m_amp = noiseParam.x;
				m_posNoise.m_freq = noiseParam.y;
				m_rotNoise.m_amp = noiseParam.z;
				m_rotNoise.m_freq = noiseParam.w;
				bApplyNoise = true;
			}
			break;

		case eAnimParamType_Scale:
			{
				pScaleTrack = pTrack;
				scale = stl::get<Vec3>(pScaleTrack->GetValue(animContext.time));

				// Check whether the scale value is valid.
				if (scale.x < 0.01 || scale.y < 0.01 || scale.z < 0.01)
				{
					CryWarning(VALIDATOR_MODULE_MOVIE, VALIDATOR_WARNING,
					           "An EntityNode <%s> gets an invalid scale (%f,%f,%f) from a TrackView track, so ignored.",
					           (const char*)GetName(), scale.x, scale.y, scale.z);
					scale = m_scale;
				}

				if (!IsEquivalent(scale, GetScale(), 0.001f))
				{
					bScaleModified = true;
				}
			}
			break;

		case eAnimParamType_Event:
			if (!animContext.bResetting)
			{
				CEventTrack* pEntityTrack = (CEventTrack*)pTrack;
				SEventKey key;
				int entityKey = pEntityTrack->GetActiveKey(animContext.time, &key);

				// If key is different or if time is standing exactly on key time.
				if (entityKey != m_lastEntityKey || key.m_time == animContext.time)
				{
					m_lastEntityKey = entityKey;

					if (entityKey >= 0)
					{
						const bool bNotTrigger = key.m_bNoTriggerInScrubbing && animContext.bSingleFrame && key.m_time != animContext.time;

						if (!bNotTrigger)
						{
							const bool bRagollizeEvent = strcmp(key.m_event, "Ragdollize") == 0;

							if (!bRagollizeEvent || gEnv->pMovieSystem->IsPhysicsEventsEnabled())
							{
								ApplyEventKey(pEntityTrack, entityKey, key);
							}

							bool bHideOrUnHide = strcmp(key.m_event, "Hide") == 0 || strcmp(key.m_event, "UnHide") == 0;

							if (m_pOwner && bHideOrUnHide)
							{
								m_pOwner->OnNodeVisibilityChanged(this, pEntity->IsHidden());
							}
						}
					}
				}
			}

			break;

		case eAnimParamType_Visibility:
			if (!animContext.bResetting)
			{
				IAnimTrack* pVisibilityTrack = pTrack;
				bool bVisible = stl::get<bool>(pVisibilityTrack->GetValue(animContext.time));

				if (pEntity->IsHidden() == bVisible)
				{
					pEntity->Hide(!bVisible);

					if (bVisible)
					{
						IAnimTrack* pPhysicalizeTrack = GetTrackForParameter(eAnimParamType_Physicalize);

						if (pPhysicalizeTrack)
						{
							const bool bUsePhysics = stl::get<bool>(pPhysicalizeTrack->GetValue(m_time));
							EnableEntityPhysics(bUsePhysics);
						}
					}

					m_visible = bVisible;

					if (m_pOwner)
					{
						m_pOwner->OnNodeVisibilityChanged(this, pEntity->IsHidden());
					}
				}
			}

			break;

		case eAnimParamType_ProceduralEyes:
			{
				if (!animContext.bResetting)
				{
					IAnimTrack* procEyeTrack = pTrack;
					const bool bUseProcEyes = stl::get<bool>(procEyeTrack->GetValue(animContext.time));
					EnableProceduralFacialAnimation(bUseProcEyes);
				}

				break;
			}
		case eAnimParamType_AudioTrigger:
			{
				++numAudioTriggerTracks;

				if (numAudioTriggerTracks > m_audioTriggerTracks.size())
				{
					m_audioTriggerTracks.resize(numAudioTriggerTracks);
				}

				if (!animContext.bResetting && !bMute && animContext.time > SAnimTime(0))
				{
					SAudioTriggerKey audioTriggerKey;
					const int audioTriggerKeyNum = static_cast<CAudioTriggerTrack*>(pTrack)->GetActiveKey(animContext.time, &audioTriggerKey);
					SAudioInfo& audioTriggerInfo = m_audioTriggerTracks[numAudioTriggerTracks - 1];

					if (audioTriggerKeyNum >= 0)
					{
						if (audioTriggerInfo.audioKeyStart < audioTriggerKeyNum)
						{
							ApplyAudioTriggerKey(audioTriggerKey.m_startTriggerId);
						}

						if (audioTriggerInfo.audioKeyStart > audioTriggerKeyNum)
						{
							audioTriggerInfo.audioKeyStop = audioTriggerKeyNum;
						}

						audioTriggerInfo.audioKeyStart = audioTriggerKeyNum;

						SAnimTime const soundKeyTime = (animContext.time - audioTriggerKey.m_time);
						if (audioTriggerKey.m_duration > SAnimTime(0) && soundKeyTime >= audioTriggerKey.m_duration)
						{
							if (audioTriggerInfo.audioKeyStop < audioTriggerKeyNum)
							{
								audioTriggerInfo.audioKeyStop = audioTriggerKeyNum;

								if (audioTriggerKey.m_stopTriggerId != CryAudio::InvalidControlId)
								{
									ApplyAudioTriggerKey(audioTriggerKey.m_stopTriggerId);
								}
								else
								{
									ApplyAudioTriggerKey(audioTriggerKey.m_startTriggerId, false);
								}
							}
						}
						else
						{
							audioTriggerInfo.audioKeyStop = -1;
						}
					}
					else
					{
						audioTriggerInfo.audioKeyStart = -1;
						audioTriggerInfo.audioKeyStop = -1;
					}
				}

				break;
			}

		case eAnimParamType_AudioFile:
			{
				++numAudioFileTracks;

				if (numAudioFileTracks > m_audioFileTracks.size())
				{
					m_audioFileTracks.resize(numAudioFileTracks);
				}

				if (!animContext.bResetting && !bMute)
				{
					SAudioFileKey audioFileKey;
					SAudioInfo& audioFileInfo = m_audioFileTracks[numAudioFileTracks - 1];
					CAudioFileTrack* pAudioFileTrack = static_cast<CAudioFileTrack*>(pTrack);
					const int audioFileKeyNum = pAudioFileTrack->GetActiveKey(animContext.time, &audioFileKey);
					if (pEntity && audioFileKeyNum >= 0 && audioFileKey.m_duration > SAnimTime(0) && !(audioFileKey.m_bNoTriggerInScrubbing && animContext.bSingleFrame))
					{
						const SAnimTime audioKeyTime = (animContext.time - audioFileKey.m_time);
						if (animContext.time <= audioFileKey.m_time + audioFileKey.m_duration)
						{
							if (audioFileInfo.audioKeyStart < audioFileKeyNum)
							{
								IEntityAudioComponent* pIEntityAudioComponent = pEntity->GetOrCreateComponent<IEntityAudioComponent>();
								if (pIEntityAudioComponent)
								{
									const CryAudio::SPlayFileInfo audioPlayFileInfo(audioFileKey.m_audioFile, audioFileKey.m_bIsLocalized);
									pIEntityAudioComponent->PlayFile(audioPlayFileInfo);
								}
							}
							audioFileInfo.audioKeyStart = audioFileKeyNum;
						}
						else if (audioKeyTime >= audioFileKey.m_duration)
						{
							audioFileInfo.audioKeyStart = -1;
						}
					}
					else
					{
						audioFileInfo.audioKeyStart = -1;
					}
				}

				break;
			}

		case eAnimParamType_AudioParameter:
			{
				++numAudioParameterTracks;

				if (numAudioParameterTracks > m_audioParameterTracks.size())
				{
					m_audioParameterTracks.resize(numAudioParameterTracks, 0.0f);
				}

				CryAudio::ControlId audioParameterId = static_cast<CAudioParameterTrack*>(pTrack)->m_audioParameterId;
				if (audioParameterId != CryAudio::InvalidControlId)
				{
					const float newAudioParameterValue = stl::get<float>(pTrack->GetValue(animContext.time));
					float& prevAudioParameterValue = m_audioParameterTracks[numAudioParameterTracks - 1];
					if (fabs(prevAudioParameterValue - newAudioParameterValue) > FLT_EPSILON)
					{
						IEntityAudioComponent* pIEntityAudioComponent = pEntity->GetOrCreateComponent<IEntityAudioComponent>();
						if (pIEntityAudioComponent)
						{
							pIEntityAudioComponent->SetParameter(audioParameterId, newAudioParameterValue);
							prevAudioParameterValue = newAudioParameterValue;
						}
					}
				}

				break;
			}

		case eAnimParamType_AudioSwitch:
			{
				++numAudioSwitchTracks;

				if (numAudioSwitchTracks > m_audioSwitchTracks.size())
				{
					m_audioSwitchTracks.resize(numAudioSwitchTracks, -1);
				}

				if (!animContext.bResetting && !bMute)
				{
					SAudioSwitchKey audioSwitchKey;
					CAudioSwitchTrack* pAudioSwitchTrack = static_cast<CAudioSwitchTrack*>(pTrack);
					int& prevAudioSwitchKeyNum = m_audioSwitchTracks[numAudioSwitchTracks - 1];
					const int newAudioSwitchKeyNum = pAudioSwitchTrack->GetActiveKey(animContext.time, &audioSwitchKey);
					if (prevAudioSwitchKeyNum != newAudioSwitchKeyNum)
					{
						if (newAudioSwitchKeyNum >= 0)
						{
							CryAudio::ControlId audioSwitchId = audioSwitchKey.m_audioSwitchId;
							CryAudio::SwitchStateId audioSwitchStateId = audioSwitchKey.m_audioSwitchStateId;
							if (audioSwitchId != CryAudio::InvalidControlId && audioSwitchStateId != CryAudio::InvalidSwitchStateId)
							{
								IEntityAudioComponent* pIEntityAudioComponent = pEntity->GetOrCreateComponent<IEntityAudioComponent>();
								if (pIEntityAudioComponent)
								{
									pIEntityAudioComponent->SetSwitchState(audioSwitchId, audioSwitchStateId);
									prevAudioSwitchKeyNum = newAudioSwitchKeyNum;
								}
							}
						}
					}
				}

				break;
			}

		case eAnimParamType_DynamicResponseSignal:
			{
				if (!animContext.bResetting)
				{
					CDynamicResponseSignalTrack* pDRSSignalTrack = (CDynamicResponseSignalTrack*)pTrack;
					SDynamicResponseSignalKey key;
					int drsSignalKey = pDRSSignalTrack->GetActiveKey(animContext.time, &key);
					if (drsSignalKey != m_lastDrsSignalKey || key.m_time == animContext.time)
					{
						m_lastDrsSignalKey = drsSignalKey;
						if (drsSignalKey >= 0)
						{
							const bool bNotTrigger = key.m_bNoTriggerInScrubbing && animContext.bSingleFrame && key.m_time != animContext.time;
							if (!bNotTrigger && pEntity)
							{
								string sequenceName;
								sequenceName.Format("SendDrsSignal from Sequence '%s'", animContext.pSequence->GetName());
								SET_DRS_USER_SCOPED(sequenceName);

								const IEntityDynamicResponseComponent* pIEntityDrsProxy = crycomponent_cast<IEntityDynamicResponseComponent*>(pEntity->CreateProxy(ENTITY_PROXY_DYNAMICRESPONSE));
								DRS::IVariableCollectionSharedPtr pContextVariableCollection = nullptr;

								if (key.m_contextVariableName[0] != 0)
								{
									pContextVariableCollection = gEnv->pDynamicResponseSystem->CreateContextCollection();
									pContextVariableCollection->SetVariableValue(key.m_contextVariableName, CHashedString(key.m_contextVariableValue));
								}

								pIEntityDrsProxy->GetResponseActor()->QueueSignal(key.m_signalName, pContextVariableCollection);
							}
						}
					}
				}

				break;
			}

		case eAnimParamType_Mannequin:
			if (!animContext.bResetting)
			{
				CMannequinTrack* pMannequinTrack = (CMannequinTrack*)pTrack;
				SMannequinKey manKey;
				int key = pMannequinTrack->GetActiveKey(animContext.time, &manKey);

				if (key >= 0)
				{
					int breakhere = 0;

					//Only activate once
					if (m_iCurMannequinKey != key)
					{
						m_iCurMannequinKey = key;

						const uint32 valueName = CCrc32::ComputeLowercase(manKey.m_fragmentName);
						IGameObject* pGameObject = gEnv->pGameFramework->GetGameObject(pEntity->GetId());

						if (pGameObject)
						{
							IAnimatedCharacter* pAnimChar = (IAnimatedCharacter*) pGameObject->QueryExtension("AnimatedCharacter");

							if (pAnimChar)
							{
								const FragmentID fragID = pAnimChar->GetActionController()->GetContext().controllerDef.m_fragmentIDs.Find(valueName);

								bool isValid = !(FRAGMENT_ID_INVALID == fragID);

								// Find tags
								string tagName = manKey.m_tags;
								TagState tagState = TAG_STATE_EMPTY;

								if (!tagName.empty())
								{
									const CTagDefinition* tagDefinition = pAnimChar->GetActionController()->GetTagDefinition(fragID);
									tagName.append("+");

									while (tagName.find("+") != string::npos)
									{
										string::size_type strPos = tagName.find("+");

										string rest = tagName.substr(strPos + 1, tagName.size());
										string curTagName = tagName.substr(0, strPos);
										bool found = false;

										if (tagDefinition)
										{
											const uint32 tagCRC = CCrc32::ComputeLowercase(curTagName);
											const TagID tagID = tagDefinition->Find(tagCRC);
											found = tagID != TAG_ID_INVALID;
											tagDefinition->Set(tagState, tagID, true);
										}

										if (!found)
										{
											const TagID tagID = pAnimChar->GetActionController()->GetContext().state.GetDef().Find(curTagName);

											if (tagDefinition)
											{
												const int tagCRC = CCrc32::ComputeLowercase(curTagName);
												const TagID tagID = tagDefinition->Find(tagCRC);
												found = tagID != TAG_ID_INVALID;
												tagDefinition->Set(tagState, tagID, true);
											}

											if (!found)
											{
												const TagID tagID = pAnimChar->GetActionController()->GetContext().state.GetDef().Find(curTagName);
												if (tagID != TAG_ID_INVALID)
												{
													pAnimChar->GetActionController()->GetContext().state.Set(tagID, true);
												}
											}
										}

										tagName = rest;
									}

								}

								IAction* pAction = new TAction<SAnimationContext>(manKey.m_priority, fragID, tagState, 0u, 0xffffffff);
								pAnimChar->GetActionController()->Queue(*pAction);
							}
						}
					}

				}
				else
				{
					m_iCurMannequinKey = -1;
				}
			}

			break;

		case eAnimParamType_Animation:
			if (!animContext.bResetting)
			{
				if (animCharacterLayer < MAX_CHARACTER_TRACKS + ADDITIVE_LAYERS_OFFSET)
				{
					int index = animCharacterLayer;
					CCharacterTrack* pCharTrack = (CCharacterTrack*)pTrack;

					if (pCharTrack->GetAnimationLayerIndex() >= 0)   // If the track has an animation layer specified,
					{
						assert(pCharTrack->GetAnimationLayerIndex() < ISkeletonAnim::LayerCount);
						index = pCharTrack->GetAnimationLayerIndex();  // use it instead.
					}

					if (pEntity)
					{
						ICharacterInstance* pCharacter = pEntity->GetCharacter(0);

						if (pCharacter)
						{
							AnimateCharacterTrack(pCharTrack, animContext, index, animationTrack, m_baseAnimState, pEntity, pCharacter);
						}
					}

					if (animCharacterLayer == 0)
					{
						animCharacterLayer += ADDITIVE_LAYERS_OFFSET;
					}

					++animCharacterLayer;
					++animationTrack;
				}
			}

			break;

		case eAnimParamType_Expression:
			if (!animContext.bResetting)
			{
				CExprTrack* pExpTrack = (CExprTrack*)pTrack;
				AnimateExpressionTrack(pExpTrack, animContext);
			}

			break;

		case eAnimParamType_FaceSequence:
			if (!animContext.bResetting)
			{
				CFaceSequenceTrack* pSelTrack = (CFaceSequenceTrack*)pTrack;
				AnimateFacialSequence(pSelTrack, animContext);
			}

			break;

		case eAnimParamType_LookAt:
			if (!animContext.bResetting)
			{
				CLookAtTrack* pSelTrack = (CLookAtTrack*)pTrack;
				AnimateLookAt(pSelTrack, animContext);
			}

			break;

		case eAnimParamType_ByString:
			if (!animContext.bResetting)
			{
				bScriptPropertyModified = AnimateScriptTableProperty(pTrack, animContext, paramType.GetName()) || bScriptPropertyModified;
			}

			break;
		}
	}

	if (bApplyNoise)
	{
		// Position noise
		if (m_posNoise.m_amp != 0)
		{
			pos += m_posNoise.Get(animContext.time);

			if (pos != m_pos)
			{
				entityUpdateFlags |= eUpdateEntity_Position;
			}
		}

		// Rotation noise
		if (m_rotNoise.m_amp != 0)
		{
			Ang3 angles = Ang3::GetAnglesXYZ(Matrix33(rotate)) * 180.0f / gf_PI;
			Vec3 noiseVec = m_rotNoise.Get(animContext.time);
			angles.x += noiseVec.x;
			angles.y += noiseVec.y;
			angles.z += noiseVec.z;
			rotate.SetRotationXYZ(angles * gf_PI / 180.0f);

			if (rotate != m_rotate)
			{
				entityUpdateFlags |= eUpdateEntity_Rotation;
			}
		}
	}

	if (bScriptPropertyModified)
	{
		if (pEntity->GetScriptTable()->HaveValue("OnPropertyChange"))
			Script::CallMethod(pEntity->GetScriptTable(), "OnPropertyChange");
#ifdef CHECK_FOR_TOO_MANY_ONPROPERTY_SCRIPT_CALLS
		m_OnPropertyCalls++;
#endif
	}

	// Update physics status if needed.
	IAnimTrack* pPhysicalizeTrack = GetTrackForParameter(eAnimParamType_Physicalize);

	if (pPhysicalizeTrack)
	{
		const bool bUsePhysics = stl::get<bool>(pPhysicalizeTrack->GetValue(m_time));
		EnableEntityPhysics(bUsePhysics);
	}

	// [*DavidR | 6/Oct/2010] Positioning an entity when ragdollized will not look good at all :)
	// Note: Articulated != ragdoll in some cases. And kinematic(mass 0) articulated entities could allow
	// repositioning, but positioning a kinematic articulated entity by TrackView isn't something we expect
	// to happen very much compared with regular ragdoll
	const bool bRagdoll = pEntity->GetPhysics() && (pEntity->GetPhysics()->GetType() == PE_ARTICULATED);

	if (!bRagdoll && (entityUpdateFlags || bScaleModified || bScriptPropertyModified || (m_target != NULL)))
	{
		m_pos = pos;
		m_rotate = rotate;
		m_scale = scale;

		m_bIgnoreSetParam = true; // Prevents feedback change of track.
		// no callback specified, so lets move the entity directly
		if (!m_pOwner)
		{
			if (entityUpdateFlags)
			{
				UpdateEntityPosRotVel(m_pos, m_rotate, animContext.time == SAnimTime(0), (EUpdateEntityFlags)entityUpdateFlags, animContext.time);
			}

			if (bScaleModified)
			{
				pEntity->SetScale(m_scale, ENTITY_XFORM_TRACKVIEW);
			}
		}

		m_bIgnoreSetParam = false; // Prevents feedback change of track.
	}

	m_time = animContext.time;

	UpdateTargetCamera(pEntity, rotate);

	if (m_pOwner)
	{
		m_bIgnoreSetParam = true; // Prevents feedback change of track.
		m_pOwner->OnNodeAnimated(this);
		m_bIgnoreSetParam = false;
	}
}

void CAnimEntityNode::ReleaseSounds()
{
	REINST(Stop all playing sounds)
	//// stop all sounds
	//if (!m_soundInfo.empty())
	//{
	//	std::vector<SSoundInfo>::iterator Iter(m_soundInfo.begin());
	//	std::vector<SSoundInfo>::const_iterator const IterEnd(m_soundInfo.end());

	//	for (; Iter != IterEnd; ++Iter)
	//	{
	//		SSoundInfo& rSoundInfo = *Iter;

	//		if (rSoundInfo.nSoundID != INVALID_SOUNDID)
	//		{
	//			if (_smart_ptr<ISound> const pSound = gEnv->pAudioSystem->GetSound(rSoundInfo.nSoundID))
	//			{
	//				pSound->Stop();
	//			}
	//		}

	//		rSoundInfo.Reset();
	//	}
	//}
}

void CAnimEntityNode::OnReset()
{
	m_EntityId = 0;
	m_lastEntityKey = -1;
	m_lastDrsSignalKey = -1;

	m_audioFileTracks.clear();
	m_audioSwitchTracks.clear();
	m_audioTriggerTracks.clear();
	m_audioParameterTracks.clear();

	m_baseAnimState.m_lastAnimationKeys[0][0] = -1;
	m_baseAnimState.m_lastAnimationKeys[0][1] = -1;
	m_baseAnimState.m_lastAnimationKeys[1][0] = -1;
	m_baseAnimState.m_lastAnimationKeys[1][1] = -1;
	m_baseAnimState.m_lastAnimationKeys[2][0] = -1;
	m_baseAnimState.m_lastAnimationKeys[2][1] = -1;

	m_lookAtTarget = "";
	m_lookAtEntityId = 0;
	m_allowAdditionalTransforms = true;
	m_lookPose = "";
	ReleaseSounds();
	ReleaseAllAnims();
	UpdateDynamicParams();

	m_baseAnimState.m_layerPlaysAnimation[0] = m_baseAnimState.m_layerPlaysAnimation[1] = m_baseAnimState.m_layerPlaysAnimation[2] = false;

	if (m_pOwner)
	{
		m_pOwner->OnNodeReset(this);
	}
}

void CAnimEntityNode::PrepareAnimations()
{
	// Update durations of all character animations.
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	ICharacterInstance* pCharacter = pEntity->GetCharacter(0);

	if (!pCharacter)
	{
		return;
	}

	IAnimationSet* pAnimations = pCharacter->GetIAnimationSet();

	const int trackCount = NumTracks();

	for (int paramIndex = 0; paramIndex < trackCount; paramIndex++)
	{
		CAnimParamType trackType = m_tracks[paramIndex]->GetParameterType();
		IAnimTrack* pTrack = m_tracks[paramIndex];

		if (pAnimations)
		{
			switch (trackType.GetType())
			{
			case eAnimParamType_Animation:
				{
					int numKeys = pTrack->GetNumKeys();

					for (int i = 0; i < numKeys; i++)
					{
						SCharacterKey key;
						pTrack->GetKey(i, &key);
						int animId = pAnimations->GetAnimIDByName(key.m_animation);

						if (animId >= 0)
						{
							float duration = pAnimations->GetDuration_sec(animId);

							if (duration != key.m_animDuration)
							{
								key.m_animDuration = duration;
								pTrack->SetKey(i, &key);
							}
						}
					}
					break;
				}

			case eAnimParamType_FaceSequence:
				{
					IFacialInstance* pFaceInstance = pCharacter->GetFacialInstance();

					if (pFaceInstance)
					{
						SFaceSequenceKey key;
						int numKeys = pTrack->GetNumKeys();

						for (int i = 0; i < numKeys; i++)
						{
							pTrack->GetKey(i, &key);
							_smart_ptr<IFacialAnimSequence> pSequence = LoadFacialSequence(key.m_selection);

							if (pSequence)
							{
								key.m_duration = SAnimTime(pSequence->GetTimeRange().Length());
								pTrack->SetKey(i, &key);
							}
						}
					}
					break;
				}
			}
		}
	}
}

IFacialAnimSequence* CAnimEntityNode::LoadFacialSequence(const char* sequenceName)
{
	FacialSequenceMap::iterator loadedSequencePosition = m_facialSequences.find(CONST_TEMP_STRING(sequenceName));

	if (loadedSequencePosition == m_facialSequences.end())
	{
		IEntity* pEntity = GetEntity();
		ICharacterInstance* pCharacter = (pEntity ? pEntity->GetCharacter(0) : 0);
		IFacialInstance* pFaceInstance = (pCharacter ? pCharacter->GetFacialInstance() : 0);
		IFacialAnimSequence* pSequence = (pFaceInstance ? pFaceInstance->LoadSequence(sequenceName, false) : 0);

		// Add the sequence to the cache, even if the pointer is 0 - stop us from continually trying to load a missing sequence.
		loadedSequencePosition = m_facialSequences.insert(std::make_pair(sequenceName, pSequence)).first;
	}

	return (loadedSequencePosition != m_facialSequences.end() ?
	        (*loadedSequencePosition).second :
	        _smart_ptr<IFacialAnimSequence>(0));
}

void CAnimEntityNode::ReleaseAllFacialSequences()
{
	IEntity* pEntity = GetEntity();
	ICharacterInstance* pCharacter = (pEntity ? pEntity->GetCharacter(0) : 0);
	IFacialInstance* pFaceInstance = (pCharacter ? pCharacter->GetFacialInstance() : 0);

	if (pFaceInstance)
	{
		pFaceInstance->StopSequence(eFacialSequenceLayer_Trackview);
	}
}

void CAnimEntityNode::EnableProceduralFacialAnimation(bool enable)
{
	IEntity* pEntity = GetEntity();
	ICharacterInstance* pCharacter = (pEntity ? pEntity->GetCharacter(0) : 0);
	IFacialInstance* pFaceInstance = (pCharacter ? pCharacter->GetFacialInstance() : 0);

	if (pFaceInstance)
	{
		pFaceInstance->EnableProceduralFacialAnimation(enable);
	}
}

bool CAnimEntityNode::GetProceduralFacialAnimationEnabled()
{
	IEntity* pEntity = GetEntity();
	ICharacterInstance* pCharacter = (pEntity ? pEntity->GetCharacter(0) : 0);
	const IFacialInstance* pFaceInstance = (pCharacter ? pCharacter->GetFacialInstance() : 0);

	if (pFaceInstance)
	{
		return pFaceInstance->IsProceduralFacialAnimationEnabled();
	}

	return false;
}

void CAnimEntityNode::Activate(bool bActivate)
{
	CAnimNode::Activate(bActivate);

	if (bActivate)
	{
#ifdef CHECK_FOR_TOO_MANY_ONPROPERTY_SCRIPT_CALLS
		m_OnPropertyCalls = 0;
#endif
		PrepareAnimations();
		m_proceduralFacialAnimationEnabledOld = GetProceduralFacialAnimationEnabled();
		EnableProceduralFacialAnimation(false);
	}
	else
	{
#ifdef CHECK_FOR_TOO_MANY_ONPROPERTY_SCRIPT_CALLS
		IEntity* pEntity = GetEntity();

		if (m_OnPropertyCalls > 30) // arbitrary amount
		{
			CryWarning(VALIDATOR_MODULE_MOVIE, VALIDATOR_ERROR, "Entity: %s. A trackview movie has called lua function 'OnPropertyChange' too "
			                                                    "many (%d) times .This is a performance issue. Adding Some custom management in the entity lua code will fix the issue",
			           pEntity ? pEntity->GetName() : "<UNKNOWN", m_OnPropertyCalls);
		}

#endif

		// Release lock on preloaded Animations so no locks can leak
		m_animationCacher.ClearCache();

		EnableProceduralFacialAnimation(m_proceduralFacialAnimationEnabledOld);
	}

	m_lookAtTarget = "";
	m_lookAtEntityId = 0;
	m_allowAdditionalTransforms = true;
	m_lookPose = "";
};

void CAnimEntityNode::OnStart()
{
	m_bIsAnimDriven = false;

	IAnimTrack* pPhysicalizeTrack = GetTrackForParameter(eAnimParamType_Physicalize);

	if (pPhysicalizeTrack)
	{
		m_bInitialPhysicsStatus = false;

		if (IEntity* pEntity = GetEntity())
		{
			m_bInitialPhysicsStatus = pEntity->IsPhysicsEnabled();
		}

		EnableEntityPhysics(false);
	}
}

void CAnimEntityNode::OnPause()
{
	ReleaseSounds();
	StopEntity();
}

void CAnimEntityNode::OnStop()
{
	ReleaseSounds();
	StopEntity();
}

void CAnimEntityNode::UpdateEntityPosRotVel(const Vec3& targetPos, const Quat& targetRot, const bool initialState, const int flags, SAnimTime fTime)
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	IAnimTrack* pPhysicsDrivenTrack = GetTrackForParameter(eAnimParamType_PhysicsDriven);
	bool bPhysicsDriven = false;

	if (pPhysicsDrivenTrack)
	{
		bPhysicsDriven = stl::get<bool>(pPhysicsDrivenTrack->GetValue(m_time));
	}

	IPhysicalEntity* pPhysEnt = (initialState || pEntity->GetParent()) ? NULL : pEntity->GetPhysics();

	if (bPhysicsDriven && pPhysEnt && pPhysEnt->GetType() != PE_STATIC)
	{
		pe_status_dynamics dynamics;
		pPhysEnt->GetStatus(&dynamics);

		pe_status_pos psp;
		pPhysEnt->GetStatus(&psp);

		const float rTimeStep = 1.f / max(gEnv->pTimer->GetFrameTime(), 100e-3f);
		pe_action_set_velocity setVel;
		setVel.v = dynamics.v;
		setVel.w = dynamics.w;

		if (flags & (eUpdateEntity_Animation | eUpdateEntity_Position))
		{
			setVel.v = (targetPos - psp.pos) * (rTimeStep * movie_physicalentity_animation_lerp);
		}

		if (flags & (eUpdateEntity_Animation | eUpdateEntity_Rotation))
		{
			const Quat dq = targetRot * psp.q.GetInverted();
			setVel.w = dq.v * (2.f * rTimeStep * movie_physicalentity_animation_lerp); //This is an approximation
		}

		pPhysEnt->Action(&setVel);
	}
	else
	{
		if (flags & eUpdateEntity_Animation || ((flags & eUpdateEntity_Position) && (flags & eUpdateEntity_Rotation)))
		{
			const Vec3& scale = pEntity->GetScale();
			pEntity->SetPosRotScale(targetPos, targetRot, scale, ENTITY_XFORM_TRACKVIEW);
		}
		else
		{
			if (flags & eUpdateEntity_Position)
			{
				pEntity->SetPos(targetPos, ENTITY_XFORM_TRACKVIEW);
			}

			if (flags & eUpdateEntity_Rotation)
			{
				pEntity->SetRotation(targetRot, ENTITY_XFORM_TRACKVIEW);
			}
		}
	}
}

void CAnimEntityNode::ApplyEventKey(CEventTrack* track, int keyIndex, SEventKey& key)
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	if (*key.m_animation) // if there is an animation
	{
		// Start playing animation.
		ICharacterInstance* pCharacter = pEntity->GetCharacter(0);

		if (pCharacter)
		{
			CryCharAnimationParams aparams;
			aparams.m_fTransTime = 0.15f;

			aparams.m_fTransTime = 0.0f;
			aparams.m_nFlags = CA_TRACK_VIEW_EXCLUSIVE;

			ISkeletonAnim* pSkeletonAnimation = pCharacter->GetISkeletonAnim();

			aparams.m_nLayerID = 0;
			pSkeletonAnimation->StartAnimation(key.m_animation, aparams);

			IAnimationSet* pAnimations = pCharacter->GetIAnimationSet();
			assert(pAnimations);
			//float duration = pAnimations->GetLength( key.animation );

			int animId = pAnimations->GetAnimIDByName(key.m_animation);

			if (animId >= 0)
			{
				const float duration = pAnimations->GetDuration_sec(animId);
				if (duration != key.m_duration.ToFloat())
				{
					key.m_duration = SAnimTime(duration);
					track->SetKey(keyIndex, &key);
				}
			}
		}
	}

	if (key.m_event.size() > 0) // if there's an event
	{
		// Fire event on Entity.
		IEntityScriptComponent* pScriptProxy = (IEntityScriptComponent*)pEntity->GetProxy(ENTITY_PROXY_SCRIPT);

		if (pScriptProxy)
		{
			// Find event
			int type = -1;

			if (IEntityClass* pClass = pEntity->GetClass())
			{
				const int count = pClass->GetEventCount();
				IEntityClass::SEventInfo info;

				for (int i = 0; i < count; ++i)
				{
					info = pClass->GetEventInfo(i);

					if (strcmp(key.m_event, info.name) == 0)
					{
						type = info.type;
						break;
					}
				}
			}

			// Convert value to type
			switch (type)
			{
			case IEntityClass::EVT_INT:
			case IEntityClass::EVT_FLOAT:
			case IEntityClass::EVT_ENTITY:
				pScriptProxy->CallEvent(key.m_event, (float)atof(key.m_eventValue));
				break;

			case IEntityClass::EVT_BOOL:
				pScriptProxy->CallEvent(key.m_event, atoi(key.m_eventValue) != 0 ? true : false);
				break;

			case IEntityClass::EVT_STRING:
				pScriptProxy->CallEvent(key.m_event, key.m_eventValue);
				break;

			case IEntityClass::EVT_VECTOR:
				{
					Vec3 vTemp(0, 0, 0);
					float x = 0, y = 0, z = 0;
					int res = sscanf(key.m_eventValue, "%f,%f,%f", &x, &y, &z);
					assert(res == 3);
					vTemp(x, y, z);
					pScriptProxy->CallEvent(key.m_event, vTemp);
				}
				break;

			case -1:
			default:
				pScriptProxy->CallEvent(key.m_event);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CAnimEntityNode::ApplyAudioTriggerKey(CryAudio::ControlId audioTriggerId, bool const bPlay /* = true */)
{
	if (audioTriggerId != CryAudio::InvalidControlId)
	{
		IEntity* const pEntity = GetEntity();

		// Get or create sound proxy if necessary.
		IEntityAudioComponent* const pIEntityAudioComponent = pEntity->GetOrCreateComponent<IEntityAudioComponent>();
		if (pIEntityAudioComponent)
		{
			if (bPlay)
			{
				pIEntityAudioComponent->ExecuteTrigger(audioTriggerId);
			}
			else
			{
				pIEntityAudioComponent->StopTrigger(audioTriggerId);
			}
		}
	}
}

void CAnimEntityNode::ReleaseAllAnims()
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	ICharacterInstance* pCharacter = pEntity->GetCharacter(0);

	if (!pCharacter)
	{
		return;
	}

	IAnimationSet* pAnimations = pCharacter->GetIAnimationSet();
	assert(pAnimations);

	for (TStringSetIt It = m_setAnimationSinks.begin(); It != m_setAnimationSinks.end(); ++It)
	{
		const char* pszName = (*It).c_str();
	}

	m_setAnimationSinks.clear();

	if (AnimationPlaying(m_baseAnimState))
	{
		pCharacter->GetISkeletonAnim()->SetTrackViewExclusive(0);

		pCharacter->GetISkeletonAnim()->StopAnimationsAllLayers();

		pCharacter->SetPlaybackScale(1.0000f);
		pCharacter->GetISkeletonAnim()->SetAnimationDrivenMotion(m_bWasTransRot);
		m_baseAnimState.m_layerPlaysAnimation[0] = m_baseAnimState.m_layerPlaysAnimation[1] = m_baseAnimState.m_layerPlaysAnimation[2] = false;

		NotifyEntityScript(pEntity, "OnSequenceAnimationStop");
	}

	ReleaseAllFacialSequences();
}

void CAnimEntityNode::OnEndAnimation(const char* sAnimation)
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	TStringSetIt iter = m_setAnimationSinks.find(sAnimation);

	if (iter == m_setAnimationSinks.end())
	{
		return; // this anim was not started by us...
	}

	m_setAnimationSinks.erase(iter);

	ICharacterInstance* pCharacter = pEntity->GetCharacter(0);

	if (!pCharacter)
	{
		return;
	}

	IAnimationSet* pAnimations = pCharacter->GetIAnimationSet();
	assert(pAnimations);
}

// return active keys ( maximum: 2 in case of overlap ) in the right order
static int32 GetActiveKeys(int32 activeKeys[], SAnimTime ectime, CCharacterTrack* track)
{
	int32 numActiveKeys = 0;

	SAnimTime time;
	bool swap = false;

	int32 numKeys = track->GetNumKeys();

	for (int i = 0; i < numKeys; ++i)
	{
		SCharacterKey key;
		track->GetKey(i, &key);

		if ((key.m_time <= ectime) && (key.m_time + SAnimTime(track->GetKeyDuration(i)) > ectime))
		{
			activeKeys[numActiveKeys] = i;

			// last key had a bigger
			if (time > key.m_time)
			{
				swap = true;
			}

			time = key.m_time;
			numActiveKeys++;

			// not more than 2 concurrent keys allowed
			if (numActiveKeys == 2)
			{
				break;
			}
		}
	}

	if (swap)
	{
		int32 temp = activeKeys[1];
		activeKeys[1] = activeKeys[0];
		activeKeys[0] = temp;
	}

	// use the first if we are before the first key
	if (!numActiveKeys && track->GetNumKeys())
	{
		SCharacterKey key0;
		track->GetKey(0, &key0);

		if (ectime < key0.m_time)
		{
			numActiveKeys = 1;
			activeKeys[0] = 0;
		}
	}

	return numActiveKeys;
}

static bool GetNearestKeys(int32 activeKeys[], SAnimTime ectime, CCharacterTrack* track, int32& numActiveKeys)
{
	SAnimTime nearestLeft = SAnimTime::Min();
	SAnimTime nearestRight = SAnimTime::Max();

	int32 left = -1, right = -1;

	int32 numKeys = track->GetNumKeys();

	for (int i = 0; i < numKeys; ++i)
	{
		SCharacterKey key;
		track->GetKey(i, &key);

		SAnimTime endTime = key.m_time + SAnimTime(track->GetKeyDuration(i));

		if ((endTime < ectime) && (endTime > nearestLeft))
		{
			left = i;
			nearestLeft = endTime;
		}

		if ((key.m_time > ectime) && (key.m_time < nearestRight))
		{
			right = i;
			nearestRight = key.m_time;
		}
	}

	if (left != -1)
	{
		SCharacterKey possibleKey;
		track->GetKey(left, &possibleKey);

		assert(possibleKey.m_bLoop == false);

		if (!possibleKey.m_bBlendGap)
		{
			// if no blending is set
			// set animation to the last frame of the nearest animation
			// to the left
			activeKeys[0] = left;
			numActiveKeys = 1;
			return false;
		}
	}

	// found gap-blend neighbors
	if ((right != -1) && (left != -1))
	{
		activeKeys[0] = left;
		activeKeys[1] = right;
		return true;
	}

	return false;
}

static bool HaveKeysChanged(int32 activeKeys[], int32 previousKeys[])
{
	return !((activeKeys[0] == previousKeys[0]) && (activeKeys[1] == previousKeys[1]));
}

void CAnimEntityNode::AnimateCharacterTrack(class CCharacterTrack* pTrack, SAnimContext& animContext, int layer, int trackIndex, SAnimState& animState, IEntity* pEntity, ICharacterInstance* pCharacter)
{
	ISystem* pISystem = GetISystem();
	IRenderer* pIRenderer = gEnv->pRenderer;
	IRenderAuxGeom* pAuxGeom = pIRenderer->GetIRenderAuxGeom();

	pTrack->GetActiveKey(animContext.time, 0);  // To sort keys

	int32 activeKeys[2] = { -1, -1 };
	int32 numActiveKeys = GetActiveKeys(activeKeys, animContext.time, pTrack);

	// decide if blending gap is asked for or possible
	bool blendGap = false;

	if (numActiveKeys == 0)
	{
		blendGap = GetNearestKeys(activeKeys, animContext.time, pTrack, numActiveKeys);
	}

	bool bAnyChange = CheckTimeJumpingOrOtherChanges(animContext, activeKeys, numActiveKeys, pCharacter, layer, trackIndex, animState);

	// the used keys have changed - be it overlapping, single, nearest or none at all
	if (bAnyChange)
	{
		if (animState.m_bTimeJumped[trackIndex] == false) // In case of the time-jumped, the existing animation must not be cleared.
		{
			pCharacter->GetISkeletonAnim()->ClearFIFOLayer(layer);
		}

		animState.m_layerPlaysAnimation[trackIndex] = false;

		// rebuild the transition queue
		for (int32 i = 0; i < 2; ++i)
		{
			int32 k = activeKeys[i];

			if (k < 0)
			{
				break;
			}

			SCharacterKey key;
			pTrack->GetKey(k, &key);

			float t = (animContext.time - key.m_time).ToFloat();
			t = key.m_startTime + t * key.m_speed;

			if (key.m_animation[0])
			{
				// retrieve the animation collection for the model
				IAnimationSet* pAnimations = pCharacter->GetIAnimationSet();
				assert(pAnimations);

				if (key.m_bUnload)
				{
					m_setAnimationSinks.insert(TStringSetIt::value_type(key.m_animation));
				}

				if (pCharacter->GetISkeletonAnim()->GetAnimationDrivenMotion() && (!AnimationPlaying(animState)))
				{
					m_bWasTransRot = true;
				}

				pCharacter->GetISkeletonAnim()->SetAnimationDrivenMotion(key.m_bInPlace ? 1 : 0);
				pCharacter->GetISkeletonAnim()->SetTrackViewExclusive(1);

				// Start playing animation.
				CryCharAnimationParams aparams;
				aparams.m_nFlags = CA_TRACK_VIEW_EXCLUSIVE | CA_ALLOW_ANIM_RESTART;

				if (key.m_bLoop)
				{
					aparams.m_nFlags |= CA_LOOP_ANIMATION;
				}

				aparams.m_nLayerID = layer;
				aparams.m_fTransTime = -1.0f;

				pCharacter->GetISkeletonAnim()->StartAnimation(key.m_animation, aparams);

				NotifyEntityScript(pEntity, "OnSequenceAnimationStart", key.m_animation);

				animState.m_layerPlaysAnimation[trackIndex] = true;

				// fix duration?
				int animId = pAnimations->GetAnimIDByName(key.m_animation);

				if (animId >= 0)
				{
					float duration = pAnimations->GetDuration_sec(animId);

					if (key.m_animDuration != duration)
					{
						key.m_animDuration = duration;
						pTrack->SetKey(k, &key);
					}
				}
			}
		}

		animState.m_lastAnimationKeys[trackIndex][0] = activeKeys[0];
		animState.m_lastAnimationKeys[trackIndex][1] = activeKeys[1];

		if (!AnimationPlaying(animState))
		{
			// There is no animation left playing - exit TrackViewExclusive mode
			pCharacter->GetISkeletonAnim()->SetTrackViewExclusive(0);
			pCharacter->GetISkeletonAnim()->StopAnimationsAllLayers();
			pCharacter->SetPlaybackScale(1.0000f);
			pCharacter->GetISkeletonAnim()->SetAnimationDrivenMotion(m_bWasTransRot);
			m_bIsAnimDriven = false;
			NotifyEntityScript(pEntity, "OnSequenceAnimationStop");
			return;
		}
	}

	if (animState.m_layerPlaysAnimation[trackIndex])
	{
		if (animState.m_bTimeJumped[trackIndex])
		{
			assert(numActiveKeys == 1 && activeKeys[0] >= 0 && pCharacter->GetISkeletonAnim()->GetNumAnimsInFIFO(layer) == 2);
			UpdateAnimTimeJumped(activeKeys[0], pTrack, animContext.time, pCharacter, layer, !bAnyChange, trackIndex, animState);
		}
		// regular one- or two-animation(s) case
		else if (numActiveKeys > 0)
		{
			UpdateAnimRegular(numActiveKeys, activeKeys, pTrack, animContext.time, pCharacter, layer, !bAnyChange);
		}
		// blend gap
		else if (blendGap)
		{
			UpdateAnimBlendGap(activeKeys, pTrack, animContext.time, pCharacter, layer);
		}
	}
}

void CAnimEntityNode::StopExpressions()
{
	if (m_setExpressions.empty())
	{
		return;
	}

	m_setExpressions.clear();
}

void CAnimEntityNode::AnimateExpressionTrack(CExprTrack* pTrack, SAnimContext& animContext)
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	SExprKey exprKey;
	int nKeys = pTrack->GetNumKeys();

	// we go through all the keys, since expressions may overlap
	for (int nKey = 0; nKey < nKeys; nKey++)
	{
		pTrack->GetKey(nKey, &exprKey);

		if (!exprKey.m_name[0])
		{
			return;
		}

		float fKeyLentgh = exprKey.m_blendIn + exprKey.m_hold + exprKey.m_blendOut;
		float fOffset = (animContext.time - exprKey.m_time).ToFloat();

		CryCharMorphParams morphParams;
		morphParams.m_fAmplitude = exprKey.m_amp;
		morphParams.m_fBlendIn = exprKey.m_blendIn;
		morphParams.m_fBlendOut = exprKey.m_blendOut;
		morphParams.m_fLength = exprKey.m_hold;
		morphParams.m_fStartTime = fOffset;

		ICharacterInstance* pInst = pEntity->GetCharacter(0);

		if (pInst)
		{
			if ((exprKey.m_time > animContext.time) || (fOffset >= fKeyLentgh))
			{
				continue;
			}
		}
	}
}

void CAnimEntityNode::AnimateFacialSequence(CFaceSequenceTrack* pTrack, SAnimContext& animContext)
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	ICharacterInstance* pCharacter = pEntity->GetCharacter(0);

	if (!pCharacter)
	{
		return;
	}

	IFacialInstance* pFaceInstance = pCharacter->GetFacialInstance();

	if (!pFaceInstance)
	{
		return;
	}

	SFaceSequenceKey key;
	int nkey = pTrack->GetActiveKey(animContext.time, &key);

	if (nkey >= 0 && nkey < pTrack->GetNumKeys())
	{
		_smart_ptr<IFacialAnimSequence> pSequence = LoadFacialSequence(key.m_selection);

		if (pSequence)
		{
			key.m_duration = SAnimTime(pSequence->GetTimeRange().Length());
			pTrack->SetKey(nkey, &key);

			SAnimTime t = animContext.time - key.m_time;

			if (t <= key.m_duration)
			{
				if (!pFaceInstance->IsPlaySequence(pSequence, eFacialSequenceLayer_Trackview))
				{
					pFaceInstance->PlaySequence(pSequence, eFacialSequenceLayer_Trackview);
					pFaceInstance->PauseSequence(eFacialSequenceLayer_Trackview, true);
				}

				pFaceInstance->SeekSequence(eFacialSequenceLayer_Trackview, t.ToFloat());
			}
			else
			{
				if (pFaceInstance->IsPlaySequence(pSequence, eFacialSequenceLayer_Trackview))
				{
					pFaceInstance->StopSequence(eFacialSequenceLayer_Trackview);
				}
			}
		}
	}
}

void CAnimEntityNode::AnimateLookAt(CLookAtTrack* pTrack, SAnimContext& animContext)
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	ICharacterInstance* pCharacter = 0;

	IAIActor* pAIActor = CastToIAIActorSafe(pEntity->GetAI());

	if (!pAIActor)
	{
		pCharacter = pEntity->GetCharacter(0);
	}

	EntityId lookAtEntityId = 0;
	bool allowAdditionalTransforms = 0;
	string lookPose = "";

	SLookAtKey key;
	int nkey = pTrack->GetActiveKey(animContext.time, &key);
	allowAdditionalTransforms = true;

	if (nkey >= 0)
	{
		lookPose = key.m_lookPose;

		if ((m_lookAtTarget[0] && !m_lookAtEntityId) || strcmp(key.m_selection, m_lookAtTarget) != 0)
		{
			m_lookAtTarget = key.m_selection;
			IEntity* pTargetEntity = NULL;

			if (strcmp(key.m_selection, "_LocalPlayer") != 0)
			{
				m_lookAtLocalPlayer = false;
				pTargetEntity = gEnv->pEntitySystem->FindEntityByName(key.m_selection);
			}
			else if (gEnv->pGameFramework)
			{
				m_lookAtLocalPlayer = true;
				IActor* pLocalActor = gEnv->pGameFramework->GetClientActor();

				if (pLocalActor)
				{
					pTargetEntity = pLocalActor->GetEntity();
				}
			}

			if (pTargetEntity)
			{
				lookAtEntityId = pTargetEntity->GetId();
			}
			else
			{
				lookAtEntityId = 0;
			}
		}
		else
		{
			lookAtEntityId = m_lookAtEntityId;
		}
	}
	else
	{
		lookAtEntityId = 0;
		m_lookAtTarget = "";
		allowAdditionalTransforms = true;
		lookPose = "";
	}

	if (m_lookAtEntityId != lookAtEntityId
	    || m_allowAdditionalTransforms != allowAdditionalTransforms
	    || m_lookPose != lookPose)
	{
		m_lookAtEntityId = lookAtEntityId;
		m_allowAdditionalTransforms = allowAdditionalTransforms;
		m_lookPose = lookPose;

		// We need to enable smoothing for the facial animations, since look ik can override them and cause snapping.
		IFacialInstance* pFacialInstance = (pCharacter ? pCharacter->GetFacialInstance() : 0);

		if (pFacialInstance)
		{
			pFacialInstance->TemporarilyEnableBoneRotationSmoothing();
		}
	}

	IEntity* pLookAtEntity = 0;

	if (m_lookAtEntityId)
	{
		pLookAtEntity = gEnv->pEntitySystem->GetEntity(m_lookAtEntityId);
	}

	if (pLookAtEntity)
	{
		Vec3 pos;

		// Override _LocalPlayer position with camera position - looks a lot better
		if (m_lookAtLocalPlayer)
		{
			pos = gEnv->pRenderer->GetCamera().GetPosition();
		}
		else
		{
			pos = pLookAtEntity->GetWorldPos();
			ICharacterInstance* pLookAtChar = pLookAtEntity->GetCharacter(0);

			if (pLookAtChar)
			{
				// Try look at head bone.
				int16 nHeadBoneId = pLookAtChar->GetIDefaultSkeleton().GetJointIDByName(HEAD_BONE_NAME);

				if (nHeadBoneId >= 0)
				{
					pos = pLookAtEntity->GetWorldTM().TransformPoint(pLookAtChar->GetISkeletonPose()->GetAbsJointByID(nHeadBoneId).t);
				}
			}
		}

		if (pCharacter)
		{
			IAnimationPoseBlenderDir* pIPoseBlenderLook = pCharacter->GetISkeletonPose()->GetIPoseBlenderLook();

			if (pIPoseBlenderLook)
			{
				uint32 lookIKLayer = DEFAULT_LOOKIK_LAYER;

				if (pTrack->GetAnimationLayerIndex() >= 0)
				{
					assert(pTrack->GetAnimationLayerIndex() < 16);
					lookIKLayer = pTrack->GetAnimationLayerIndex();
				}

				pIPoseBlenderLook->SetState(true);
				pIPoseBlenderLook->SetLayer(lookIKLayer);
				pIPoseBlenderLook->SetTarget(pos);
				pIPoseBlenderLook->SetFadeoutAngle(DEG2RAD(120.0f));
				pIPoseBlenderLook->SetPolarCoordinatesSmoothTimeSeconds(key.m_smoothTime);

				CryCharAnimationParams animationparams;
				animationparams.m_nFlags = CA_TRACK_VIEW_EXCLUSIVE | CA_LOOP_ANIMATION;
				animationparams.m_nLayerID = lookIKLayer;
				animationparams.m_fTransTime = 1.0f;
				pCharacter->GetISkeletonAnim()->StartAnimation(key.m_lookPose, animationparams);
			}
		}
	}
	else
	{
		if (pCharacter)
		{
			IAnimationPoseBlenderDir* pIPoseBlenderLook = pCharacter->GetISkeletonPose()->GetIPoseBlenderLook();

			if (pIPoseBlenderLook)
			{
				pIPoseBlenderLook->SetState(false);
			}
		}

		if (pAIActor)
		{
			pAIActor->ResetLookAt();
		}
	}
}

bool CAnimEntityNode::AnimateScriptTableProperty(IAnimTrack* pTrack, SAnimContext& animContext, const char* name)
{
	bool propertyChanged = false;
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return propertyChanged;
	}

	TScriptPropertyParamInfoMap::iterator findIter = m_nameToScriptPropertyParamInfo.find(name);

	if (findIter != m_nameToScriptPropertyParamInfo.end())
	{
		SScriptPropertyParamInfo& param = m_entityScriptPropertiesParamInfos[findIter->second];

		const TMovieSystemValue value = pTrack->GetValue(animContext.time);

		float fValue;
		Vec3 vecValue;
		bool boolValue;
		float currfValue = 0.f;
		Vec3 currVecValue(0, 0, 0);
		bool currBoolValue = false;
		int currBoolIntValue = 0;

		switch (pTrack->GetValueType())
		{
		case eAnimValue_Float:
			fValue = stl::get<float>(value);
			param.scriptTable->GetValue(param.variableName, currfValue);

			// this check actually fails much more often than it should. There is some kind of lack of precision in the trackview interpolation calculations, and often a value that should
			// be constant does small oscillations around the correct value. (0.49999, 5.00001, 0.49999, etc).
			// not a big issue as long as those changes dont trigger constant calls to OnPropertyChange, but maybe it could be worth it to see if is ok to use some range check like fcmp().
			if (currfValue != fValue)
			{
				param.scriptTable->SetValue(param.variableName, fValue);
				propertyChanged = true;
			}

			break;

		case eAnimValue_Vector:
		// fall through
		case eAnimValue_RGB:
			vecValue = stl::get<Vec3>(value);

			if (pTrack->GetValueType() == eAnimValue_RGB)
			{
				vecValue /= 255.0f;

				vecValue.x = clamp_tpl(vecValue.x, 0.0f, 1.0f);
				vecValue.y = clamp_tpl(vecValue.y, 0.0f, 1.0f);
				vecValue.z = clamp_tpl(vecValue.z, 0.0f, 1.0f);
			}

			if (param.isVectorTable)
			{
				param.scriptTable->GetValue("x", currVecValue.x) && param.scriptTable->GetValue("y", currVecValue.y) && param.scriptTable->GetValue("z", currVecValue.z);

				if (currVecValue != vecValue)
				{
					param.scriptTable->SetValue("x", vecValue.x);
					param.scriptTable->SetValue("y", vecValue.y);
					param.scriptTable->SetValue("z", vecValue.z);
					propertyChanged = true;
				}
			}
			else
			{
				param.scriptTable->GetValue(param.variableName, currVecValue);

				if (currVecValue != vecValue)
				{
					param.scriptTable->SetValue(param.variableName, vecValue);
					propertyChanged = true;
				}
			}

			break;

		case eAnimValue_Bool:
			boolValue = stl::get<bool>(value);

			if (param.scriptTable->GetValueType(param.variableName) == svtNumber)
			{
				int boolIntValue = boolValue ? 1 : 0;
				param.scriptTable->GetValue(param.variableName, currBoolIntValue);

				if (currBoolIntValue != boolIntValue)
				{
					param.scriptTable->SetValue(param.variableName, boolIntValue);
					propertyChanged = true;
				}
			}
			else
			{
				param.scriptTable->GetValue(param.variableName, currBoolValue);

				if (currBoolValue != boolValue)
				{
					param.scriptTable->SetValue(param.variableName, boolValue);
					propertyChanged = true;
				}
			}

			break;
		}

		// we give the lua code the chance to internally manage the change without needing a call to OnPropertyChange which is totally general and usually includes a recreation of all internal objects
		if (propertyChanged)
		{
			HSCRIPTFUNCTION func = 0;
			pEntity->GetScriptTable()->GetValue("OnPropertyAnimated", func);

			if (func)
			{
				bool changeTakenCareOf = false;
				Script::CallReturn(gEnv->pScriptSystem, func, pEntity->GetScriptTable(), param.variableName.c_str(), changeTakenCareOf);
				propertyChanged = !changeTakenCareOf;
				gEnv->pScriptSystem->ReleaseFunc(func);
			}
		}
	}

	return propertyChanged;
}

void CAnimEntityNode::Serialize(XmlNodeRef& xmlNode, bool bLoading, bool bLoadEmptyTracks)
{
	CAnimNode::Serialize(xmlNode, bLoading, bLoadEmptyTracks);
	IEntity* pEntity = gEnv->pEntitySystem->FindEntityByName(m_name);

	if (pEntity)
	{
		m_entityGuid = pEntity->GetGuid();
	}

	if (bLoading)
	{
		if (!pEntity)
		{
			xmlNode->getAttr("EntityGUID", m_entityGuid);
		}

		xmlNode->getAttr("EntityGUIDTarget", m_entityGuidTarget);
		xmlNode->getAttr("EntityGUIDSource", m_entityGuidSource);
	}
	else
	{
		// Save the latest object GUID obtained from the Entity System
		xmlNode->setAttr("EntityGUID", m_entityGuid);

		if (!m_entityGuidTarget.IsNull())
		{
			xmlNode->setAttr("EntityGUIDTarget", m_entityGuidTarget);
		}

		if (!m_entityGuidSource.IsNull())
		{
			xmlNode->setAttr("EntityGUIDSource", m_entityGuidSource);
		}
	}
}

void CAnimEntityNode::ApplyAnimKey(int32 keyIndex, class CCharacterTrack* track, SAnimTime ectime,
																		 ICharacterInstance* pCharacter, int layer, int animIndex, bool bAnimEvents)
{
	SCharacterKey key;
	track->GetKey(keyIndex, &key);

	float t = (ectime - key.m_time).ToFloat();
	t = key.m_startTime + t * key.m_speed;

	if ((key.GetMaxEndTime() - key.m_startTime) > 0.0f && key.m_animDuration > 0.0f)
	{
		if (t < key.m_startTime)
		{
			t = key.m_startTime;
		}
		else if (key.m_bLoop && key.m_animDuration > 0.0f)
		{
			t = fmod(t, key.m_animDuration);
		}
		else if (t > key.GetMaxEndTime())
		{
			t = key.GetMaxEndTime();
		}

		pCharacter->SetPlaybackScale(0.0000f);
		float fNormalizedTime = t / key.m_animDuration;
		assert(fNormalizedTime >= 0.0f && fNormalizedTime <= 1.0f);
		pCharacter->GetISkeletonAnim()->ManualSeekAnimationInFIFO(layer, animIndex, fNormalizedTime, bAnimEvents);
		pCharacter->GetISkeletonAnim()->SetLayerNormalizedTime(layer, fNormalizedTime);
	}
}

void CAnimEntityNode::UpdateAnimTimeJumped(int32 keyIndex, class CCharacterTrack* track,
																						 SAnimTime ectime, ICharacterInstance* pCharacter, int layer, bool bAnimEvents, int trackIndex, SAnimState& animState)
{
	f32 blendWeight = 0.0f;
	ISkeletonAnim* pISkeletonAnim = pCharacter->GetISkeletonAnim();

	if (pISkeletonAnim->GetNumAnimsInFIFO(layer) == 2)
	{
		SCharacterKey key;
		track->GetKey(keyIndex, &key);

		float transitionTime = clamp_tpl(track->GetKeyDuration(keyIndex), FLT_EPSILON, movie_timeJumpTransitionTime);
		blendWeight = (ectime.ToFloat() - animState.m_jumpTime[trackIndex]) / transitionTime;
		assert(0 <= blendWeight && blendWeight <= 1.0f);

		if (blendWeight > 1.0f)
		{
			blendWeight = 1.0f;
		}
		else if (blendWeight < 0)
		{
			blendWeight = 0;
		}

		pCharacter->GetISkeletonAnim()->SetTrackViewMixingWeight(layer, blendWeight);
	}
	else
	{
		pCharacter->GetISkeletonAnim()->SetTrackViewMixingWeight(layer, 0);
	}

	ApplyAnimKey(keyIndex, track, ectime, pCharacter, layer, 1, bAnimEvents);
}

void CAnimEntityNode::UpdateAnimRegular(int32 numActiveKeys, int32 activeKeys[], class CCharacterTrack* track, SAnimTime ectime, ICharacterInstance* pCharacter, int layer, bool bAnimEvents)
{
	// cross-fade
	if (numActiveKeys == 2)
	{
		SCharacterKey key1, key2;
		track->GetKey(activeKeys[0], &key1);

		track->GetKey(activeKeys[1], &key2);

		const SAnimTime key1EndTime = key1.m_time + SAnimTime(track->GetKeyDuration(activeKeys[0]));
		const SAnimTime t0 = key1EndTime - key2.m_time;
		const SAnimTime t = ectime - key2.m_time;
		const f32 blendWeight = (key1EndTime == key2.m_time) ? 1.0f : (t / t0).ToFloat();

		assert(0 <= blendWeight && blendWeight <= 1.0f);
		pCharacter->GetISkeletonAnim()->SetTrackViewMixingWeight(layer, blendWeight);
	}

	for (int32 i = 0; i < 2; ++i)
	{
		if (activeKeys[i] < 0)
		{
			break;
		}

		ApplyAnimKey(activeKeys[i], track, ectime, pCharacter, layer, i, bAnimEvents);
	}
}

void CAnimEntityNode::UpdateAnimBlendGap(int32 activeKeys[], class CCharacterTrack* track, SAnimTime ectime, ICharacterInstance* pCharacter, int layer)
{
	f32 blendWeight = 0.0f;
	SCharacterKey key1, key2;
	track->GetKey(activeKeys[0], &key1);
	track->GetKey(activeKeys[1], &key2);

	SAnimTime key1EndTime = key1.m_time + SAnimTime(track->GetKeyDuration(activeKeys[0]));
	SAnimTime t0 = key2.m_time - key1EndTime;
	SAnimTime t = ectime - key1EndTime;

	if (key1EndTime == key2.m_time)
	{
		blendWeight = 1.0f;
	}
	else
	{
		blendWeight = (t / t0).ToFloat();
	}

	pCharacter->SetPlaybackScale(0.0000f);
	f32 endTimeNorm = 1.0f;

	if (key1.m_animDuration > 0.0f)
	{
		endTimeNorm = key1.GetMaxEndTime() / key1.m_animDuration;
	}

	assert(endTimeNorm >= 0.0f && endTimeNorm <= 1.0f);
	pCharacter->GetISkeletonAnim()->ManualSeekAnimationInFIFO(layer, 0, endTimeNorm, false);
	f32 startTimeNorm = 0.0f;

	if (key2.m_animDuration > 0.0f)
	{
		startTimeNorm = key2.m_startTime / key2.m_animDuration;
	}

	assert(startTimeNorm >= 0.0f && startTimeNorm <= 1.0f);
	pCharacter->GetISkeletonAnim()->ManualSeekAnimationInFIFO(layer, 1, startTimeNorm, false);
	pCharacter->GetISkeletonAnim()->SetTrackViewMixingWeight(layer, blendWeight);
}

bool CAnimEntityNode::CheckTimeJumpingOrOtherChanges(const SAnimContext& animContext, int32 activeKeys[], int32 numActiveKeys,
                                                     ICharacterInstance* pCharacter, int layer, int trackIndex, SAnimState& animState)
{
	bool bEditing = gEnv->IsEditor() && gEnv->IsEditorGameMode() == false;
	bool bJustJumped = animContext.bSingleFrame && bEditing == false;

	if (bJustJumped)
	{
		animState.m_jumpTime[trackIndex] = animContext.time.ToFloat();
	}

	bool bKeysChanged = HaveKeysChanged(activeKeys, animState.m_lastAnimationKeys[trackIndex]);
	bool bAnyChange = bKeysChanged || bJustJumped;

	if (animState.m_bTimeJumped[trackIndex])
	{
		const bool bJumpBlendingDone = (animContext.time.ToFloat() - animState.m_jumpTime[trackIndex]) > movie_timeJumpTransitionTime;

		bAnyChange = bAnyChange || bJumpBlendingDone;

		if (bAnyChange)
		{
			animState.m_bTimeJumped[trackIndex] = false;
			animState.m_jumpTime[trackIndex] = 0;
		}
	}
	else if (animState.m_bTimeJumped[trackIndex] == false)
	{
		animState.m_bTimeJumped[trackIndex] = bJustJumped;

		if (animState.m_bTimeJumped[trackIndex])
		{
			if (numActiveKeys != 1)
			{
				// The transition blending in a time-jumped case only makes sense
				// when there is a single active animation at that point of time.
				animState.m_bTimeJumped[trackIndex] = false;
			}

			if (pCharacter->GetISkeletonAnim()->GetNumAnimsInFIFO(layer) != 1)
			{
				// The transition blending in a time-jumped case only makes sense
				// where there has been a single animation going on for the character.
				animState.m_bTimeJumped[trackIndex] = false;
			}
		}
	}

	return bAnyChange;
}

void CAnimEntityNode::PrecacheStatic(SAnimTime time)
{
	// Update durations of all character animations.
	IEntity* pEntity = GetEntity();

	if (pEntity)
	{
		static_cast<CAnimSequence*>(GetSequence())->PrecacheEntity(pEntity);
	}

	const uint trackCount = NumTracks();

	for (int paramIndex = 0; paramIndex < trackCount; paramIndex++)
	{
		CAnimParamType paramType = m_tracks[paramIndex]->GetParameterType();
		IAnimTrack* pTrack = m_tracks[paramIndex];

		if (paramType == eAnimParamType_AudioTrigger)
		{
			// Pre-cache audio data from all tracks
			CAudioTriggerTrack const* const pAudioTriggerTrack = static_cast<CAudioTriggerTrack const* const>(pTrack);
			SAudioTriggerKey audioTriggerKey;

			int const numKeys = pTrack->GetNumKeys();
			for (int keyIndex = 0; keyIndex < numKeys; ++keyIndex)
			{
				pAudioTriggerTrack->GetKey(keyIndex, &audioTriggerKey);
			}
		}
	}
}

void CAnimEntityNode::PrecacheDynamic(SAnimTime time)
{
	// Update durations of all character animations.
	IEntity* pEntity = GetEntity();
	if (!pEntity)
	{
		return;
	}

	ICharacterInstance* pCharacter = pEntity->GetCharacter(0);
	if (!pCharacter)
	{
		return;
	}

	IAnimationSet* pAnimations = pCharacter->GetIAnimationSet();
	if (!pAnimations)
	{
		return;
	}

	int trackCount = NumTracks();

	for (int paramIndex = 0; paramIndex < trackCount; paramIndex++)
	{
		CAnimParamType trackType = m_tracks[paramIndex]->GetParameterType();
		IAnimTrack* pTrack = m_tracks[paramIndex];

		if (trackType == eAnimParamType_Animation)
		{
			int numKeys = pTrack->GetNumKeys();

			for (int i = 0; i < numKeys; i++)
			{
				SCharacterKey key;
				pTrack->GetKey(i, &key);

				// always make sure that all animation keys in the time interval
				// [time ; time + ANIMATION_KEY_PRELOAD_INTERVAL]
				// have an extra reference so they are kept in memory
				if (key.m_time < time)
				{
					int32 animID = pAnimations->GetAnimIDByName(key.m_animation);

					if (animID >= 0)
					{
						uint32 animPathCRC = pAnimations->GetFilePathCRCByAnimID(animID);
						m_animationCacher.RemoveFromCache(animPathCRC);
					}

					continue;
				}

				if (key.m_time < (time + ANIMATION_KEY_PRELOAD_INTERVAL))
				{
					int animId = pAnimations->GetAnimIDByName(key.m_animation);

					if (animId >= 0)
					{
						uint32 animPathCRC = pAnimations->GetFilePathCRCByAnimID(animId);
						m_animationCacher.AddToCache(animPathCRC);
					}

					continue;
				}
			}
		}
	}
}

Vec3 CAnimEntityNode::Noise::Get(SAnimTime time) const
{
	Vec3 noise;
	const float phase = time.ToFloat() * m_freq;
	const Vec3 phase0 = Vec3(15.0f * m_freq, 55.1f * m_freq, 101.2f * m_freq);

	noise.x = gEnv->pSystem->GetNoiseGen()->Noise1D(phase + phase0.x) * m_amp;
	noise.y = gEnv->pSystem->GetNoiseGen()->Noise1D(phase + phase0.y) * m_amp;
	noise.z = gEnv->pSystem->GetNoiseGen()->Noise1D(phase + phase0.z) * m_amp;

	return noise;
}

Vec3 CAnimEntityNode::Adjust3DSoundOffset(bool bVoice, IEntity* pEntity, Vec3& oSoundPos) const
{
	ICharacterInstance* pCharacter = pEntity->GetCharacter(0);
	Vec3 offset(0);

	if (pCharacter)
	{
		oSoundPos = pEntity->GetWorldTM() * pCharacter->GetAABB().GetCenter();

		if (bVoice == false)
		{
			offset = pCharacter->GetAABB().GetCenter();
		}
	}

	return offset;
}

void CAnimEntityNode::StopEntity()
{
	IEntity* pEntity = GetEntity();

	if (!pEntity)
	{
		return;
	}

	IPhysicalEntity* pPhysEntity = pEntity->GetParent() ? NULL : pEntity->GetPhysics();

	if (pPhysEntity)
	{
		int entityUpdateFlags = 0;

		int trackCount = NumTracks();

		for (int paramIndex = 0; paramIndex < trackCount; paramIndex++)
		{
			IAnimTrack* pTrack = m_tracks[paramIndex];

			if (pTrack && pTrack->GetNumKeys() > 0)
			{
				CAnimParamType paramType = pTrack->GetParameterType();

				switch (paramType.GetType())
				{
				case eAnimParamType_Position:
					entityUpdateFlags |= eUpdateEntity_Position;
					break;

				case eAnimParamType_Rotation:
					entityUpdateFlags |= eUpdateEntity_Rotation;
					break;
				}
			}

			pe_status_dynamics dynamics;
			pPhysEntity->GetStatus(&dynamics);

			pe_action_set_velocity setVel;
			setVel.v = (entityUpdateFlags & eUpdateEntity_Position) ? Vec3(ZERO) : dynamics.v;
			setVel.w = (entityUpdateFlags & eUpdateEntity_Rotation) ? Vec3(ZERO) : dynamics.w;
			pPhysEntity->Action(&setVel);
		}
	}

	IAnimTrack* pPhysicalizeTrack = GetTrackForParameter(eAnimParamType_Physicalize);

	if (pPhysicalizeTrack)
	{
		EnableEntityPhysics(m_bInitialPhysicsStatus);
	}
}

void CAnimEntityNode::UpdateTargetCamera(IEntity* pEntity, const Quat& rotation)
{
	// TODO: This only works if either the camera or the target are directly
	// referenced in TrackView. Would be better if we could move this to
	// the camera proxy in entity system or similar.

	IEntity* pEntityCamera = NULL;
	IEntity* pEntityTarget = NULL;

	if (!m_entityGuidTarget.IsNull())
	{
		pEntityCamera = pEntity;

		if (!m_EntityIdTarget)
		{
			m_EntityIdTarget = gEnv->pEntitySystem->FindEntityByGuid(m_entityGuidTarget);
		}

		if (m_EntityIdTarget)
		{
			pEntityTarget = gEnv->pEntitySystem->GetEntity(m_EntityIdTarget);
		}
	}
	else if (!m_entityGuidSource.IsNull())
	{
		pEntityTarget = pEntity;

		if (!m_EntityIdSource)
		{
			m_EntityIdSource = gEnv->pEntitySystem->FindEntityByGuid(m_entityGuidSource);
		}

		if (m_EntityIdSource)
		{
			pEntityCamera = gEnv->pEntitySystem->GetEntity(m_EntityIdSource);
		}
	}

	if (pEntityCamera && pEntityTarget)
	{
		Vec3 cameraPos = pEntityCamera->GetPos();

		if (pEntityCamera->GetParent())
		{
			cameraPos = pEntityCamera->GetParent()->GetWorldTM().TransformPoint(cameraPos);
		}

		Matrix33 rotationMatrix(IDENTITY);

		if (!rotation.IsIdentity())
		{
			Ang3 angles(rotation);
			rotationMatrix = Matrix33::CreateRotationY(angles.y);
		}

		const Vec3 targetPos = pEntityTarget->GetWorldPos();

		Matrix34 tm;

		if (targetPos == cameraPos)
		{
			tm.SetTranslation(cameraPos);
		}
		else
		{
			tm = Matrix34(Matrix33::CreateRotationVDir((targetPos - cameraPos).GetNormalized()), cameraPos) * rotationMatrix;
		}

		pEntityCamera->SetWorldTM(tm, ENTITY_XFORM_TRACKVIEW);
	}
}

ILINE bool CAnimEntityNode::AnimationPlaying(const SAnimState& animState) const
{
	return animState.m_layerPlaysAnimation[0] || animState.m_layerPlaysAnimation[1] || animState.m_layerPlaysAnimation[2];
}

#undef s_nodeParamsInitialized
#undef s_nodeParams
#undef AddSupportedParam

void CAnimationCacher::AddToCache(uint32 animPathCRC)
{
	uint32 numAnims = m_cachedAnims.size();

	for (uint32 i = 0; i < numAnims; ++i)
	{
		if (m_cachedAnims[i] == animPathCRC)
		{
			return;
		}
	}

	if (gEnv->pCharacterManager->CAF_AddRef(animPathCRC))
	{
		m_cachedAnims.push_back(animPathCRC);
	}
}

void CAnimationCacher::RemoveFromCache(uint32 animPathCRC)
{
	uint32 numAnims = m_cachedAnims.size();

	for (uint32 i = 0; i < numAnims; ++i)
	{
		if (m_cachedAnims[i] == animPathCRC)
		{
			gEnv->pCharacterManager->CAF_Release(animPathCRC);

			for (uint32 j = i; j < numAnims - 1; ++j)
			{
				m_cachedAnims[j] = m_cachedAnims[j + 1];
			}

			m_cachedAnims.resize(numAnims - 1);
			return;
		}
	}
}

void CAnimationCacher::ClearCache()
{
	uint32 numAnims = m_cachedAnims.size();

	for (uint32 i = 0; i < numAnims; ++i)
	{
		gEnv->pCharacterManager->CAF_Release(m_cachedAnims[i]);
	}

	m_cachedAnims.resize(0);
}
