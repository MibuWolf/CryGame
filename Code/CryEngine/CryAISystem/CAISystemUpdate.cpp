// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

/********************************************************************
   -------------------------------------------------------------------------
   File name:   CAISystemUpdate.cpp
   $Id$
   Description: all the update related functionality here

   -------------------------------------------------------------------------
   History:
   - 2007				: Created by Kirill Bulatsev

 *********************************************************************/

#include "StdAfx.h"

#include <stdio.h>

#include <limits>
#include <map>
#include <numeric>
#include <algorithm>

#include <CryPhysics/IPhysics.h>
#include <CryEntitySystem/IEntitySystem.h>
#include <CryScriptSystem/IScriptSystem.h>
#include <Cry3DEngine/I3DEngine.h>
#include <CrySystem/ILog.h>
#include <CrySystem/File/CryFile.h>
#include <CryMath/Cry_Math.h>
#include <CrySystem/ISystem.h>
#include <CrySystem/ITimer.h>
#include <CrySystem/IConsole.h>
#include <CryNetwork/ISerialize.h>
#include <CryAISystem/IAgent.h>
#include <CryCore/Containers/VectorSet.h>

#include "CAISystem.h"
#include "CryAISystem.h"
#include "AILog.h"
#include "Free2DNavRegion.h"
#include "Graph.h"
#include "AStarSolver.h"
#include "Puppet.h"
#include "AIVehicle.h"
#include "GoalOp.h"
#include "AIPlayer.h"
#include "PipeUser.h"
#include "Leader.h"
#include "SmartObjects.h"
#include "AIActions.h"
#include "AICollision.h"
#include "AIRadialOcclusion.h"
#include "GraphNodeManager.h"
#include "CentralInterestManager.h"
#include "CodeCoverageManager.h"
#include "CodeCoverageGUI.h"
#include "StatsManager.h"
#include "TacticalPointSystem/TacticalPointSystem.h"
#include "Communication/CommunicationManager.h"
#include "Walkability/WalkabilityCacheManager.h"
#include "Navigation/NavigationSystem/NavigationSystem.h"

#include "DebugDrawContext.h"

#include "Navigation/MNM/TileGenerator.h"
#include "Navigation/MNM/NavMesh.h"

//-----------------------------------------------------------------------------------------------------------
static bool IsPuppetOnScreen(CPuppet* pPuppet)
{
	IEntity* pEntity = pPuppet->GetEntity();
	if (!pEntity)
		return false;
	IEntityRender* pIEntityRender = pEntity->GetRenderInterface();
	if (!pIEntityRender || !pIEntityRender->GetRenderNode())
		return false;
	int frameDiff = gEnv->nMainFrameID - pIEntityRender->GetRenderNode()->GetDrawFrame();
	if (frameDiff > 2)
		return false;
	return true;
}

//
//-----------------------------------------------------------------------------------------------------------
EPuppetUpdatePriority CAISystem::CalcPuppetUpdatePriority(CPuppet* pPuppet) const
{
	float fMinDistSq = std::numeric_limits<float>::max();
	bool bOnScreen = false;
	const Vec3 pos = pPuppet->GetPos();

	if (gAIEnv.configuration.eCompatibilityMode != ECCM_CRYSIS && gAIEnv.configuration.eCompatibilityMode != ECCM_CRYSIS2)
	{
		// find closest player distance (better than using the camera pos in coop / dedicated server)
		//	and check visibility against all players

		AIObjectOwners::const_iterator ai = gAIEnv.pAIObjectManager->m_Objects.find(AIOBJECT_PLAYER);
		for (; ai != gAIEnv.pAIObjectManager->m_Objects.end() && ai->first == AIOBJECT_PLAYER; ++ai)
		{
			CAIPlayer* pPlayer = CastToCAIPlayerSafe(ai->second.GetAIObject());
			if (pPlayer)
			{
				fMinDistSq = min(fMinDistSq, (pos - pPlayer->GetPos()).GetLengthSquared());

				if (!bOnScreen)
					bOnScreen = (IAIObject::eFOV_Outside != pPlayer->IsPointInFOV(pos, 2.0f));  // double range for this check, real sight range is used below.
			}
		}
	}
	else
	{
		// previous behavior retained for Crysis compatibility
		Vec3 camPos = gEnv->pSystem->GetViewCamera().GetPosition();
		fMinDistSq = Distance::Point_PointSq(camPos, pos);
		bOnScreen = IsPuppetOnScreen(pPuppet);
	}

	// Calculate the update priority of the puppet.
	const float fSightRangeSq = sqr(pPuppet->GetParameters().m_PerceptionParams.sightRange);
	const bool bInSightRange = (fMinDistSq < fSightRangeSq);
	if (bOnScreen)
	{
		return (bInSightRange ? AIPUP_VERY_HIGH : AIPUP_HIGH);
	}
	else
	{
		return (bInSightRange ? AIPUP_MED : AIPUP_LOW);
	}
}

//
//-----------------------------------------------------------------------------------------------------------
#ifdef CRYAISYSTEM_DEBUG
void CAISystem::UpdateDebugStuff()
{
	#if CRY_PLATFORM_WINDOWS

	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);
	// Delete the debug lines if the debug draw is not on.
	if ((gAIEnv.CVars.DebugDraw == 0))
	{
		m_vecDebugLines.clear();
		m_vecDebugBoxes.clear();
	}

	bool drawCover = (gAIEnv.CVars.DebugDraw != 0) && (gAIEnv.CVars.DebugDrawCover != 0);

	const char* debugHideSpotName = gAIEnv.CVars.DebugHideSpotName;
	drawCover &= (debugHideSpotName && debugHideSpotName[0] && strcmp(debugHideSpotName, "0"));

	if (drawCover)
	{
		uint32 anchorTypes[2] =
		{
			AIANCHOR_COMBAT_HIDESPOT,
			AIANCHOR_COMBAT_HIDESPOT_SECONDARY
		};

		uint32 anchorTypeCount = CRY_ARRAY_COUNT(anchorTypes);

		bool all = !stricmp(debugHideSpotName, "all");
		if (!all && (m_DebugHideObjects.size() > 1))
			m_DebugHideObjects.clear();

		const float maxDistanceSq = sqr(75.0f);
		const CCamera& camera = gEnv->pSystem->GetViewCamera();

		bool found = false;
		for (uint32 at = 0; at < anchorTypeCount; ++at)
		{
			const AIObjectOwners::const_iterator itEnd = gAIEnv.pAIObjectManager->m_Objects.end();
			for (AIObjectOwners::const_iterator it = gAIEnv.pAIObjectManager->m_Objects.find(anchorTypes[at]); it != itEnd; ++it)
			{
				CAIObject* pObject = it->second.GetAIObject();
				if (pObject->GetType() != anchorTypes[at])
					break;

				if (!pObject->IsEnabled())
					continue;

				const GraphNode* pNode = gAIEnv.pGraph->GetNode(pObject->GetNavNodeIndex());
				if (!pNode)
					continue;

				if (all)
				{
					if ((pObject->GetPos() - camera.GetPosition()).GetLengthSquared() > maxDistanceSq)
					{
						DebugHideObjectMap::iterator delIt = m_DebugHideObjects.find(pObject->GetAIObjectID());
						if (delIt != m_DebugHideObjects.end())
							m_DebugHideObjects.erase(delIt);

						continue;
					}

					// Marcio: Assume max radius for now!
					Sphere sphere(pObject->GetPos(), 12.0f);

					if (!camera.IsSphereVisible_F(sphere))
					{
						DebugHideObjectMap::iterator delIt = m_DebugHideObjects.find(pObject->GetAIObjectID());
						if (delIt != m_DebugHideObjects.end())
							m_DebugHideObjects.erase(delIt);

						continue;
					}
				}
				else
				{
					if (stricmp(pObject->GetName(), debugHideSpotName))
						continue;
				}

				SHideSpot hideSpot(SHideSpotInfo::eHST_ANCHOR, pObject->GetPos(), pObject->GetMoveDir());
				hideSpot.pNavNode = pNode;
				hideSpot.pAnchorObject = pObject;

				std::pair<DebugHideObjectMap::iterator, bool> result = m_DebugHideObjects.insert(
				  DebugHideObjectMap::value_type(pObject->GetAIObjectID(), CAIHideObject()));

				CAIHideObject& hideObject = result.first->second;
				hideObject.Set(&hideSpot, pObject->GetPos(), pObject->GetMoveDir());

				if (!all)
				{
					found = true;
					break;
				}
			}

			if (!all && found)
				break;
		}

		// clean up removed objects
		{
			DebugHideObjectMap::iterator it = m_DebugHideObjects.begin();
			DebugHideObjectMap::iterator itEnd = m_DebugHideObjects.end();

			while (it != itEnd)
			{
				CAIObject* pAIObject = gAIEnv.pObjectContainer->GetAIObject(it->first);

				bool ok = false;
				if (pAIObject && pAIObject->IsEnabled())
				{
					for (uint32 at = 0; at < anchorTypeCount; ++at)
					{
						if (pAIObject->GetType() == anchorTypes[at])
						{
							ok = true;
							break;
						}
					}
				}

				DebugHideObjectMap::iterator toErase = it++;
				if (!ok)
					m_DebugHideObjects.erase(toErase);
			}
		}

		// update and draw them
		DebugHideObjectMap::iterator it = m_DebugHideObjects.begin();
		DebugHideObjectMap::iterator itEnd = m_DebugHideObjects.end();

		for (; it != itEnd; ++it)
		{
			CAIHideObject& debugHideObject = it->second;

			if (debugHideObject.IsValid())
			{
				debugHideObject.HurryUpCoverPathGen();
				while (!debugHideObject.IsCoverPathComplete())
					debugHideObject.Update(0);
				debugHideObject.DebugDraw();
			}
		}
	}
	else
		m_DebugHideObjects.clear();

	// Update fake tracers
	if (gAIEnv.CVars.DrawFakeTracers > 0)
	{
		for (size_t i = 0; i < m_DEBUG_fakeTracers.size(); )
		{
			m_DEBUG_fakeTracers[i].t -= m_frameDeltaTime;
			if (m_DEBUG_fakeTracers[i].t < 0.0f)
			{
				m_DEBUG_fakeTracers[i] = m_DEBUG_fakeTracers.back();
				m_DEBUG_fakeTracers.pop_back();
			}
			else
			{
				++i;
			}
		}
	}
	else
	{
		m_DEBUG_fakeTracers.clear();
	}

	// Update fake hit effects
	if (gAIEnv.CVars.DrawFakeHitEffects > 0)
	{
		for (size_t i = 0; i < m_DEBUG_fakeHitEffect.size(); )
		{
			m_DEBUG_fakeHitEffect[i].t -= m_frameDeltaTime;
			if (m_DEBUG_fakeHitEffect[i].t < 0.0f)
			{
				m_DEBUG_fakeHitEffect[i] = m_DEBUG_fakeHitEffect.back();
				m_DEBUG_fakeHitEffect.pop_back();
			}
			else
			{
				++i;
			}
		}
	}
	else
	{
		m_DEBUG_fakeHitEffect.clear();
	}

	// Update fake damage indicators
	if (gAIEnv.CVars.DrawFakeDamageInd > 0)
	{
		for (unsigned i = 0; i < m_DEBUG_fakeDamageInd.size(); )
		{
			m_DEBUG_fakeDamageInd[i].t -= m_frameDeltaTime;
			if (m_DEBUG_fakeDamageInd[i].t < 0)
			{
				m_DEBUG_fakeDamageInd[i] = m_DEBUG_fakeDamageInd.back();
				m_DEBUG_fakeDamageInd.pop_back();
			}
			else
				++i;
		}
		m_DEBUG_screenFlash = max(0.0f, m_DEBUG_screenFlash - m_frameDeltaTime);
	}
	else
	{
		m_DEBUG_fakeDamageInd.clear();
		m_DEBUG_screenFlash = 0.0f;
	}

	if (gAIEnv.CVars.DebugCheckWalkability)
	{
		CAIObject* startObject = gAIEnv.pAIObjectManager->GetAIObjectByName("CheckWalkabilityTestStart");
		CAIObject* endObject = gAIEnv.pAIObjectManager->GetAIObjectByName("CheckWalkabilityTestEnd");

		if (startObject && endObject)
		{
			bool result = false;
			const float radius = gAIEnv.CVars.DebugCheckWalkabilityRadius;

			if (gAIEnv.CVars.DebugCheckWalkability == 1)
			{
				// query all entities in this path as well as their bounding boxes from physics
				AABB enclosingAABB(AABB::RESET);

				enclosingAABB.Add(startObject->GetPos());
				enclosingAABB.Add(endObject->GetPos());
				enclosingAABB.Expand(Vec3(radius));

				StaticAABBArray aabbs;
				StaticPhysEntityArray entities;

				size_t entityCount = GetPhysicalEntitiesInBox(enclosingAABB.min, enclosingAABB.max, entities, AICE_ALL);
				entities.resize(entityCount);
				aabbs.resize(entityCount);

				// get all aabbs
				pe_status_pos status;

				for (size_t i = 0; i < entityCount; ++i)
				{
					if (entities[i]->GetStatus(&status))
					{
						const Vec3 aabbMin = status.BBox[0];
						const Vec3 aabbMax = status.BBox[1];

						// some aabbs strangely return all zero from physics, thus use the enclosingAABB for the whole path
						if (aabbMin.IsZero() && aabbMax.IsZero())
							aabbs[i] = enclosingAABB;
						else
							aabbs[i] = AABB(aabbMin + status.pos, aabbMax + status.pos);
					}
				}

				result = CheckWalkability(startObject->GetPos(), endObject->GetPos(), radius, entities, aabbs);
			}
			else if (gAIEnv.CVars.DebugCheckWalkability == 2)
				result = CheckWalkability(startObject->GetPos(), endObject->GetPos(), radius);

			CDebugDrawContext dc;
			dc->Draw2dLabel(400.0f, 100.0f, 5.0f, result ? Col_Green : Col_Red, true, "%s %s!", "CheckWalkability ", result ? "passed" : "failed");
		}
	}

	if (gAIEnv.CVars.DebugWalkabilityCache)
	{
		gAIEnv.pWalkabilityCacheManager->Draw();
	}

	#endif // #if CRY_PLATFORM_WINDOWS
}
#endif //CRYAISYSTEM_DEBUG

//===================================================================
// GetUpdateAllAlways
//===================================================================
bool CAISystem::GetUpdateAllAlways() const
{
	bool updateAllAlways = gAIEnv.CVars.UpdateAllAlways != 0;
	return updateAllAlways;
}

struct SSortedPuppetB
{
	SSortedPuppetB(CPuppet* o, float dot, float d) : obj(o), weight(0.f), dot(dot), dist(d) {}
	bool operator<(const SSortedPuppetB& rhs) const { return weight < rhs.weight;  }

	float    weight, dot, dist;
	CPuppet* obj;
};

//
//-----------------------------------------------------------------------------------------------------------
void CAISystem::UpdateAmbientFire()
{
	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);

	if (gAIEnv.CVars.AmbientFireEnable == 0)
		return;

	int64 dt((GetFrameStartTime() - m_lastAmbientFireUpdateTime).GetMilliSecondsAsInt64());
	if (dt < (int)(gAIEnv.CVars.AmbientFireUpdateInterval * 1000.0f))
		return;

	// Marcio: Update ambient fire towards all players.
	for (AIObjectOwners::const_iterator ai = gAIEnv.pAIObjectManager->m_Objects.find(AIOBJECT_ACTOR), end = gAIEnv.pAIObjectManager->m_Objects.end(); ai != end && ai->first == AIOBJECT_ACTOR; ++ai)
	{
		CAIObject* obj = ai->second.GetAIObject();
		if (!obj || !obj->IsEnabled())
			continue;

		CPuppet* pPuppet = obj->CastToCPuppet();
		if (!pPuppet)
			continue;

		// By default make every AI in ambient fire, only the ones that are allowed to shoot right are set explicitly.
		pPuppet->SetAllowedToHitTarget(false);
	}

	for (AIObjectOwners::const_iterator ai = gAIEnv.pAIObjectManager->m_Objects.find(AIOBJECT_VEHICLE), end = gAIEnv.pAIObjectManager->m_Objects.end(); ai != end && ai->first == AIOBJECT_VEHICLE; ++ai)
	{
		CAIVehicle* obj = CastToCAIVehicleSafe(ai->second.GetAIObject());
		if (!obj || !obj->IsDriverInside())
			continue;
		CPuppet* pPuppet = obj->CastToCPuppet();
		if (!pPuppet)
			continue;

		// By default make every AI in ambient fire, only the ones that are allowed to shoot right are set explicitly.
		pPuppet->SetAllowedToHitTarget(false);
	}

	AIObjectOwners::const_iterator plit = gAIEnv.pAIObjectManager->m_Objects.find(AIOBJECT_PLAYER);
	for (; plit != gAIEnv.pAIObjectManager->m_Objects.end() && plit->first == AIOBJECT_PLAYER; ++plit)
	{
		CAIPlayer* pPlayer = CastToCAIPlayerSafe(plit->second.GetAIObject());
		if (!pPlayer)
			return;

		m_lastAmbientFireUpdateTime = GetFrameStartTime();

		const Vec3& playerPos = pPlayer->GetPos();
		const Vec3& playerDir = pPlayer->GetMoveDir();

		typedef std::vector<SSortedPuppetB> TShooters;
		TShooters shooters;
		shooters.reserve(32);                     // sizeof(SSortedPuppetB) = 16, 512 / 16 = 32, will go in bucket allocator

		float maxDist = 0.0f;

		// Update
		for (AIObjectOwners::const_iterator ai = gAIEnv.pAIObjectManager->m_Objects.find(AIOBJECT_ACTOR), end = gAIEnv.pAIObjectManager->m_Objects.end(); ai != end && ai->first == AIOBJECT_ACTOR; ++ai)
		{
			CAIObject* obj = ai->second.GetAIObject();
			if (!obj->IsEnabled())
				continue;
			CPuppet* pPuppet = obj->CastToCPuppet();
			if (!pPuppet)
				continue;

			CAIObject* pTarget = (CAIObject*)pPuppet->GetAttentionTarget();

			if ((pTarget != NULL) && pTarget->IsAgent())
			{
				if (pTarget == pPlayer)
				{
					Vec3 dirPlayerToPuppet = pPuppet->GetPos() - playerPos;
					float dist = dirPlayerToPuppet.NormalizeSafe();
					if (dist > 0.01f && dist < pPuppet->GetParameters().m_fAttackRange)
					{
						maxDist = max(maxDist, dist);
						float dot = playerDir.Dot(dirPlayerToPuppet);
						shooters.push_back(SSortedPuppetB(pPuppet, dot, dist));
					}
					continue;
				}
			}

			// Shooting something else than player, allow to hit.
			pPuppet->SetAllowedToHitTarget(true);
		}

		// Update
		for (AIObjectOwners::const_iterator ai = gAIEnv.pAIObjectManager->m_Objects.find(AIOBJECT_VEHICLE), end = gAIEnv.pAIObjectManager->m_Objects.end(); ai != end && ai->first == AIOBJECT_VEHICLE; ++ai)
		{
			CAIVehicle* obj = ai->second.GetAIObject()->CastToCAIVehicle();
			if (!obj->IsDriverInside())
				continue;
			CPuppet* pPuppet = obj->CastToCPuppet();
			if (!pPuppet)
				continue;

			CAIObject* pTarget = (CAIObject*)pPuppet->GetAttentionTarget();

			if ((pTarget != NULL) && pTarget->IsAgent())
			{
				if (pTarget == pPlayer)
				{
					Vec3 dirPlayerToPuppet = pPuppet->GetPos() - playerPos;
					float dist = dirPlayerToPuppet.NormalizeSafe();

					if ((dist > 0.01f) && (dist < pPuppet->GetParameters().m_fAttackRange) && pPuppet->AllowedToFire())
					{
						maxDist = max(maxDist, dist);

						float dot = playerDir.Dot(dirPlayerToPuppet);
						shooters.push_back(SSortedPuppetB(pPuppet, dot, dist));
					}

					continue;
				}
			}
			// Shooting something else than player, allow to hit.
			pPuppet->SetAllowedToHitTarget(true);
		}

		if (!shooters.empty() && maxDist > 0.01f)
		{
			// Find nearest shooter
			TShooters::iterator nearestIt = shooters.begin();
			float nearestWeight = sqr((1.0f - nearestIt->dot) / 2) * (0.3f + 0.7f * nearestIt->dist / maxDist);
			;
			for (TShooters::iterator it = shooters.begin() + 1; it != shooters.end(); ++it)
			{
				float weight = sqr((1.0f - it->dot) / 2) * (0.3f + 0.7f * it->dist / maxDist);
				if (weight < nearestWeight)
				{
					nearestWeight = weight;
					nearestIt = it;
				}
			}

			Vec3 dirToNearest = nearestIt->obj->GetPos() - playerPos;
			dirToNearest.NormalizeSafe();

			for (TShooters::iterator it = shooters.begin(); it != shooters.end(); ++it)
			{
				Vec3 dirPlayerToPuppet = it->obj->GetPos() - playerPos;
				float dist = dirPlayerToPuppet.NormalizeSafe();
				float dot = dirToNearest.Dot(dirPlayerToPuppet);
				it->weight = sqr((1.0f - dot) / 2) * (dist / maxDist);
			}

			std::sort(shooters.begin(), shooters.end());

			uint32 i = 0;
			uint32 quota = gAIEnv.CVars.AmbientFireQuota;

			for (TShooters::iterator it = shooters.begin(); it != shooters.end(); ++it)
			{
				it->obj->SetAllowedToHitTarget(true);
				if ((++i >= quota) && (it->dist > 7.5f)) // Always allow to hit if in 2.5 meter radius
					break;
			}
		}
	}
}

inline bool PuppetFloatSorter(const std::pair<CPuppet*, float>& lhs, const std::pair<CPuppet*, float>& rhs)
{
	return lhs.second < rhs.second;
}

//
//-----------------------------------------------------------------------------------------------------------
void CAISystem::UpdateExpensiveAccessoryQuota()
{
	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);

	for (unsigned i = 0; i < m_delayedExpAccessoryUpdates.size(); )
	{
		SAIDelayedExpAccessoryUpdate& update = m_delayedExpAccessoryUpdates[i];
		update.timeMs -= (int)(m_frameDeltaTime * 1000.0f);

		if (update.timeMs < 0)
		{
			update.pPuppet->SetAllowedToUseExpensiveAccessory(update.state);
			m_delayedExpAccessoryUpdates[i] = m_delayedExpAccessoryUpdates.back();
			m_delayedExpAccessoryUpdates.pop_back();
		}
		else
			++i;
	}

	const int UpdateTimeMs = 3000;
	const float fUpdateTimeMs = (float)UpdateTimeMs;

	int64 dt((GetFrameStartTime() - m_lastExpensiveAccessoryUpdateTime).GetMilliSecondsAsInt64());
	if (dt < UpdateTimeMs)
		return;

	m_lastExpensiveAccessoryUpdateTime = GetFrameStartTime();

	m_delayedExpAccessoryUpdates.clear();

	Vec3 interestPos = gEnv->pSystem->GetViewCamera().GetPosition();

	std::vector<std::pair<CPuppet*, float>> puppets;
	VectorSet<CPuppet*> stateRemoved;

	// Choose the best of each group, then best of the best.
	for (AIGroupMap::iterator it = m_mapAIGroups.begin(), itend = m_mapAIGroups.end(); it != itend; ++it)
	{
		CAIGroup* pGroup = it->second;

		CPuppet* pBestUnit = 0;
		float bestVal = FLT_MAX;

		for (TUnitList::iterator itu = pGroup->GetUnits().begin(), ituend = pGroup->GetUnits().end(); itu != ituend; ++itu)
		{
			CPuppet* pPuppet = CastToCPuppetSafe(itu->m_refUnit.GetAIObject());
			if (!pPuppet)
				continue;
			if (!pPuppet->IsEnabled())
				continue;

			if (pPuppet->IsAllowedToUseExpensiveAccessory())
				stateRemoved.insert(pPuppet);

			const int accessories = pPuppet->GetParameters().m_weaponAccessories;
			if ((accessories & (AIWEPA_COMBAT_LIGHT | AIWEPA_PATROL_LIGHT)) == 0)
				continue;

			if (pPuppet->GetProxy())
			{
				SAIWeaponInfo wi;
				pPuppet->GetProxy()->QueryWeaponInfo(wi);
				if (!wi.hasLightAccessory)
					continue;
			}

			float val = Distance::Point_Point(interestPos, pPuppet->GetPos());

			if (pPuppet->GetAttentionTargetThreat() == AITHREAT_AGGRESSIVE)
				val *= 0.5f;
			else if (pPuppet->GetAttentionTargetThreat() >= AITHREAT_INTERESTING)
				val *= 0.8f;

			if (val < bestVal)
			{
				bestVal = val;
				pBestUnit = pPuppet;
			}
		}

		if (pBestUnit)
		{
			CCCPOINT(UpdateExpensiveAccessoryQuota);
			puppets.push_back(std::make_pair(pBestUnit, bestVal));
		}
	}

	std::sort(puppets.begin(), puppets.end(), PuppetFloatSorter);

	unsigned maxExpensiveAccessories = 3;
	for (unsigned i = 0, ni = puppets.size(); i < ni && i < maxExpensiveAccessories; ++i)
	{
		stateRemoved.erase(puppets[i].first);

		if (!puppets[i].first->IsAllowedToUseExpensiveAccessory())
		{
			//		puppets[i].first->SetAllowedToUseExpensiveAccessory(true);

			int timeMs = (int)(fUpdateTimeMs * 0.5f + cry_random(0.0f, fUpdateTimeMs) * 0.4f);
			m_delayedExpAccessoryUpdates.push_back(SAIDelayedExpAccessoryUpdate(puppets[i].first, timeMs, true));
		}
	}

	for (unsigned i = 0, ni = stateRemoved.size(); i < ni; ++i)
	{
		int timeMs = (int)(cry_random(0.0f, fUpdateTimeMs) * 0.4f);
		m_delayedExpAccessoryUpdates.push_back(SAIDelayedExpAccessoryUpdate(stateRemoved[i], timeMs, false));
	}
}

//
//-----------------------------------------------------------------------------------------------------------
void CAISystem::SingleDryUpdate(CAIActor* pAIActor)
{
	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);
	if (pAIActor->IsEnabled())
		pAIActor->Update(IAIObject::EUpdateType::Dry);
	else
		pAIActor->UpdateDisabled(IAIObject::EUpdateType::Dry);
}

//
//-----------------------------------------------------------------------------------------------------------
void CAISystem::UpdateAuxSignalsMap()
{
	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);

	if (!m_mapAuxSignalsFired.empty())
	{
		MapSignalStrings::iterator mss = m_mapAuxSignalsFired.begin();
		while (mss != m_mapAuxSignalsFired.end())
		{
			(mss->second).fTimeout -= m_frameDeltaTime;
			if ((mss->second).fTimeout < 0)
			{
				MapSignalStrings::iterator mss_to_erase = mss;
				++mss;
				m_mapAuxSignalsFired.erase(mss_to_erase);
			}
			else
				++mss;
		}
	}
}
