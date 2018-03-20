// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "StdAfx.h"
#include "NavigationSystem.h"
#include "../MNM/TileGenerator.h"
#include "../MNM/NavMesh.h"
#include "DebugDrawContext.h"
#include "MNMPathfinder.h"
#include <CryThreading/IJobManager_JobDelegator.h>
#include <CryCore/Platform/CryWindows.h>

// BAI navigation file version history
// Changes in version 9
//  - Navigation volumes storage is changed:
//    * all used navigation volumes are saved (including exclusion volumes, which were missing before);
//    * navigation area names saved together with volume data;
//    * volumes stored only onces, instead of storing them together with each mesh.
// Changes in version 8
//  - struct MNM::Tile::STriangle layout is changed - now it has triangle flags
#define BAI_NAVIGATION_FILE_VERSION 9

#define MAX_NAME_LENGTH             512
#if defined(SW_NAVMESH_USE_GUID)
	#define BAI_NAVIGATION_GUID_FLAG  (1 << 31)
#else
	#define BAI_NAVIGATION_GUID_FLAG  (1 << 30)
#endif

//#pragma optimize("", off)
//#pragma inline_depth(0)

#if !defined(_RELEASE)
	#define NAVIGATION_SYSTEM_CONSOLE_AUTOCOMPLETE
#endif

#if defined NAVIGATION_SYSTEM_CONSOLE_AUTOCOMPLETE

	#include <CrySystem/IConsole.h>

struct SAgentTypeListAutoComplete : public IConsoleArgumentAutoComplete
{
	virtual int GetCount() const
	{
		assert(gAIEnv.pNavigationSystem);
		return static_cast<int>(gAIEnv.pNavigationSystem->GetAgentTypeCount());
	}

	virtual const char* GetValue(int nIndex) const
	{
		NavigationSystem* pNavigationSystem = gAIEnv.pNavigationSystem;
		assert(pNavigationSystem);
		return pNavigationSystem->GetAgentTypeName(pNavigationSystem->GetAgentTypeID(nIndex));
	}
};

SAgentTypeListAutoComplete s_agentTypeListAutoComplete;

#endif

enum { MaxTaskCountPerWorkerThread = 12, };
enum { MaxVolumeDefCopyCount = 8 }; // volume copies for access in other threads

#if NAVIGATION_SYSTEM_PC_ONLY
void GenerateTileJob(MNM::CTileGenerator::Params params, volatile uint16* state, MNM::STile* tile, uint32* hashValue)
{
	if (*state != NavigationSystem::TileTaskResult::Failed)
	{
		MNM::CTileGenerator generator;
		bool result = generator.Generate(params, *tile, hashValue);
		if (result)
			*state = NavigationSystem::TileTaskResult::Completed;
		else if (((params.flags & MNM::CTileGenerator::Params::NoHashTest) == 0) && (*hashValue == params.hashValue))
			*state = NavigationSystem::TileTaskResult::NoChanges;
		else
			*state = NavigationSystem::TileTaskResult::Failed;
	}
}

DECLARE_JOB("NavigationGeneration", NavigationGenerationJob, GenerateTileJob);
#endif

uint32 NameHash(const char* name)
{
	uint32 len = strlen(name);
	uint32 hash = 0;

	for (uint32 i = 0; i < len; ++i)
	{
		hash += (uint8)(CryStringUtils::toLowerAscii(name[i]));
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash;
}

bool ShouldBeConsideredByVoxelizer(IPhysicalEntity& physicalEntity, uint32& flags)
{
	if (physicalEntity.GetType() == PE_WHEELEDVEHICLE)
	{
		return false;
	}

	bool considerMass = (physicalEntity.GetType() == PE_RIGID);
	if (!considerMass)
	{
		pe_status_pos sp;
		if (physicalEntity.GetStatus(&sp))
		{
			considerMass = (sp.iSimClass == SC_ACTIVE_RIGID) || (sp.iSimClass == SC_SLEEPING_RIGID);
		}
	}

	if (considerMass)
	{
		pe_status_dynamics dyn;
		if (!physicalEntity.GetStatus(&dyn))
		{
			return false;
		}

		if (dyn.mass > 1e-6f)
		{
			return false;
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

NavigationSystem::NavigationSystem(const char* configName)
	: m_configName(configName)
	, m_updatesManager(this)
	, m_throughput(0.0f)
	, m_cacheHitRate(0.0f)
	, m_free(0)
	, m_state(Idle)
	, m_meshes(256)                 //Same size of meshes, off-mesh and islandConnections elements
	, m_offMeshNavigationManager(256)
	, m_islandConnectionsManager()
	, m_volumes(512)
	, m_worldAABB(AABB::RESET)
	, m_volumeDefCopy(MaxVolumeDefCopyCount, VolumeDefCopy())
	, m_listenersList(10)
	, m_users(10)
	, m_configurationVersion(0)
	, m_isNavigationUpdatePaused(false)
	, m_tileGeneratorExtensionsContainer()
{
	SetupTasks();

	m_worldMonitor = WorldMonitor(functor(m_updatesManager, &CMNMUpdatesManager::EntityChanged));

	ReloadConfig();

	m_pEditorBackgroundUpdate = new NavigationSystemBackgroundUpdate(*this);

#ifdef SEG_WORLD
	if (ISystemEventDispatcher* pSystemEventDispatcher = gEnv->pSystem->GetISystemEventDispatcher())
	{
		pSystemEventDispatcher->RegisterListener(this, "NavigationSystem");
	}
#endif

#ifdef NAVIGATION_SYSTEM_CONSOLE_AUTOCOMPLETE
	gEnv->pConsole->RegisterAutoComplete("ai_debugMNMAgentType", &s_agentTypeListAutoComplete);
#endif
}

NavigationSystem::~NavigationSystem()
{
	StopWorldMonitoring();

	Clear();

	SAFE_DELETE(m_pEditorBackgroundUpdate);

#ifdef SEG_WORLD
	if (ISystemEventDispatcher* pSystemEventDispatcher = gEnv->pSystem->GetISystemEventDispatcher())
	{
		pSystemEventDispatcher->RemoveListener(this);
	}
#endif

#ifdef NAVIGATION_SYSTEM_CONSOLE_AUTOCOMPLETE
	gEnv->pConsole->UnRegisterAutoComplete("ai_debugMNMAgentType");
#endif
}

NavigationAgentTypeID NavigationSystem::CreateAgentType(const char* name, const CreateAgentTypeParams& params)
{
	assert(name);
	AgentTypes::const_iterator it = m_agentTypes.begin();
	AgentTypes::const_iterator end = m_agentTypes.end();

	for (; it != end; ++it)
	{
		const AgentType& agentType = *it;

		if (!agentType.name.compareNoCase(name))
		{
			AIWarning("Trying to create NavigationSystem AgentType with duplicate name '%s'!", name);
			assert(0);
			return NavigationAgentTypeID();
		}
	}

	m_agentTypes.resize(m_agentTypes.size() + 1);
	AgentType& agentType = m_agentTypes.back();

	agentType.name = name;
	agentType.settings.voxelSize = params.voxelSize;
	agentType.settings.radiusVoxelCount = params.radiusVoxelCount;
	agentType.settings.climbableVoxelCount = params.climbableVoxelCount;
	agentType.settings.climbableInclineGradient = params.climbableInclineGradient;
	agentType.settings.climbableStepRatio = params.climbableStepRatio;
	agentType.meshEntityCallback = functor(ShouldBeConsideredByVoxelizer);
	agentType.settings.heightVoxelCount = params.heightVoxelCount;
	agentType.settings.maxWaterDepthVoxelCount = params.maxWaterDepthVoxelCount;

	return NavigationAgentTypeID(m_agentTypes.size());
}

NavigationAgentTypeID NavigationSystem::GetAgentTypeID(const char* name) const
{
	assert(name);

	AgentTypes::const_iterator it = m_agentTypes.begin();
	AgentTypes::const_iterator end = m_agentTypes.end();

	for (; it != end; ++it)
	{
		const AgentType& agentType = *it;

		if (!agentType.name.compareNoCase(name))
			return NavigationAgentTypeID((uint32)(std::distance(m_agentTypes.begin(), it) + 1));
	}

	return NavigationAgentTypeID();
}

NavigationAgentTypeID NavigationSystem::GetAgentTypeID(size_t index) const
{
	if (index < m_agentTypes.size())
		return NavigationAgentTypeID(index + 1);

	return NavigationAgentTypeID();
}

const char* NavigationSystem::GetAgentTypeName(NavigationAgentTypeID agentTypeID) const
{
	if (agentTypeID && agentTypeID <= m_agentTypes.size())
		return m_agentTypes[agentTypeID - 1].name.c_str();

	return 0;
}

size_t NavigationSystem::GetAgentTypeCount() const
{
	return m_agentTypes.size();
}

bool NavigationSystem::GetAgentTypeProperties(const NavigationAgentTypeID agentTypeID, AgentType& agentTypeProperties) const
{
	if (agentTypeID && agentTypeID <= m_agentTypes.size())
	{
		agentTypeProperties = m_agentTypes[agentTypeID - 1];
		return true;
	}

	return false;
}

inline size_t NearestFactor(size_t n, size_t f)
{
	while (n % f)
		++f;

	return f;
}
#ifdef SW_NAVMESH_USE_GUID
NavigationMeshID NavigationSystem::CreateMesh(const char* name, NavigationAgentTypeID agentTypeID,
                                              const CreateMeshParams& params, NavigationMeshGUID guid)
{
	MeshMap::iterator it = m_swMeshes.find(guid);
	if (it != m_swMeshes.end())
	{
		return it->second;
	}
	NavigationMeshID id(++m_nextFreeMeshId);
	id = CreateMesh(name, agentTypeID, params, id, guid);
	m_swMeshes.insert(std::pair<NavigationMeshGUID, NavigationMeshID>(guid, id));
	return id;
}
#else
NavigationMeshID NavigationSystem::CreateMesh(const char* name, NavigationAgentTypeID agentTypeID,
                                              const CreateMeshParams& params)
{
	return CreateMesh(name, agentTypeID, params, NavigationMeshID(0));
}
#endif
#ifdef SW_NAVMESH_USE_GUID
NavigationMeshID NavigationSystem::CreateMesh(const char* name, NavigationAgentTypeID agentTypeID,
                                              const CreateMeshParams& params, NavigationMeshID requestedID, NavigationMeshGUID guid)
#else
NavigationMeshID NavigationSystem::CreateMesh(const char* name, NavigationAgentTypeID agentTypeID,
                                              const CreateMeshParams& params, NavigationMeshID requestedID)
#endif
{
	assert(name && agentTypeID && agentTypeID <= m_agentTypes.size());

	if (agentTypeID && (agentTypeID <= m_agentTypes.size()))
	{
		AgentType& agentType = m_agentTypes[agentTypeID - 1];

		MNM::CNavMesh::SGridParams paramsGrid;
		paramsGrid.tileSize = params.tileSize;
		paramsGrid.voxelSize = agentType.settings.voxelSize;
		paramsGrid.tileCount = params.tileCount;

		paramsGrid.voxelSize.x = NearestFactor(paramsGrid.tileSize.x * 1000, (size_t)(paramsGrid.voxelSize.x * 1000)) * 0.001f;
		paramsGrid.voxelSize.y = NearestFactor(paramsGrid.tileSize.y * 1000, (size_t)(paramsGrid.voxelSize.y * 1000)) * 0.001f;
		paramsGrid.voxelSize.z = NearestFactor(paramsGrid.tileSize.z * 1000, (size_t)(paramsGrid.voxelSize.z * 1000)) * 0.001f;

		if (!paramsGrid.voxelSize.IsEquivalent(agentType.settings.voxelSize, 0.0001f))
		{
			AIWarning("VoxelSize (%.3f, %.3f, %.3f) for agent '%s' adjusted to (%.3f, %.3f, %.3f)"
			          " - needs to be a factor of TileSize (%d, %d, %d)",
			          agentType.settings.voxelSize.x, agentType.settings.voxelSize.y, agentType.settings.voxelSize.z,
			          agentType.name.c_str(),
			          paramsGrid.voxelSize.x, paramsGrid.voxelSize.y, paramsGrid.voxelSize.z,
			          paramsGrid.tileSize.x, paramsGrid.tileSize.y, paramsGrid.tileSize.z);
		}

		NavigationMeshID id = requestedID;
		if (requestedID == NavigationMeshID(0))
			id = NavigationMeshID(m_meshes.insert(NavigationMesh(agentTypeID)));
		else
			m_meshes.insert(requestedID, NavigationMesh(agentTypeID));
		NavigationMesh& mesh = m_meshes[id];
		mesh.navMesh.Init(paramsGrid);
		mesh.name = name;
		mesh.exclusions = agentType.exclusions;

#ifdef SW_NAVMESH_USE_GUID
		agentType.meshes.push_back(AgentType::MeshInfo(guid, id, NameHash(name)));
#else
		agentType.meshes.push_back(AgentType::MeshInfo(id, NameHash(name)));
#endif

		m_offMeshNavigationManager.OnNavigationMeshCreated(id);

		return id;
	}

	return NavigationMeshID();
}

NavigationMeshID NavigationSystem::CreateMeshForVolumeAndUpdate(const char* name, NavigationAgentTypeID agentTypeID, const CreateMeshParams& params, const NavigationVolumeID volumeID)
{
	if (volumeID && m_volumes.validate(volumeID))
	{
		NavigationMeshID meshID = CreateMesh(name, agentTypeID, params);
		SetMeshBoundaryVolume(meshID, volumeID);

		NavigationBoundingVolume& volume = m_volumes[volumeID];
		m_updatesManager.RequestMeshUpdate(meshID, volume.aabb);

		return meshID;
	}
	return NavigationMeshID();
}

void NavigationSystem::DestroyMesh(NavigationMeshID meshID)
{
	if (meshID && m_meshes.validate(meshID))
	{
		NavigationMesh& mesh = m_meshes[meshID];

		AILogComment("NavigationSystem::DestroyMesh meshID = %u '%s'", (unsigned int)meshID, mesh.name.c_str());

		for (size_t t = 0; t < m_runningTasks.size(); ++t)
		{
			if (m_results[m_runningTasks[t]].meshID == meshID)
				m_results[m_runningTasks[t]].state = TileTaskResult::Failed;
		}

		for (size_t t = 0; t < m_runningTasks.size(); ++t)
		{
			if (m_results[m_runningTasks[t]].meshID == meshID)
				gEnv->pJobManager->WaitForJob(m_results[m_runningTasks[t]].jobState);
		}

		AgentType& agentType = m_agentTypes[mesh.agentTypeID - 1];
		AgentType::Meshes::iterator it = agentType.meshes.begin();
		AgentType::Meshes::iterator end = agentType.meshes.end();

		for (; it != end; ++it)
		{
			if (it->id == meshID)
			{
				std::swap(*it, agentType.meshes.back());
				agentType.meshes.pop_back();
				break;
			}
		}

		m_updatesManager.OnMeshDestroyed(meshID);

		m_meshes.erase(meshID);

		m_offMeshNavigationManager.OnNavigationMeshDestroyed(meshID);

		ComputeWorldAABB();
	}
}

void NavigationSystem::SetMeshEntityCallback(NavigationAgentTypeID agentTypeID, const NavigationMeshEntityCallback& callback)
{
	if (agentTypeID && (agentTypeID <= m_agentTypes.size()))
	{
		AgentType& agentType = m_agentTypes[agentTypeID - 1];

		agentType.meshEntityCallback = callback;
	}
}

void NavigationSystem::AddMeshChangeCallback(NavigationAgentTypeID agentTypeID,
                                             const NavigationMeshChangeCallback& callback)
{
	if (agentTypeID && (agentTypeID <= m_agentTypes.size()))
	{
		AgentType& agentType = m_agentTypes[agentTypeID - 1];

		stl::push_back_unique(agentType.callbacks, callback);
	}
}

void NavigationSystem::RemoveMeshChangeCallback(NavigationAgentTypeID agentTypeID,
                                                const NavigationMeshChangeCallback& callback)
{
	if (agentTypeID && (agentTypeID <= m_agentTypes.size()))
	{
		AgentType& agentType = m_agentTypes[agentTypeID - 1];

		stl::find_and_erase(agentType.callbacks, callback);
	}
}

#ifdef SW_NAVMESH_USE_GUID
void NavigationSystem::SetMeshBoundaryVolume(NavigationMeshID meshID, NavigationVolumeID volumeID, NavigationVolumeGUID volumeGUID)
#else
void NavigationSystem::SetMeshBoundaryVolume(NavigationMeshID meshID, NavigationVolumeID volumeID)
#endif
{
	if (meshID && m_meshes.validate(meshID))
	{
		NavigationMesh& mesh = m_meshes[meshID];

		if (mesh.boundary)
		{
			++mesh.version;
		}

		if (!m_volumes.validate(volumeID))
		{
			AIWarning("NavigationSystem::SetMeshBoundaryVolume: setting invalid volumeID %u for a mesh %u '%s'", (unsigned int)volumeID, (unsigned int)meshID, mesh.name.c_str());
			volumeID = NavigationVolumeID();
		}

		AILogComment("NavigationSystem::SetMeshBoundaryVolume: set volumeID %u for a mesh %u '%s'", (unsigned int)volumeID, (unsigned int)meshID, mesh.name.c_str());

		mesh.boundary = volumeID;
#ifdef SW_NAVMESH_USE_GUID
		mesh.boundaryGUID = volumeGUID;
#endif

		ComputeWorldAABB();
	}
}

#ifdef SW_NAVMESH_USE_GUID
NavigationVolumeID NavigationSystem::CreateVolume(Vec3* vertices, size_t vertexCount, float height, NavigationVolumeGUID guid)
{
	VolumeMap::iterator it = m_swVolumes.find(guid);
	if (it != m_swVolumes.end())
	{
		return it->second;
	}
	NavigationVolumeID id(++m_nextFreeVolumeId);
	id = CreateVolume(vertices, vertexCount, height, id);
	m_swVolumes.insert(std::pair<NavigationVolumeGUID, NavigationVolumeID>(guid, id));
	return id;
}
#else
NavigationVolumeID NavigationSystem::CreateVolume(Vec3* vertices, size_t vertexCount, float height)
{
	return CreateVolume(vertices, vertexCount, height, NavigationVolumeID(0));
}
#endif

NavigationVolumeID NavigationSystem::CreateVolume(Vec3* vertices, size_t vertexCount, float height, NavigationVolumeID requestedID)
{
	NavigationVolumeID id = requestedID;
	if (requestedID == NavigationVolumeID(0))
		id = NavigationVolumeID(m_volumes.insert(NavigationBoundingVolume()));
	else
		m_volumes.insert(requestedID, NavigationBoundingVolume());
	assert(id != 0);
	SetVolume(id, vertices, vertexCount, height);

	return id;
}

void NavigationSystem::DestroyVolume(NavigationVolumeID volumeID)
{
	if (volumeID && m_volumes.validate(volumeID))
	{
		NavigationBoundingVolume& volume = m_volumes[volumeID];

		AgentTypes::iterator it = m_agentTypes.begin();
		AgentTypes::iterator end = m_agentTypes.end();

		for (; it != end; ++it)
		{
			AgentType& agentType = *it;

			stl::find_and_erase(agentType.exclusions, volumeID);

			AgentType::Meshes::const_iterator mit = agentType.meshes.begin();
			AgentType::Meshes::const_iterator mend = agentType.meshes.end();

			for (; mit != mend; ++mit)
			{
				const NavigationMeshID meshID = mit->id;
				NavigationMesh& mesh = m_meshes[meshID];

				if (mesh.boundary == volumeID)
				{
					++mesh.version;
					continue;
				}

				if (stl::find_and_erase(mesh.exclusions, volumeID))
				{
					m_updatesManager.RequestMeshUpdate(meshID, volume.aabb);
					++mesh.version;
				}
			}
		}

		if (gEnv->IsEditor())
		{
			m_volumesManager.InvalidateID(volumeID);
		}

		m_volumes.erase(volumeID);
	}
}

void NavigationSystem::SetVolume(NavigationVolumeID volumeID, Vec3* vertices, size_t vertexCount, float height)
{
	assert(vertices);

	if (volumeID && m_volumes.validate(volumeID))
	{
		bool recomputeAABB = false;

		NavigationBoundingVolume newVolume;
		AABB aabbNew(AABB::RESET);

		newVolume.vertices.reserve(vertexCount);

		for (size_t i = 0; i < vertexCount; ++i)
		{
			aabbNew.Add(vertices[i]);
			newVolume.vertices.push_back(vertices[i]);
		}

		aabbNew.Add(vertices[0] + Vec3(0.0f, 0.0f, height));

		newVolume.height = height;
		newVolume.aabb = aabbNew;

		NavigationBoundingVolume& volume = m_volumes[volumeID];

		if (!volume.vertices.empty())
		{
			AgentTypes::const_iterator it = m_agentTypes.begin();
			AgentTypes::const_iterator end = m_agentTypes.end();

			for (; it != end; ++it)
			{
				const AgentType& agentType = *it;

				AgentType::Meshes::const_iterator mit = agentType.meshes.begin();
				AgentType::Meshes::const_iterator mend = agentType.meshes.end();

				for (; mit != mend; ++mit)
				{
					const NavigationMeshID meshID = mit->id;
					NavigationMesh& mesh = m_meshes[meshID];

					if (mesh.boundary == volumeID)
					{
						++mesh.version;
						recomputeAABB = true;
						
						m_updatesManager.RequestMeshDifferenceUpdate(meshID, volume, newVolume);
					}

					if (std::find(mesh.exclusions.begin(), mesh.exclusions.end(), volumeID) != mesh.exclusions.end())
					{
						m_updatesManager.RequestMeshUpdate(meshID, volume.aabb);
						m_updatesManager.RequestMeshUpdate(meshID, aabbNew);
						++mesh.version;
					}
				}
			}
		}

		newVolume.Swap(volume);

		// recompute the world bounding box after we have set the volume
		if (recomputeAABB)
			ComputeWorldAABB();
	}
}

bool NavigationSystem::ValidateVolume(NavigationVolumeID volumeID) const
{
	return m_volumes.validate(volumeID);
}

NavigationVolumeID NavigationSystem::GetVolumeID(NavigationMeshID meshID) const
{
	// This function is used to retrieve the correct ID of the volume boundary connected to the mesh.
	// After restoring the navigation data it could be that the cached volume id in the Sandbox SapeObject
	// is different from the actual one connected to the shape.
	if (meshID && m_meshes.validate(meshID))
		return m_meshes[meshID].boundary;
	else
		return NavigationVolumeID();
}

#ifdef SW_NAVMESH_USE_GUID
void NavigationSystem::SetExclusionVolume(const NavigationAgentTypeID* agentTypeIDs, size_t agentTypeIDCount, NavigationVolumeID volumeID, NavigationVolumeGUID volumeGUID)
#else
void NavigationSystem::SetExclusionVolume(const NavigationAgentTypeID* agentTypeIDs, size_t agentTypeIDCount, NavigationVolumeID volumeID)
#endif
{
	if (volumeID && m_volumes.validate(volumeID))
	{
		bool recomputeAABB = false;

		NavigationBoundingVolume& volume = m_volumes[volumeID];

		AgentTypes::iterator it = m_agentTypes.begin();
		AgentTypes::iterator end = m_agentTypes.end();

		for (; it != end; ++it)
		{
			AgentType& agentType = *it;

			stl::find_and_erase(agentType.exclusions, volumeID);

			AgentType::Meshes::const_iterator mit = agentType.meshes.begin();
			AgentType::Meshes::const_iterator mend = agentType.meshes.end();

			for (; mit != mend; ++mit)
			{
				const NavigationMeshID meshID = mit->id;
				NavigationMesh& mesh = m_meshes[meshID];

				if (stl::find_and_erase(mesh.exclusions, volumeID))
				{
					m_updatesManager.RequestMeshUpdate(meshID, volume.aabb);

					++mesh.version;
				}
			}
		}

		for (size_t i = 0; i < agentTypeIDCount; ++i)
		{
			if (agentTypeIDs[i] && (agentTypeIDs[i] <= m_agentTypes.size()))
			{
				AgentType& agentType = m_agentTypes[agentTypeIDs[i] - 1];

				agentType.exclusions.push_back(volumeID);

				AgentType::Meshes::const_iterator mit = agentType.meshes.begin();
				AgentType::Meshes::const_iterator mend = agentType.meshes.end();

				for (; mit != mend; ++mit)
				{
					const NavigationMeshID meshID = mit->id;
					NavigationMesh& mesh = m_meshes[meshID];

					mesh.exclusions.push_back(volumeID);
#ifdef SW_NAVMESH_USE_GUID
					mesh.exclusionsGUID.push_back(volumeGUID);
#endif
					++mesh.version;

					if (mesh.boundary != volumeID)
						m_updatesManager.RequestMeshUpdate(meshID, volume.aabb);
					else
					{
						AILogComment("NavigationSystem::SetExclusionVolume: volumeID %u for a mesh %u '%s'", (unsigned int)volumeID, (unsigned int)meshID, mesh.name.c_str());
						mesh.boundary = NavigationVolumeID();
						recomputeAABB = true;
					}
				}
			}
		}

		if (recomputeAABB)
			ComputeWorldAABB();
	}
}

/*
   void NavigationSystem::AddMeshExclusionVolume(NavigationMeshID meshID, NavigationVolumeID volumeID)
   {
   if (meshID && volumeID && m_meshes.validate(meshID) && m_volumes.validate(volumeID))
   {
   NavigationMesh& mesh = m_meshes[meshID];

   if (stl::push_back_unique(mesh.exclusions, volumeID))
   ++mesh.version;
   }
   }

   void NavigationSystem::RemoveMeshExclusionVolume(NavigationMeshID meshID, NavigationVolumeID volumeID)
   {
   if (meshID && volumeID && m_meshes.validate(meshID) && m_volumes.validate(volumeID))
   {
   NavigationMesh& mesh = m_meshes[meshID];

   if (stl::find_and_erase(mesh.exclusions, volumeID))
   ++mesh.version;
   }
   }
 */
NavigationMeshID NavigationSystem::GetMeshID(const char* name, NavigationAgentTypeID agentTypeID) const
{
	assert(name && agentTypeID && (agentTypeID <= m_agentTypes.size()));

	if (agentTypeID && (agentTypeID <= m_agentTypes.size()))
	{
		uint32 nameHash = NameHash(name);

		const AgentType& agentType = m_agentTypes[agentTypeID - 1];
		AgentType::Meshes::const_iterator it = agentType.meshes.begin();
		AgentType::Meshes::const_iterator end = agentType.meshes.end();

		for (; it != end; ++it)
		{
			if (it->name == nameHash)
				return it->id;
		}
	}

	return NavigationMeshID();
}

const char* NavigationSystem::GetMeshName(NavigationMeshID meshID) const
{
	if (meshID && m_meshes.validate(meshID))
	{
		const NavigationMesh& mesh = m_meshes[meshID];

		return mesh.name.c_str();
	}

	return 0;
}

void NavigationSystem::SetMeshName(NavigationMeshID meshID, const char* name)
{
	if (meshID && m_meshes.validate(meshID))
	{
		NavigationMesh& mesh = m_meshes[meshID];

		mesh.name = name;

		AgentType& agentType = m_agentTypes[mesh.agentTypeID - 1];
		AgentType::Meshes::iterator it = agentType.meshes.begin();
		AgentType::Meshes::iterator end = agentType.meshes.end();

		for (; it != end; ++it)
		{
			if (it->id == meshID)
			{
				it->name = NameHash(name);
				break;
			}
		}
	}
}

void NavigationSystem::ResetAllNavigationSystemUsers()
{
	for (NavigationSystemUsers::Notifier notifier(m_users); notifier.IsValid(); notifier.Next())
	{
		notifier->Reset();
	}
}

void NavigationSystem::WaitForAllNavigationSystemUsersCompleteTheirReadingAsynchronousTasks()
{
	for (NavigationSystemUsers::Notifier notifier(m_users); notifier.IsValid(); notifier.Next())
	{
		notifier->CompleteRunningTasks();
	}
}

INavigationSystem::WorkingState NavigationSystem::GetState() const
{
	return m_state;
}

void NavigationSystem::UpdateNavigationSystemUsersForSynchronousWritingOperations()
{
	for (NavigationSystemUsers::Notifier notifier(m_users); notifier.IsValid(); notifier.Next())
	{
		notifier->UpdateForSynchronousWritingOperations();
	}
}

void NavigationSystem::UpdateNavigationSystemUsersForSynchronousOrAsynchronousReadingOperations()
{
	for (NavigationSystemUsers::Notifier notifier(m_users); notifier.IsValid(); notifier.Next())
	{
		notifier->UpdateForSynchronousOrAsynchronousReadingOperation();
	}
}

void NavigationSystem::UpdateInternalNavigationSystemData(const bool blocking)
{
#if NAVIGATION_SYSTEM_PC_ONLY
	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);

	CRY_ASSERT_MESSAGE(m_pEditorBackgroundUpdate->IsRunning() == false, "Background update for editor is still running while the application has the focus!!");

	const bool editorBackgroundThreadRunning = m_pEditorBackgroundUpdate->IsRunning();
	if (editorBackgroundThreadRunning)
	{
		AIError("NavigationSystem - Editor background thread is still running, while the main thread is updating, skipping update");
		return;
	}

	m_worldMonitor.FlushPendingAABBChanges();

	// Prevent multiple updates per frame
	static int lastUpdateFrameID = 0;

	const int frameID = gEnv->nMainFrameID;
	const bool doUpdate = (frameID != lastUpdateFrameID) && !(editorBackgroundThreadRunning);
	if (doUpdate)
	{
		lastUpdateFrameID = frameID;

		const float frameTime = gEnv->pTimer->GetFrameTime();

		UpdateMeshes(frameTime, blocking, gAIEnv.CVars.NavigationSystemMT != 0, false);
	}
#endif
}

void NavigationSystem::UpdateInternalSubsystems()
{
	m_offMeshNavigationManager.ProcessQueuedRequests();
}

INavigationSystem::WorkingState NavigationSystem::Update(bool blocking)
{
	// Pre update step. We need to request all our NavigationSystem users
	// to complete all their reading jobs.
	WaitForAllNavigationSystemUsersCompleteTheirReadingAsynchronousTasks();

	// step 1: update all the tasks that may write on the NavigationSystem data
	UpdateInternalNavigationSystemData(blocking);
	UpdateInternalSubsystems();

	// step 2: update all the writing tasks that needs to write some data inside the navigation system
	UpdateNavigationSystemUsersForSynchronousWritingOperations();

	// step 3: update all the tasks that needs to read only the NavigationSystem data
	// Those tasks may or may not be asynchronous tasks and the reading of the navigation data
	// may extend in time after this function is actually executed.
	UpdateNavigationSystemUsersForSynchronousOrAsynchronousReadingOperations();

	return m_state;
}

void NavigationSystem::PauseNavigationUpdate()
{
	m_isNavigationUpdatePaused = true;
	m_pEditorBackgroundUpdate->Pause(true);
}

void NavigationSystem::RestartNavigationUpdate()
{
	m_isNavigationUpdatePaused = false;
	m_pEditorBackgroundUpdate->Pause(false);
}

uint32 NavigationSystem::GetWorkingQueueSize() const 
{ 
	return (uint32)m_updatesManager.GetRequestQueueSize();
}

#if NAVIGATION_SYSTEM_PC_ONLY
void NavigationSystem::UpdateMeshes(const float frameTime, const bool blocking, const bool multiThreaded, const bool bBackground)
{
	m_updatesManager.Update();
	
	if (m_isNavigationUpdatePaused || frameTime == .0f)
		return;

	m_debugDraw.UpdateWorkingProgress(frameTime, m_updatesManager.GetRequestQueueSize());

	if (!m_updatesManager.HasUpdateRequests() && m_runningTasks.empty())
	{
		if (m_state != Idle)
		{
			// We just finished the processing of the tiles, so before being in Idle
			// we need to recompute the Islands detection
			ComputeIslands();
		}
		m_state = Idle;
		return;
	}

	size_t completed = 0;
	size_t cacheHit = 0;

	while (true)
	{
		if (!m_runningTasks.empty())
		{
			FRAME_PROFILER("Navigation System::UpdateMeshes() - Running Task Processing", gEnv->pSystem, PROFILE_AI);

			RunningTasks::iterator it = m_runningTasks.begin();
			RunningTasks::iterator end = m_runningTasks.end();

			for (; it != end; )
			{
				const size_t resultSlot = *it;
				assert(resultSlot < m_results.size());

				TileTaskResult& result = m_results[resultSlot];
				if (result.state == TileTaskResult::Running)
				{
					++it;
					continue;
				}

				CommitTile(result);

				{
					FRAME_PROFILER("Navigation System::UpdateMeshes() - Running Task Processing - WaitForJob", gEnv->pSystem, PROFILE_AI);

					gEnv->GetJobManager()->WaitForJob(result.jobState);
				}

				++completed;
				cacheHit += result.state == TileTaskResult::NoChanges;

				MNM::STile().Swap(result.tile);

				VolumeDefCopy& def = m_volumeDefCopy[result.volumeCopy];
				--def.refCount;

				result.state = TileTaskResult::Running;
				result.next = m_free;

				m_free = static_cast<uint16>(resultSlot);

				const bool reachedLastTask = (it == m_runningTasks.end() - 1);

				std::swap(*it, m_runningTasks.back());
				m_runningTasks.pop_back();

				if (reachedLastTask)
					break;  // prevent the invalidated iterator "it" from getting compared in the for-loop

				end = m_runningTasks.end();
			}
		}

		m_throughput = completed / frameTime;
		m_cacheHitRate = cacheHit / frameTime;

		if (!m_updatesManager.HasUpdateRequests() && m_runningTasks.empty())
		{
			if (m_state != Idle)
			{
				// We just finished the processing of the tiles, so before being in Idle
				// we need to recompute the Islands detection
				ComputeIslands();
			}

			m_state = Idle;

			return;
		}

		if (m_updatesManager.HasUpdateRequests())
		{
			m_state = Working;

			FRAME_PROFILER("Navigation System::UpdateMeshes() - Job Spawning", gEnv->pSystem, PROFILE_AI);
			const size_t idealMinimumTaskCount = 2;
			const size_t MaxRunningTaskCount = multiThreaded ? m_maxRunningTaskCount : std::min(m_maxRunningTaskCount, idealMinimumTaskCount);

			while (m_updatesManager.HasUpdateRequests() && (m_runningTasks.size() < MaxRunningTaskCount))
			{
				const CMNMUpdatesManager::TileUpdateRequest& task = m_updatesManager.GetFrontRequest();

				if (task.IsAborted())
				{
					m_updatesManager.PopFrontRequest();
					continue;
				}

				const NavigationMesh& mesh = m_meshes[task.meshID];
				const MNM::CNavMesh::SGridParams& paramsGrid = mesh.navMesh.GetGridParams();

				m_runningTasks.push_back(m_free);
				CRY_ASSERT_MESSAGE(m_free < m_results.size(), "Index out of array bounds!");
				TileTaskResult& result = m_results[m_free];
				m_free = result.next;

				if (!SpawnJob(result, task.meshID, paramsGrid, task.x, task.y, task.z, multiThreaded))
				{
					result.state = TileTaskResult::Running;
					result.next = m_free;

					m_free = m_runningTasks.back();
					m_runningTasks.pop_back();
					break;
				}

				m_updatesManager.PopFrontRequest();
			}

			// keep main thread busy too if we're blocking
			if (blocking && m_updatesManager.HasUpdateRequests() && multiThreaded)
			{
				while (m_updatesManager.HasUpdateRequests())
				{
					const CMNMUpdatesManager::TileUpdateRequest& task = m_updatesManager.GetFrontRequest();

					if (task.IsAborted())
					{
						m_updatesManager.PopFrontRequest();
						continue;
					}

					NavigationMesh& mesh = m_meshes[task.meshID];
					const MNM::CNavMesh::SGridParams& paramsGrid = mesh.navMesh.GetGridParams();

					TileTaskResult result;
					if (!SpawnJob(result, task.meshID, paramsGrid, task.x, task.y, task.z, false))
						break;

					CommitTile(result);

					m_updatesManager.PopFrontRequest();
					break;
				}
			}
		}

		if (blocking && (m_updatesManager.HasUpdateRequests() || !m_runningTasks.empty()))
			continue;

		m_state = m_runningTasks.empty() ? Idle : Working;

		return;
	}

}

void NavigationSystem::SetupGenerator(NavigationMeshID meshID, const MNM::CNavMesh::SGridParams& paramsGrid,
                                      uint16 x, uint16 y, uint16 z, MNM::CTileGenerator::Params& params,
                                      const MNM::BoundingVolume* boundary, const MNM::BoundingVolume* exclusions,
                                      size_t exclusionCount)
{
	const NavigationMesh& mesh = m_meshes[meshID];

	params.origin = paramsGrid.origin + Vec3i(x * paramsGrid.tileSize.x, y * paramsGrid.tileSize.y, z * paramsGrid.tileSize.z);
	params.voxelSize = paramsGrid.voxelSize;
	params.sizeX = paramsGrid.tileSize.x;
	params.sizeY = paramsGrid.tileSize.y;
	params.sizeZ = paramsGrid.tileSize.z;
	params.boundary = boundary;
	params.exclusions = exclusions;
	params.exclusionCount = static_cast<uint16>(exclusionCount);

	const AgentType& agentType = m_agentTypes[mesh.agentTypeID - 1];

	params.agent.radius = agentType.settings.radiusVoxelCount;
	params.agent.height = agentType.settings.heightVoxelCount;
	params.agent.climbableHeight = agentType.settings.climbableVoxelCount;
	params.agent.maxWaterDepth = agentType.settings.maxWaterDepthVoxelCount;
	params.climbableInclineGradient = agentType.settings.climbableInclineGradient;
	params.climbableStepRatio = agentType.settings.climbableStepRatio;
	params.agent.callback = agentType.meshEntityCallback;

	if (MNM::TileID tileID = mesh.navMesh.GetTileID(x, y, z))
		params.hashValue = mesh.navMesh.GetTile(tileID).GetHashValue();
	else
		params.flags |= MNM::CTileGenerator::Params::NoHashTest;

	params.pTileGeneratorExtensions = &m_tileGeneratorExtensionsContainer;
	params.navAgentTypeId = mesh.agentTypeID;
}

bool NavigationSystem::SpawnJob(TileTaskResult& result, NavigationMeshID meshID, const MNM::CNavMesh::SGridParams& paramsGrid,
                                uint16 x, uint16 y, uint16 z, bool mt)
{
	result.x = x;
	result.y = y;
	result.z = z;

	result.meshID = meshID;
	result.state = TileTaskResult::Running;
	result.hashValue = 0;

	const NavigationMesh& mesh = m_meshes[meshID];

	size_t firstFree = ~0ul;
	size_t index = ~0ul;

	for (size_t i = 0; i < MaxVolumeDefCopyCount; ++i)
	{
		VolumeDefCopy& def = m_volumeDefCopy[i];
		if ((def.meshID == meshID) && (def.version == mesh.version))
		{
			index = i;
			break;
		}
		else if ((firstFree == ~0ul) && !def.refCount)
			firstFree = i;
	}

	if ((index == ~0ul) && (firstFree == ~0ul))
		return false;

	VolumeDefCopy* def = 0;

	if (index != ~0ul)
		def = &m_volumeDefCopy[index];
	else
	{
		index = firstFree;

		if (!m_volumes.validate(mesh.boundary))
		{
			AIWarning("NavigationSystem::SpawnJob: Detected non-valid mesh boundary volume (%u) for mesh %u '%s', skipping", (unsigned int)mesh.boundary, (unsigned int)meshID, mesh.name.c_str());
			return false;
		}

		def = &m_volumeDefCopy[index];
		def->meshID = meshID;
		def->version = mesh.version;
		def->boundary = m_volumes[mesh.boundary];
		def->exclusions.clear();
		def->exclusions.reserve(mesh.exclusions.size());

		NavigationMesh::ExclusionVolumes::const_iterator eit = mesh.exclusions.begin();
		NavigationMesh::ExclusionVolumes::const_iterator eend = mesh.exclusions.end();

		for (; eit != eend; ++eit)
		{
			const NavigationVolumeID exclusionVolumeID = *eit;
			assert(m_volumes.validate(exclusionVolumeID));

			if (m_volumes.validate(exclusionVolumeID))
			{
				def->exclusions.push_back(m_volumes[exclusionVolumeID]);
			}
			else
			{
				CryLogAlways("NavigationSystem::SpawnJob(): Detected non-valid exclusion volume (%d) for mesh '%s', skipping", (uint32)exclusionVolumeID, mesh.name.c_str());
			}
		}
	}

	result.volumeCopy = static_cast<uint16>(index);

	++def->refCount;

	MNM::CTileGenerator::Params params;

	SetupGenerator(meshID, paramsGrid, x, y, z, params, &def->boundary,
	               def->exclusions.empty() ? 0 : &def->exclusions[0], def->exclusions.size());

	if (mt)
	{
		NavigationGenerationJob job(params, &result.state, &result.tile, &result.hashValue);
		job.RegisterJobState(&result.jobState);
		job.SetPriorityLevel(JobManager::eStreamPriority);
		job.Run();
	}
	else
	{
		GenerateTileJob(params, &result.state, &result.tile, &result.hashValue);
	}

	if (gAIEnv.CVars.DebugDrawNavigation)
	{
		if (gEnv->pRenderer)
		{
			if (IRenderAuxGeom* pRenderAuxGeom = gEnv->pRenderer->GetIRenderAuxGeom())
			{
				pRenderAuxGeom->DrawAABB(AABB(params.origin, params.origin + Vec3((float)params.sizeX, (float)params.sizeY, (float)params.sizeZ)),
				                         IDENTITY, false, Col_Red, eBBD_Faceted);
			}
		}
	}

	return true;
}

void NavigationSystem::CommitTile(TileTaskResult& result)
{
	// The mesh for this tile has been destroyed, it doesn't make sense to commit the tile
	const bool nonValidTile = (m_meshes.validate(result.meshID) == false);
	if (nonValidTile)
	{
		return;
	}

	NavigationMesh& mesh = m_meshes[result.meshID];

	switch (result.state)
	{
	case TileTaskResult::Completed:
		{
			FRAME_PROFILER("Navigation System::CommitTile() - Running Task Processing - ConnectToNetwork", gEnv->pSystem, PROFILE_AI);

			MNM::TileID tileID = mesh.navMesh.SetTile(result.x, result.y, result.z, result.tile);
			mesh.navMesh.ConnectToNetwork(tileID);

			m_offMeshNavigationManager.RefreshConnections(result.meshID, tileID);
			gAIEnv.pMNMPathfinder->OnNavigationMeshChanged(result.meshID, tileID);

			const AgentType& agentType = m_agentTypes[mesh.agentTypeID - 1];
			AgentType::Callbacks::const_iterator cit = agentType.callbacks.begin();
			AgentType::Callbacks::const_iterator cend = agentType.callbacks.end();

			for (; cit != cend; ++cit)
			{
				const NavigationMeshChangeCallback& callback = *cit;
				callback(mesh.agentTypeID, result.meshID, tileID);
			}
		}
		break;
	case TileTaskResult::Failed:
		{
			FRAME_PROFILER("Navigation System::CommitTile() - Running Task Processing - ClearTile", gEnv->pSystem, PROFILE_AI);

			if (MNM::TileID tileID = mesh.navMesh.GetTileID(result.x, result.y, result.z))
			{
				mesh.navMesh.ClearTile(tileID);

				m_offMeshNavigationManager.RefreshConnections(result.meshID, tileID);
				gAIEnv.pMNMPathfinder->OnNavigationMeshChanged(result.meshID, tileID);

				const AgentType& agentType = m_agentTypes[mesh.agentTypeID - 1];
				AgentType::Callbacks::const_iterator cit = agentType.callbacks.begin();
				AgentType::Callbacks::const_iterator cend = agentType.callbacks.end();

				for (; cit != cend; ++cit)
				{
					const NavigationMeshChangeCallback& callback = *cit;
					callback(mesh.agentTypeID, result.meshID, tileID);
				}
			}
		}
		break;
	case TileTaskResult::NoChanges:
		break;
	default:
		assert(0);
		break;
	}
}
#endif

void NavigationSystem::ProcessQueuedMeshUpdates()
{
#if NAVIGATION_SYSTEM_PC_ONLY
	do
	{
		UpdateMeshes(0.0333f, false, gAIEnv.CVars.NavigationSystemMT != 0, false);
	}
	while (m_state == INavigationSystem::Working);
#endif
}

size_t NavigationSystem::QueueMeshUpdate(NavigationMeshID meshID, const AABB& aabb)
{
	AIWarning("NavigationSystem::QueueMeshUpdate() is deprecated! INavigationUpdatesManager::RequestMeshUpdate should be used instead");
	m_updatesManager.RequestMeshUpdate(meshID, aabb);
	return 0;
}

void NavigationSystem::StopAllTasks()
{
	for (size_t t = 0; t < m_runningTasks.size(); ++t)
		m_results[m_runningTasks[t]].state = TileTaskResult::Failed;

	for (size_t t = 0; t < m_runningTasks.size(); ++t)
		gEnv->pJobManager->WaitForJob(m_results[m_runningTasks[t]].jobState);

	for (size_t t = 0; t < m_runningTasks.size(); ++t)
		m_results[m_runningTasks[t]].tile.Destroy();

	m_runningTasks.clear();
}

void NavigationSystem::ComputeIslands()
{
	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);

	m_islandConnectionsManager.Reset();

	AgentTypes::const_iterator it = m_agentTypes.begin();
	AgentTypes::const_iterator end = m_agentTypes.end();

	for (; it != end; ++it)
	{
		const AgentType& agentType = *it;
		AgentType::Meshes::const_iterator itMesh = agentType.meshes.begin();
		AgentType::Meshes::const_iterator endMesh = agentType.meshes.end();
		for (; itMesh != endMesh; ++itMesh)
		{
			if (itMesh->id && m_meshes.validate(itMesh->id))
			{
				NavigationMeshID meshID = itMesh->id;
				MNM::IslandConnections& islandConnections = m_islandConnectionsManager.GetIslandConnections();
				NavigationMesh& mesh = m_meshes[meshID];
				mesh.navMesh.ComputeStaticIslandsAndConnections(meshID, m_offMeshNavigationManager, islandConnections);
			}
		}
	}
}

void NavigationSystem::AddIslandConnectionsBetweenTriangles(const NavigationMeshID& meshID, const MNM::TriangleID startingTriangleID,
                                                            const MNM::TriangleID endingTriangleID)
{
	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);

	if (m_meshes.validate(meshID))
	{
		NavigationMesh& mesh = m_meshes[meshID];
		MNM::Tile::STriangle startingTriangle, endingTriangle;
		if (mesh.navMesh.GetTriangle(startingTriangleID, startingTriangle))
		{
			if (mesh.navMesh.GetTriangle(endingTriangleID, endingTriangle))
			{
				MNM::GlobalIslandID startingIslandID(meshID, startingTriangle.islandID);

				MNM::STile& tile = mesh.navMesh.GetTile(MNM::ComputeTileID(startingTriangleID));
				for (uint16 l = 0; l < startingTriangle.linkCount; ++l)
				{
					const MNM::Tile::SLink& link = tile.GetLinks()[startingTriangle.firstLink + l];
					if (link.side == MNM::Tile::SLink::OffMesh)
					{
#if DEBUG_MNM_LOG_OFFMESH_LINK_OPERATIONS
						AILogCommentID("<MNM:OffMeshLink>", "NavigationSystem::AddIslandConnectionsBetweenTriangles link from %u to %u (mesh %u)", startingTriangle.islandID, endingTriangle.islandID, meshID);
#endif
						MNM::GlobalIslandID endingIslandID(meshID, endingTriangle.islandID);
						MNM::OffMeshNavigation& offMeshNavigation = m_offMeshNavigationManager.GetOffMeshNavigationForMesh(meshID);
						MNM::OffMeshNavigation::QueryLinksResult linksResult = offMeshNavigation.GetLinksForTriangle(startingTriangleID, link.triangle);
						while (MNM::WayTriangleData nextTri = linksResult.GetNextTriangle())
						{
							if (nextTri.triangleID == endingTriangleID)
							{
								const MNM::OffMeshLink* pLink = m_offMeshNavigationManager.GetOffMeshLink(nextTri.offMeshLinkID);
								assert(pLink);
								MNM::IslandConnections::Link islandLink(nextTri.triangleID, nextTri.offMeshLinkID, endingIslandID, pLink->GetEntityIdForOffMeshLink());
								MNM::IslandConnections& islandConnections = m_islandConnectionsManager.GetIslandConnections();
								islandConnections.SetOneWayConnectionBetweenIsland(startingIslandID, islandLink);
							}
						}
					}
				}
			}
		}
	}
}

void NavigationSystem::RemoveAllIslandConnectionsForObject(const NavigationMeshID& meshID, const uint32 objectId)
{
	MNM::IslandConnections& islandConnections = m_islandConnectionsManager.GetIslandConnections();
	islandConnections.RemoveAllIslandConnectionsForObject(meshID, objectId);
}

void NavigationSystem::RemoveIslandsConnectionBetweenTriangles(const NavigationMeshID& meshID, const MNM::TriangleID startingTriangleID,
                                                               const MNM::TriangleID endingTriangleID)
{
	// NOTE pavloi 2016.02.05: be advised, that this function is not use anywhere. It should be called before triangles are unlinked
	// from each other, but currently OffMeshNavigationManager first unlinks triangles and only then unlinks islands.

	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);

	if (m_meshes.validate(meshID))
	{
		NavigationMesh& mesh = m_meshes[meshID];
		MNM::Tile::STriangle startingTriangle, endingTriangle;
		if (mesh.navMesh.GetTriangle(startingTriangleID, startingTriangle))
		{
			if (mesh.navMesh.GetTriangle(endingTriangleID, endingTriangle))
			{
				MNM::GlobalIslandID startingIslandID(meshID, startingTriangle.islandID);

				MNM::STile& tile = mesh.navMesh.GetTile(MNM::ComputeTileID(startingTriangleID));
				for (uint16 l = 0; l < startingTriangle.linkCount; ++l)
				{
					const MNM::Tile::SLink& link = tile.GetLinks()[startingTriangle.firstLink + l];
					if (link.side == MNM::Tile::SLink::OffMesh)
					{
#if DEBUG_MNM_LOG_OFFMESH_LINK_OPERATIONS
						AILogCommentID("<MNM:OffMeshLink>", "NavigationSystem::RemoveIslandsConnectionBetweenTriangles link from %u to %u (mesh %u)", startingTriangle.islandID, endingTriangle.islandID, meshID);
#endif
						MNM::GlobalIslandID endingIslandID(meshID, endingTriangle.islandID);
						MNM::OffMeshNavigation& offMeshNavigation = m_offMeshNavigationManager.GetOffMeshNavigationForMesh(meshID);
						MNM::OffMeshNavigation::QueryLinksResult linksResult = offMeshNavigation.GetLinksForTriangle(startingTriangleID, link.triangle);
						while (MNM::WayTriangleData nextTri = linksResult.GetNextTriangle())
						{
							if (nextTri.triangleID == endingTriangleID)
							{
								const MNM::OffMeshLink* pLink = m_offMeshNavigationManager.GetOffMeshLink(nextTri.offMeshLinkID);
								assert(pLink);
								MNM::IslandConnections::Link islandLink(nextTri.triangleID, nextTri.offMeshLinkID, endingIslandID, pLink->GetEntityIdForOffMeshLink());
								MNM::IslandConnections& islandConnections = m_islandConnectionsManager.GetIslandConnections();
								islandConnections.RemoveOneWayConnectionBetweenIsland(startingIslandID, islandLink);
							}
						}
					}
				}
			}
		}
	}
}

void NavigationSystem::AddOffMeshLinkIslandConnectionsBetweenTriangles(
  const NavigationMeshID& meshID,
  const MNM::TriangleID startingTriangleID,
  const MNM::TriangleID endingTriangleID,
  const MNM::OffMeshLinkID& linkID)
{
	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);

#if DEBUG_MNM_DATA_CONSISTENCY_ENABLED
	{
		bool bLinkIsFound = false;
		// Next piece code is an almost exact copy from AddIslandConnectionsBetweenTriangles()
		if (m_meshes.validate(meshID))
		{
			const NavigationMesh& mesh = m_meshes[meshID];
			MNM::Tile::STriangle startingTriangle, endingTriangle;
			if (mesh.navMesh.GetTriangle(startingTriangleID, startingTriangle))
			{
				if (mesh.navMesh.GetTriangle(endingTriangleID, endingTriangle))
				{
					const MNM::GlobalIslandID startingIslandID(meshID, startingTriangle.islandID);
					const MNM::STile& tile = mesh.navMesh.GetTile(MNM::ComputeTileID(startingTriangleID));
					for (uint16 l = 0; l < startingTriangle.linkCount && !bLinkIsFound; ++l)
					{
						const MNM::Tile::SLink& link = tile.GetLinks()[startingTriangle.firstLink + l];
						if (link.side == MNM::Tile::SLink::OffMesh)
						{
							const MNM::GlobalIslandID endingIslandID(meshID, endingTriangle.islandID);
							const MNM::OffMeshNavigation& offMeshNavigation = m_offMeshNavigationManager.GetOffMeshNavigationForMesh(meshID);
							const MNM::OffMeshNavigation::QueryLinksResult linksResult = offMeshNavigation.GetLinksForTriangle(startingTriangleID, link.triangle);
							while (MNM::WayTriangleData nextTri = linksResult.GetNextTriangle())
							{
								if (nextTri.triangleID == endingTriangleID)
								{
									if (nextTri.offMeshLinkID == linkID)
									{
										if (const MNM::OffMeshLink* pLink = m_offMeshNavigationManager.GetOffMeshLink(nextTri.offMeshLinkID))
										{
											bLinkIsFound = true;
											break;
										}
									}
								}
							}
						}
					}
				}
			}
		}

		if (!bLinkIsFound)
		{
			// It's expected, that the triangles in tiles are already linked together before this function is called.
			// But we haven't found a link during validation.
			AIErrorID("<MNM:OffMeshLink>", "NavigationSystem::AddOffMeshLinkIslandConnectionsBetweenTriangles is called with wrong input");
		}
	}
#endif // DEBUG_MNM_DATA_CONSISTENCY_ENABLED

	// TODO pavloi 2016.02.05: whole piece is suboptimal - this function is called from m_offMeshNavigationManager already, where
	// it linked triangles and had full info about them. I leave it like this to be consistent with AddIslandConnectionsBetweenTriangles()
	if (m_meshes.validate(meshID))
	{
		NavigationMesh& mesh = m_meshes[meshID];
		MNM::Tile::STriangle startingTriangle, endingTriangle;
		if (mesh.navMesh.GetTriangle(startingTriangleID, startingTriangle))
		{
			if (mesh.navMesh.GetTriangle(endingTriangleID, endingTriangle))
			{
				const MNM::GlobalIslandID startingIslandID(meshID, startingTriangle.islandID);
				const MNM::GlobalIslandID endingIslandID(meshID, endingTriangle.islandID);

				const MNM::OffMeshLink* pLink = m_offMeshNavigationManager.GetOffMeshLink(linkID);
				if (pLink)
				{
					const MNM::IslandConnections::Link islandLink(endingTriangleID, linkID, endingIslandID, pLink->GetEntityIdForOffMeshLink());
					MNM::IslandConnections& islandConnections = m_islandConnectionsManager.GetIslandConnections();
					islandConnections.SetOneWayConnectionBetweenIsland(startingIslandID, islandLink);
				}
			}
		}
	}
}

void NavigationSystem::RemoveOffMeshLinkIslandsConnectionBetweenTriangles(
  const NavigationMeshID& meshID,
  const MNM::TriangleID startingTriangleID,
  const MNM::TriangleID endingTriangleID,
  const MNM::OffMeshLinkID& linkID)
{
	FUNCTION_PROFILER(gEnv->pSystem, PROFILE_AI);
#if DEBUG_MNM_DATA_CONSISTENCY_ENABLED
	{
		bool bLinkIsFound = false;
		// Next piece code is an almost exact copy from RemoveIslandConnectionsBetweenTriangles()
		if (m_meshes.validate(meshID))
		{
			const NavigationMesh& mesh = m_meshes[meshID];
			MNM::Tile::STriangle startingTriangle, endingTriangle;
			if (mesh.navMesh.GetTriangle(startingTriangleID, startingTriangle))
			{
				if (mesh.navMesh.GetTriangle(endingTriangleID, endingTriangle))
				{
					const MNM::GlobalIslandID startingIslandID(meshID, startingTriangle.islandID);

					const MNM::STile& tile = mesh.navMesh.GetTile(MNM::ComputeTileID(startingTriangleID));
					for (uint16 l = 0; l < startingTriangle.linkCount && !bLinkIsFound; ++l)
					{
						const MNM::Tile::SLink& link = tile.GetLinks()[startingTriangle.firstLink + l];
						if (link.side == MNM::Tile::SLink::OffMesh)
						{
							const MNM::GlobalIslandID endingIslandID(meshID, endingTriangle.islandID);
							const MNM::OffMeshNavigation& offMeshNavigation = m_offMeshNavigationManager.GetOffMeshNavigationForMesh(meshID);
							const MNM::OffMeshNavigation::QueryLinksResult linksResult = offMeshNavigation.GetLinksForTriangle(startingTriangleID, link.triangle);
							while (MNM::WayTriangleData nextTri = linksResult.GetNextTriangle())
							{
								if (nextTri.triangleID == endingTriangleID)
								{
									if (nextTri.offMeshLinkID == linkID)
									{
										if (const MNM::OffMeshLink* pLink = m_offMeshNavigationManager.GetOffMeshLink(nextTri.offMeshLinkID))
										{
											bLinkIsFound = true;
											break;
										}
									}
								}
							}
						}
					}
				}
			}
		}

		if (bLinkIsFound)
		{
			// It's expected, that the triangles in tiles are already unlinked from each other before this function is called.
			// But validation actually found, that the link is still there.
			AIErrorID("<MNM:OffMeshLink>", "NavigationSystem::RemoveOffMeshLinkIslandsConnectionBetweenTriangles is called with wrong input");
		}
	}
#endif // DEBUG_MNM_DATA_CONSISTENCY_ENABLED

	// TODO pavloi 2016.02.05: whole piece is suboptimal - this function is called from m_offMeshNavigationManager already, where
	// it unlinked triangles and had full info about them. I leave it like this to be consistent with RemoveIslandConnectionsBetweenTriangles()
	if (m_meshes.validate(meshID))
	{
		NavigationMesh& mesh = m_meshes[meshID];
		MNM::Tile::STriangle startingTriangle, endingTriangle;
		if (mesh.navMesh.GetTriangle(startingTriangleID, startingTriangle))
		{
			if (mesh.navMesh.GetTriangle(endingTriangleID, endingTriangle))
			{
				const MNM::GlobalIslandID startingIslandID(meshID, startingTriangle.islandID);
				const MNM::GlobalIslandID endingIslandID(meshID, endingTriangle.islandID);

				const MNM::OffMeshLink* pLink = m_offMeshNavigationManager.GetOffMeshLink(linkID);
				if (pLink)
				{
					const MNM::IslandConnections::Link islandLink(endingTriangleID, linkID, endingIslandID, pLink->GetEntityIdForOffMeshLink());
					MNM::IslandConnections& islandConnections = m_islandConnectionsManager.GetIslandConnections();
					islandConnections.RemoveOneWayConnectionBetweenIsland(startingIslandID, islandLink);
				}
			}
		}
	}
}

#if MNM_USE_EXPORT_INFORMATION
void NavigationSystem::ComputeAccessibility(IAIObject* pIAIObject, NavigationAgentTypeID agentTypeId /* = NavigationAgentTypeID(0) */)
{
	const CAIActor* actor = CastToCAIActorSafe(pIAIObject);
	const Vec3 debugLocation = pIAIObject->GetEntity()->GetPos(); // we're using the IEntity's position (not the IAIObject one's), because the CAIObject one's is always some time behind due to the way its private position is queried via BodyInfo -> StanceState -> eye position
	const NavigationAgentTypeID actorTypeId = actor ? actor->GetNavigationTypeID() : NavigationAgentTypeID(0);
	const NavigationAgentTypeID agentTypeIdForAccessibilityCalculation = agentTypeId ? agentTypeId : actorTypeId;
	NavigationMeshID meshId = GetEnclosingMeshID(agentTypeIdForAccessibilityCalculation, debugLocation);

	if (meshId)
	{
		NavigationMesh& mesh = GetMesh(meshId);
		const MNM::CNavMesh::SGridParams& paramsGrid = mesh.navMesh.GetGridParams();
		const MNM::OffMeshNavigation& offMeshNavigation = GetOffMeshNavigationManager()->GetOffMeshNavigationForMesh(meshId);

		const MNM::vector3_t origin = MNM::vector3_t(MNM::real_t(paramsGrid.origin.x), MNM::real_t(paramsGrid.origin.y), MNM::real_t(paramsGrid.origin.z));
		const Vec3& voxelSize = mesh.navMesh.GetGridParams().voxelSize;
		const MNM::vector3_t seedLocation(MNM::real_t(debugLocation.x), MNM::real_t(debugLocation.y), MNM::real_t(debugLocation.z));

		const uint16 agentHeightUnits = GetAgentHeightInVoxelUnits(agentTypeIdForAccessibilityCalculation);

		const MNM::real_t verticalRange = MNMUtils::CalculateMinVerticalRange(agentHeightUnits, voxelSize.z);
		const MNM::real_t verticalDownwardRange(verticalRange);

		AgentType agentTypeProperties;
		const bool arePropertiesValid = GetAgentTypeProperties(agentTypeIdForAccessibilityCalculation, agentTypeProperties);
		assert(arePropertiesValid);
		const uint16 minZOffsetMultiplier(2);
		const uint16 zOffsetMultiplier = min(minZOffsetMultiplier, agentTypeProperties.settings.heightVoxelCount);
		const MNM::real_t verticalUpwardRange = arePropertiesValid ? MNM::real_t(zOffsetMultiplier * agentTypeProperties.settings.voxelSize.z) : MNM::real_t(.2f);

		MNM::TriangleID seedTriangleID = mesh.navMesh.GetTriangleAt(seedLocation - origin, verticalDownwardRange, verticalUpwardRange);

		if (seedTriangleID)
		{
			MNM::CNavMesh::AccessibilityRequest inputRequest(seedTriangleID, offMeshNavigation);
			mesh.navMesh.ComputeAccessibility(inputRequest);
		}
	}
}

void NavigationSystem::ClearAllAccessibility(uint8 resetValue)
{
	AgentTypes::const_iterator it = m_agentTypes.begin();
	AgentTypes::const_iterator end = m_agentTypes.end();

	for (; it != end; ++it)
	{
		const AgentType& agentType = *it;
		AgentType::Meshes::const_iterator itMesh = agentType.meshes.begin();
		AgentType::Meshes::const_iterator endMesh = agentType.meshes.end();
		for (; itMesh != endMesh; ++itMesh)
		{
			if (itMesh->id && m_meshes.validate(itMesh->id))
			{
				NavigationMesh& mesh = m_meshes[itMesh->id];
				mesh.navMesh.ResetAccessibility(resetValue);
			}
		}
	}
}
#endif

void NavigationSystem::CalculateAccessibility()
{
#if MNM_USE_EXPORT_INFORMATION

	bool isThereAtLeastOneSeedPresent = false;

	ClearAllAccessibility(MNM::CNavMesh::eARNotAccessible);

	// Filtering accessibility with actors
	{
		AutoAIObjectIter itActors(gAIEnv.pAIObjectManager->GetFirstAIObject(OBJFILTER_TYPE, AIOBJECT_ACTOR));

		for (; itActors->GetObject(); itActors->Next())
		{
			ComputeAccessibility(itActors->GetObject());
		}
	}

	// Filtering accessibility with Navigation Seeds
	{
		AutoAIObjectIter itNavSeeds(gAIEnv.pAIObjectManager->GetFirstAIObject(OBJFILTER_TYPE, AIOBJECT_NAV_SEED));

		for (; itNavSeeds->GetObject(); itNavSeeds->Next())
		{

			AgentTypes::const_iterator it = m_agentTypes.begin();
			AgentTypes::const_iterator end = m_agentTypes.end();

			for (; it != end; ++it)
			{
				const AgentType& agentType = *it;
				ComputeAccessibility(itNavSeeds->GetObject(), GetAgentTypeID(it->name));
			}
		}
	}
#endif
}

bool NavigationSystem::IsInUse() const
{
	return m_meshes.size() != 0;
}

MNM::TileID NavigationSystem::GetTileIdWhereLocationIsAtForMesh(NavigationMeshID meshID, const Vec3& location)
{
	NavigationMesh& mesh = GetMesh(meshID);

	const MNM::real_t range = MNM::real_t(1.0f);
	MNM::TriangleID triangleID = mesh.navMesh.GetTriangleAt(location, range, range);

	return MNM::ComputeTileID(triangleID);
}

void NavigationSystem::GetTileBoundsForMesh(NavigationMeshID meshID, MNM::TileID tileID, AABB& bounds) const
{
	const NavigationMesh& mesh = GetMesh(meshID);
	const MNM::vector3_t coords = mesh.navMesh.GetTileContainerCoordinates(tileID);

	const MNM::CNavMesh::SGridParams& params = mesh.navMesh.GetGridParams();

	Vec3 minPos((float)params.tileSize.x * coords.x.as_float(), (float)params.tileSize.y * coords.y.as_float(), (float)params.tileSize.z * coords.z.as_float());
	minPos += params.origin;

	bounds = AABB(minPos, minPos + Vec3((float)params.tileSize.x, (float)params.tileSize.y, (float)params.tileSize.z));
}

NavigationMesh& NavigationSystem::GetMesh(const NavigationMeshID& meshID)
{
	return const_cast<NavigationMesh&>(static_cast<const NavigationSystem*>(this)->GetMesh(meshID));
}

const NavigationMesh& NavigationSystem::GetMesh(const NavigationMeshID& meshID) const
{
	if (meshID && m_meshes.validate(meshID))
		return m_meshes[meshID];

	assert(0);
	static NavigationMesh dummy(NavigationAgentTypeID(0));

	return dummy;
}

NavigationMeshID NavigationSystem::GetEnclosingMeshID(NavigationAgentTypeID agentTypeID, const Vec3& location) const
{
	if (agentTypeID && agentTypeID <= m_agentTypes.size())
	{
		const AgentType& agentType = m_agentTypes[agentTypeID - 1];

		AgentType::Meshes::const_iterator mit = agentType.meshes.begin();
		AgentType::Meshes::const_iterator mend = agentType.meshes.end();

		for (; mit != mend; ++mit)
		{
			const NavigationMeshID meshID = mit->id;
			const NavigationMesh& mesh = m_meshes[meshID];
			const NavigationVolumeID boundaryID = mesh.boundary;

			if (boundaryID && m_volumes[boundaryID].Contains(location))
				return meshID;
		}
	}

	return NavigationMeshID();
}

bool NavigationSystem::IsLocationInMesh(NavigationMeshID meshID, const Vec3& location) const
{
	if (meshID && m_meshes.validate(meshID))
	{
		const NavigationMesh& mesh = m_meshes[meshID];
		const NavigationVolumeID boundaryID = mesh.boundary;

		return (boundaryID && m_volumes[boundaryID].Contains(location));
	}

	return false;
}

MNM::TriangleID NavigationSystem::GetClosestMeshLocation(NavigationMeshID meshID, const Vec3& location, float vrange,
                                                         float hrange, Vec3* meshLocation, float* distance) const
{
	if (meshID && m_meshes.validate(meshID))
	{
		MNM::vector3_t loc(MNM::real_t(location.x), MNM::real_t(location.y), MNM::real_t(location.z));
		const NavigationMesh& mesh = m_meshes[meshID];
		MNM::real_t verticalRange(vrange);
		if (const MNM::TriangleID enclosingTriID = mesh.navMesh.GetTriangleAt(loc, verticalRange, verticalRange))
		{
			if (meshLocation)
				*meshLocation = location;

			if (distance)
				*distance = 0.0f;

			return enclosingTriID;
		}
		else
		{
			MNM::real_t distanceFixed;
			MNM::vector3_t closest;

			if (const MNM::TriangleID closestTriID = mesh.navMesh.GetClosestTriangle(loc, MNM::real_t(vrange), MNM::real_t(hrange), &distanceFixed, &closest))
			{
				if (meshLocation)
					*meshLocation = closest.GetVec3();

				if (distance)
					*distance = distanceFixed.as_float();

				return closestTriID;
			}
		}
	}

	return MNM::TriangleID(0);
}

bool NavigationSystem::GetGroundLocationInMesh(NavigationMeshID meshID, const Vec3& location,
                                               float vDownwardRange, float hRange, Vec3* meshLocation) const
{
	if (meshID && m_meshes.validate(meshID))
	{
		MNM::vector3_t loc(MNM::real_t(location.x), MNM::real_t(location.y), MNM::real_t(location.z));
		const NavigationMesh& mesh = m_meshes[meshID];
		MNM::real_t verticalRange(vDownwardRange);
		if (const MNM::TriangleID enclosingTriID = mesh.navMesh.GetTriangleAt(loc, verticalRange, MNM::real_t(0.05f)))
		{
			MNM::vector3_t v0, v1, v2;
			mesh.navMesh.GetVertices(enclosingTriID, v0, v1, v2);
			MNM::vector3_t closest = ClosestPtPointTriangle(loc, v0, v1, v2);
			if (meshLocation)
				*meshLocation = closest.GetVec3();

			return true;
		}
		else
		{
			MNM::vector3_t closest;

			if (const MNM::TriangleID closestTriID = mesh.navMesh.GetClosestTriangle(loc, verticalRange, MNM::real_t(hRange), nullptr, &closest))
			{
				if (meshLocation)
					*meshLocation = closest.GetVec3();

				return true;
			}
		}
	}

	return false;
}

bool NavigationSystem::AgentTypeSupportSmartObjectUserClass(NavigationAgentTypeID agentTypeID, const char* smartObjectUserClass) const
{
	if (agentTypeID && agentTypeID <= m_agentTypes.size())
	{
		const AgentType& agentType = m_agentTypes[agentTypeID - 1];
		AgentType::SmartObjectUserClasses::const_iterator cit = agentType.smartObjectUserClasses.begin();
		AgentType::SmartObjectUserClasses::const_iterator end = agentType.smartObjectUserClasses.end();

		for (; cit != end; ++cit)
		{
			if (strcmp((*cit).c_str(), smartObjectUserClass))
			{
				continue;
			}

			return true;
		}
	}

	return false;
}

uint16 NavigationSystem::GetAgentRadiusInVoxelUnits(NavigationAgentTypeID agentTypeID) const
{
	if (agentTypeID && agentTypeID <= m_agentTypes.size())
	{
		return m_agentTypes[agentTypeID - 1].settings.radiusVoxelCount;
	}

	return 0;
}

uint16 NavigationSystem::GetAgentHeightInVoxelUnits(NavigationAgentTypeID agentTypeID) const
{
	if (agentTypeID && agentTypeID <= m_agentTypes.size())
	{
		return m_agentTypes[agentTypeID - 1].settings.heightVoxelCount;
	}

	return 0;
}

void NavigationSystem::Clear()
{
	StopAllTasks();
	SetupTasks();

	m_updatesManager.Clear();

	AgentTypes::iterator it = m_agentTypes.begin();
	AgentTypes::iterator end = m_agentTypes.end();
	for (; it != end; ++it)
	{
		AgentType& agentType = *it;
		agentType.meshes.clear();
		agentType.exclusions.clear();
	}

	for (uint16 i = 0; i < m_meshes.capacity(); ++i)
	{
		if (!m_meshes.index_free(i))
			DestroyMesh(NavigationMeshID(m_meshes.get_index_id(i)));
	}

	for (uint16 i = 0; i < m_volumes.capacity(); ++i)
	{
		if (!m_volumes.index_free(i))
			DestroyVolume(NavigationVolumeID(m_volumes.get_index_id(i)));
	}

	m_volumesManager.Clear();

#ifdef SW_NAVMESH_USE_GUID
	m_swMeshes.clear();
	m_swVolumes.clear();

	m_nextFreeMeshId = 0;
	m_nextFreeVolumeId = 0;
#endif

	m_worldAABB = AABB::RESET;

	m_volumeDefCopy.clear();
	m_volumeDefCopy.resize(MaxVolumeDefCopyCount, VolumeDefCopy());

	m_offMeshNavigationManager.Clear();
	m_islandConnectionsManager.Reset();

	ResetAllNavigationSystemUsers();
}

void NavigationSystem::ClearAndNotify()
{
	Clear();
	UpdateAllListener(NavigationCleared);

	//////////////////////////////////////////////////////////////////////////
	//After the 'clear' we need to re-enable and register smart objects again

	m_offMeshNavigationManager.Enable();
	gAIEnv.pSmartObjectManager->SoftReset();
}

bool NavigationSystem::ReloadConfig()
{
	// TODO pavloi 2016.03.09: this function should be refactored.
	// Proper way of doing things should be something like this:
	// 1) load config data into an array of CreateAgentTypeParams, do nothing, if there are errors
	// 2) pass array of CreateAgentTypeParams into different publically available function, which will:
	//    a) notify interested listeners, that we're about to wipe out and reload navigation;
	//    b) clear current data;
	//    c) replace agent settings;
	//    d) kick-off navmesh regeneration (if required/enabled);
	//    e) notify listeners, that new settings are available.
	// This way:
	// 1) loading and application of config will be decoupled from each other, so we will be able to put data in descriptor database (if we want);
	// 2) it will be possible to reliably reload config and provide better editor tools, which react to config changes in runtime;
	// 3) continuous nav mesh update can be taken away from editor's CAIManager to navigation system.

	Clear();

	m_agentTypes.clear();

	XmlNodeRef rootNode = GetISystem()->LoadXmlFromFile(m_configName.c_str());
	if (!rootNode)
	{
		AIWarning("Failed to open XML file '%s'...", m_configName.c_str());

		return false;
	}

	const char* tagName = rootNode->getTag();

	if (!stricmp(tagName, "Navigation"))
	{
		rootNode->getAttr("version", m_configurationVersion);

		size_t childCount = rootNode->getChildCount();

		for (size_t i = 0; i < childCount; ++i)
		{
			XmlNodeRef childNode = rootNode->getChild(i);

			if (!stricmp(childNode->getTag(), "AgentTypes"))
			{
				size_t agentTypeCount = childNode->getChildCount();

				for (size_t at = 0; at < agentTypeCount; ++at)
				{
					XmlNodeRef agentTypeNode = childNode->getChild(at);

					if (!agentTypeNode->haveAttr("name"))
					{
						AIWarning("Missing 'name' attribute for 'AgentType' tag at line %d while parsing NavigationXML '%s'...",
						          agentTypeNode->getLine(), m_configName.c_str());

						return false;
					}

					const char* name = 0;
					INavigationSystem::CreateAgentTypeParams params;

					for (size_t attr = 0; attr < (size_t)agentTypeNode->getNumAttributes(); ++attr)
					{
						const char* attrName = 0;
						const char* attrValue = 0;

						if (!agentTypeNode->getAttributeByIndex(attr, &attrName, &attrValue))
							continue;

						bool valid = false;
						if (!stricmp(attrName, "name"))
						{
							if (attrValue && *attrValue)
							{
								valid = true;
								name = attrValue;
							}
						}
						else if (!stricmp(attrName, "radius"))
						{
							int sradius = 0;
							if (sscanf(attrValue, "%d", &sradius) == 1 && sradius > 0)
							{
								valid = true;
								params.radiusVoxelCount = sradius;
							}
						}
						else if (!stricmp(attrName, "height"))
						{
							int sheight = 0;
							if (sscanf(attrValue, "%d", &sheight) == 1 && sheight > 0)
							{
								valid = true;
								params.heightVoxelCount = sheight;
							}
						}
						else if (!stricmp(attrName, "climbableHeight"))
						{
							int sclimbableheight = 0;
							if (sscanf(attrValue, "%d", &sclimbableheight) == 1 && sclimbableheight >= 0)
							{
								valid = true;
								params.climbableVoxelCount = sclimbableheight;
							}
						}
						else if (!stricmp(attrName, "climbableInclineGradient"))
						{
							float sclimbableinclinegradient = 0.f;
							if (sscanf(attrValue, "%f", &sclimbableinclinegradient) == 1)
							{
								valid = true;
								params.climbableInclineGradient = sclimbableinclinegradient;
							}
						}
						else if (!stricmp(attrName, "climbableStepRatio"))
						{
							float sclimbablestepratio = 0.f;
							if (sscanf(attrValue, "%f", &sclimbablestepratio) == 1)
							{
								valid = true;
								params.climbableStepRatio = sclimbablestepratio;
							}
						}
						else if (!stricmp(attrName, "maxWaterDepth"))
						{
							int smaxwaterdepth = 0;
							if (sscanf(attrValue, "%d", &smaxwaterdepth) == 1 && smaxwaterdepth >= 0)
							{
								valid = true;
								params.maxWaterDepthVoxelCount = smaxwaterdepth;
							}
						}
						else if (!stricmp(attrName, "voxelSize"))
						{
							float x, y, z;
							int c = sscanf(attrValue, "%g,%g,%g", &x, &y, &z);

							valid = (c == 1) || (c == 3);
							if (c == 1)
								params.voxelSize = Vec3(x);
							else if (c == 3)
								params.voxelSize = Vec3(x, y, z);
						}
						else
						{
							AIWarning(
							  "Unknown attribute '%s' for '%s' tag found at line %d while parsing Navigation XML '%s'...",
							  attrName, agentTypeNode->getTag(), agentTypeNode->getLine(), m_configName.c_str());

							return false;
						}

						if (!valid)
						{
							AIWarning("Invalid '%s' attribute value for '%s' tag at line %d while parsing NavigationXML '%s'...",
							          attrName, agentTypeNode->getTag(), agentTypeNode->getLine(), m_configName.c_str());

							return false;
						}
					}

					for (size_t childIdx = 0; childIdx < m_agentTypes.size(); ++childIdx)
					{
						const AgentType& agentType = m_agentTypes[childIdx];

						assert(name);

						if (!stricmp(agentType.name.c_str(), name))
						{
							AIWarning("AgentType '%s' redefinition at line %d while parsing NavigationXML '%s'...",
							          name, agentTypeNode->getLine(), m_configName.c_str());

							return false;
						}
					}

					NavigationAgentTypeID agentTypeID = CreateAgentType(name, params);
					if (!agentTypeID)
						return false;

					//////////////////////////////////////////////////////////////////////////
					/// Add supported SO classes for this AgentType/Mesh

					for (size_t childIdx = 0; childIdx < (size_t)agentTypeNode->getChildCount(); ++childIdx)
					{
						XmlNodeRef agentTypeChildNode = agentTypeNode->getChild(childIdx);

						if (!stricmp(agentTypeChildNode->getTag(), "SmartObjectUserClasses"))
						{
							AgentType& agentType = m_agentTypes[agentTypeID - 1];

							size_t soClassesCount = agentTypeChildNode->getChildCount();
							agentType.smartObjectUserClasses.reserve(soClassesCount);

							for (size_t socIdx = 0; socIdx < soClassesCount; ++socIdx)
							{
								XmlNodeRef smartObjectClassNode = agentTypeChildNode->getChild(socIdx);

								if (!stricmp(smartObjectClassNode->getTag(), "class") && smartObjectClassNode->haveAttr("name"))
								{
									stl::push_back_unique(agentType.smartObjectUserClasses, smartObjectClassNode->getAttr("name"));
								}
							}
						}
					}
				}
			}
			else
			{
				AIWarning(
				  "Unexpected tag '%s' found at line %d while parsing Navigation XML '%s'...",
				  childNode->getTag(), childNode->getLine(), m_configName.c_str());

				return false;
			}
		}

		return true;
	}
	else
	{
		AIWarning(
		  "Unexpected tag '%s' found at line %d while parsing Navigation XML '%s'...",
		  rootNode->getTag(), rootNode->getLine(), m_configName.c_str());
	}

	return false;
}

void NavigationSystem::ComputeWorldAABB()
{
	AgentTypes::const_iterator it = m_agentTypes.begin();
	AgentTypes::const_iterator end = m_agentTypes.end();

	m_worldAABB = AABB(AABB::RESET);

	for (; it != end; ++it)
	{
		const AgentType& agentType = *it;

		AgentType::Meshes::const_iterator mit = agentType.meshes.begin();
		AgentType::Meshes::const_iterator mend = agentType.meshes.end();

		for (; mit != mend; ++mit)
		{
			const NavigationMeshID meshID = mit->id;
			const NavigationMesh& mesh = m_meshes[meshID];

			if (mesh.boundary)
				m_worldAABB.Add(m_volumes[mesh.boundary].aabb);
		}
	}
}

void NavigationSystem::SetupTasks()
{
	// the number of parallel tasks while allowing maximization of tile throughput
	// might also slow down the main thread due to extra time processing them but also because
	// the connection of each tile to network is done on the main thread
	// NOTE ChrisR: capped to half the amount. The execution time of a tile job desperately needs to optimized,
	// it cannot not take more than the frame time because it'll stall the system. No clever priority scheme
	// will ever,ever,ever make that efficient.
	// PeteB: Optimized the tile job time enough to make it viable to do more than one per frame. Added CVar
	//        multiplier to allow people to control it based on the speed of their machine.
	m_maxRunningTaskCount = (gEnv->pJobManager->GetNumWorkerThreads() * 3 / 4) * gAIEnv.CVars.NavGenThreadJobs;
	m_results.resize(m_maxRunningTaskCount);

	for (uint16 i = 0; i < m_results.size(); ++i)
		m_results[i].next = i + 1;

	m_runningTasks.reserve(m_maxRunningTaskCount);

	m_free = 0;
}

bool NavigationSystem::RegisterArea(const char* shapeName, NavigationVolumeID& outVolumeId)
{
	return m_volumesManager.RegisterArea(shapeName, outVolumeId);
}

void NavigationSystem::UnRegisterArea(const char* shapeName)
{
	m_volumesManager.UnRegisterArea(shapeName);
}

NavigationVolumeID NavigationSystem::GetAreaId(const char* shapeName) const
{
	return m_volumesManager.GetAreaID(shapeName);
}

void NavigationSystem::SetAreaId(const char* shapeName, NavigationVolumeID id)
{
	m_volumesManager.SetAreaID(shapeName, id);
}

void NavigationSystem::UpdateAreaNameForId(const NavigationVolumeID id, const char* newShapeName)
{
	m_volumesManager.UpdateNameForAreaID(id, newShapeName);
}

void NavigationSystem::RemoveLoadedMeshesWithoutRegisteredAreas()
{
	std::vector<NavigationVolumeID> volumesToRemove;
	m_volumesManager.GetLoadedUnregisteredVolumes(volumesToRemove);

	if (!volumesToRemove.empty())
	{
		std::vector<NavigationMeshID> meshesToRemove;

		for (const NavigationVolumeID& volumeId : volumesToRemove)
		{
			AILogComment("Removing Navigation Volume id = %u because it was created by loaded unregistered navigation area.",
			             (uint32)volumeId);

			for (const AgentType& agentType : m_agentTypes)
			{
				for (const AgentType::MeshInfo& meshInfo : agentType.meshes)
				{
					const NavigationMeshID meshID = meshInfo.id;
					const NavigationMesh& mesh = m_meshes[meshID];

					if (mesh.boundary == volumeId)
					{
						meshesToRemove.push_back(meshID);
						AILogComment("Removing NavMesh '%s' (meshId = %u, agent = %s) because it uses loaded unregistered navigation area id = %u.",
						             mesh.name.c_str(), (uint32)meshID, agentType.name.c_str(), (uint32)volumeId);
					}
				}
			}
		}

		for (const NavigationMeshID& meshId : meshesToRemove)
		{
			DestroyMesh(meshId);
		}

		for (const NavigationVolumeID& volumeId : volumesToRemove)
		{
			DestroyVolume(volumeId);
		}
	}

	m_volumesManager.ClearLoadedAreas();
}

void NavigationSystem::StartWorldMonitoring()
{
	m_worldMonitor.Start();
}

void NavigationSystem::StopWorldMonitoring()
{
	m_worldMonitor.Stop();
}

bool NavigationSystem::GetClosestPointInNavigationMesh(const NavigationAgentTypeID agentID, const Vec3& location, float vrange, float hrange, Vec3* meshLocation, float minIslandArea) const
{
	const NavigationMeshID meshID = GetEnclosingMeshID(agentID, location);
	if (meshID && m_meshes.validate(meshID))
	{
		MNM::vector3_t loc(MNM::real_t(location.x), MNM::real_t(location.y), MNM::real_t(location.z));
		const NavigationMesh& mesh = m_meshes[meshID];
		MNM::real_t verticalRange(vrange);

		//first check vertical range, because if we are over navmesh, we want that one
		if (const MNM::TriangleID enclosingTriID = mesh.navMesh.GetTriangleAt(loc, verticalRange, verticalRange, minIslandArea))
		{
			MNM::vector3_t v0, v1, v2;
			mesh.navMesh.GetVertices(enclosingTriID, v0, v1, v2);
			MNM::vector3_t closest = ClosestPtPointTriangle(loc, v0, v1, v2);

			if (meshLocation)
			{
				*meshLocation = closest.GetVec3();
			}

			return true;
		}
		else
		{
			MNM::vector3_t closest;

			if (const MNM::TriangleID closestTriID = mesh.navMesh.GetClosestTriangle(loc, MNM::real_t(vrange), MNM::real_t(hrange), nullptr, &closest, minIslandArea))
			{
				if (meshLocation)
				{
					*meshLocation = closest.GetVec3();
				}

				return true;
			}
		}
	}

	return false;
}

bool NavigationSystem::IsLocationValidInNavigationMesh(const NavigationAgentTypeID agentID, const Vec3& location) const
{
	const NavigationMeshID meshID = GetEnclosingMeshID(agentID, location);
	bool isValid = false;
	const float horizontalRange = 1.0f;
	const float verticalRange = 1.0f;
	if (meshID)
	{
		Vec3 locationInMesh(ZERO);
		float accuracy = .0f;
		const MNM::TriangleID triangleID = GetClosestMeshLocation(meshID, location, verticalRange, horizontalRange, &locationInMesh, &accuracy);
		isValid = (triangleID && accuracy == .0f);
	}
	return isValid;
}

bool NavigationSystem::IsPointReachableFromPosition(const NavigationAgentTypeID agentID, const IEntity* pEntityToTestOffGridLinks, const Vec3& startLocation, const Vec3& endLocation) const
{
	const float horizontalRange = 1.0f;
	const float verticalRange = 1.0f;

	MNM::GlobalIslandID startingIslandID;
	const NavigationMeshID startingMeshID = GetEnclosingMeshID(agentID, startLocation);
	if (startingMeshID)
	{
		const MNM::TriangleID triangleID = GetClosestMeshLocation(startingMeshID, startLocation, verticalRange, horizontalRange, NULL, NULL);
		const NavigationMesh& mesh = m_meshes[startingMeshID];
		const MNM::CNavMesh& navMesh = mesh.navMesh;
		MNM::Tile::STriangle triangle;
		if (triangleID && navMesh.GetTriangle(triangleID, triangle) && (triangle.islandID != MNM::Constants::eStaticIsland_InvalidIslandID))
		{
			startingIslandID = MNM::GlobalIslandID(startingMeshID, triangle.islandID);
		}
	}

	MNM::GlobalIslandID endingIslandID;
	const NavigationMeshID endingMeshID = GetEnclosingMeshID(agentID, endLocation);
	if (endingMeshID)
	{
		const MNM::TriangleID triangleID = GetClosestMeshLocation(endingMeshID, endLocation, verticalRange, horizontalRange, NULL, NULL);
		const NavigationMesh& mesh = m_meshes[endingMeshID];
		const MNM::CNavMesh& navMesh = mesh.navMesh;
		MNM::Tile::STriangle triangle;
		if (triangleID && navMesh.GetTriangle(triangleID, triangle) && (triangle.islandID != MNM::Constants::eStaticIsland_InvalidIslandID))
		{
			endingIslandID = MNM::GlobalIslandID(endingMeshID, triangle.islandID);
		}
	}

	return m_islandConnectionsManager.AreIslandsConnected(pEntityToTestOffGridLinks, startingIslandID, endingIslandID);
}

MNM::GlobalIslandID NavigationSystem::GetGlobalIslandIdAtPosition(const NavigationAgentTypeID agentID, const Vec3& location)
{
	const float horizontalRange = 1.0f;
	const float verticalRange = 1.0f;

	MNM::GlobalIslandID startingIslandID;
	const NavigationMeshID startingMeshID = GetEnclosingMeshID(agentID, location);
	if (startingMeshID)
	{
		const MNM::TriangleID triangleID = GetClosestMeshLocation(startingMeshID, location, verticalRange, horizontalRange, NULL, NULL);
		const NavigationMesh& mesh = m_meshes[startingMeshID];
		const MNM::CNavMesh& navMesh = mesh.navMesh;
		MNM::Tile::STriangle triangle;
		if (triangleID && navMesh.GetTriangle(triangleID, triangle) && (triangle.islandID != MNM::Constants::eStaticIsland_InvalidIslandID))
		{
			startingIslandID = MNM::GlobalIslandID(startingMeshID, triangle.islandID);
		}
	}

	return startingIslandID;
}

bool NavigationSystem::IsLocationContainedWithinTriangleInNavigationMesh(const NavigationAgentTypeID agentID, const Vec3& location, float downRange, float upRange) const
{
	if (const NavigationMeshID meshID = GetEnclosingMeshID(agentID, location))
	{
		if (m_meshes.validate(meshID))
		{
			MNM::vector3_t loc(MNM::real_t(location.x), MNM::real_t(location.y), MNM::real_t(location.z));
			const NavigationMesh& mesh = m_meshes[meshID];
			const MNM::TriangleID enclosingTriID = mesh.navMesh.GetTriangleAt(loc, MNM::real_t(downRange), MNM::real_t(upRange));
			return enclosingTriID != 0;
		}
	}

	return false;
}

MNM::TriangleID NavigationSystem::GetTriangleIDWhereLocationIsAtForMesh(const NavigationAgentTypeID agentID, const Vec3& location)
{
	NavigationMeshID meshId = GetEnclosingMeshID(agentID, location);
	if (meshId)
	{
		NavigationMesh& mesh = GetMesh(meshId);
		const MNM::CNavMesh::SGridParams& paramsGrid = mesh.navMesh.GetGridParams();
		const MNM::OffMeshNavigation& offMeshNavigation = GetOffMeshNavigationManager()->GetOffMeshNavigationForMesh(meshId);

		const Vec3& voxelSize = mesh.navMesh.GetGridParams().voxelSize;
		const uint16 agentHeightUnits = GetAgentHeightInVoxelUnits(agentID);

		const MNM::real_t verticalRange = MNMUtils::CalculateMinVerticalRange(agentHeightUnits, voxelSize.z);
		const MNM::real_t verticalDownwardRange(verticalRange);

		AgentType agentTypeProperties;
		const bool arePropertiesValid = GetAgentTypeProperties(agentID, agentTypeProperties);
		assert(arePropertiesValid);
		const uint16 minZOffsetMultiplier(2);
		const uint16 zOffsetMultiplier = min(minZOffsetMultiplier, agentTypeProperties.settings.heightVoxelCount);
		const MNM::real_t verticalUpwardRange = arePropertiesValid ? MNM::real_t(zOffsetMultiplier * agentTypeProperties.settings.voxelSize.z) : MNM::real_t(.2f);

		return mesh.navMesh.GetTriangleAt(location - paramsGrid.origin, verticalDownwardRange, verticalUpwardRange);
	}

	return MNM::TriangleID(0);
}

const MNM::INavMesh* NavigationSystem::GetMNMNavMesh(NavigationMeshID meshID) const
{
	if (m_meshes.validate(meshID))
	{
		const NavigationMesh& mesh = m_meshes[meshID];
		return &mesh.navMesh;
	}
	return nullptr;
}

size_t NavigationSystem::GetTriangleCenterLocationsInMesh(const NavigationMeshID meshID, const Vec3& location, const AABB& searchAABB, Vec3* centerLocations, size_t maxCenterLocationCount, float minIslandArea) const
{
	if (m_meshes.validate(meshID))
	{
		const MNM::vector3_t min(MNM::real_t(searchAABB.min.x), MNM::real_t(searchAABB.min.y), MNM::real_t(searchAABB.min.z));
		const MNM::vector3_t max(MNM::real_t(searchAABB.max.x), MNM::real_t(searchAABB.max.y), MNM::real_t(searchAABB.max.z));
		const NavigationMesh& mesh = m_meshes[meshID];
		const MNM::aabb_t aabb(min, max);
		const size_t maxTriangleCount = 4096;
		MNM::TriangleID triangleIDs[maxTriangleCount];
		const size_t triangleCount = mesh.navMesh.GetTriangles(aabb, triangleIDs, maxTriangleCount, minIslandArea);
		MNM::Tile::STriangle triangle;

		if (triangleCount > 0)
		{
			MNM::vector3_t a, b, c;

			size_t i = 0;
			size_t num_tris = 0;
			for (i = 0; i < triangleCount; ++i)
			{
				mesh.navMesh.GetVertices(triangleIDs[i], a, b, c);
				centerLocations[num_tris] = ((a + b + c) * MNM::real_t(0.33333f)).GetVec3();
				num_tris++;

				if (num_tris == maxCenterLocationCount)
					return num_tris;
			}

			return num_tris;
		}
	}

	return 0;
}

size_t NavigationSystem::GetTriangleBorders(const NavigationMeshID meshID, const AABB& aabb, Vec3* pBorders, size_t maxBorderCount, float minIslandArea) const
{
	size_t numBorders = 0;

	if (m_meshes.validate(meshID))
	{
		const MNM::vector3_t min(MNM::real_t(aabb.min.x), MNM::real_t(aabb.min.y), MNM::real_t(aabb.min.z));
		const MNM::vector3_t max(MNM::real_t(aabb.max.x), MNM::real_t(aabb.max.y), MNM::real_t(aabb.max.z));
		const NavigationMesh& mesh = m_meshes[meshID];
		const MNM::aabb_t aabb(min, max);
		const size_t maxTriangleCount = 4096;
		MNM::TriangleID triangleIDs[maxTriangleCount];
		const size_t triangleCount = mesh.navMesh.GetTriangles(aabb, triangleIDs, maxTriangleCount, minIslandArea);
		//MNM::Tile::Triangle triangle;

		if (triangleCount > 0)
		{
			MNM::vector3_t verts[3];

			for (size_t i = 0; i < triangleCount; ++i)
			{
				size_t linkedEdges = 0;
				mesh.navMesh.GetLinkedEdges(triangleIDs[i], linkedEdges);
				mesh.navMesh.GetVertices(triangleIDs[i], verts[0], verts[1], verts[2]);

				for (size_t e = 0; e < 3; ++e)
				{
					if ((linkedEdges & (size_t(1) << e)) == 0)
					{
						if (pBorders != NULL)
						{
							const Vec3 v0 = verts[e].GetVec3();
							const Vec3 v1 = verts[(e + 1) % 3].GetVec3();
							const Vec3 vOther = verts[(e + 2) % 3].GetVec3();

							const Vec3 edge = Vec3(v0 - v1).GetNormalized();
							const Vec3 otherEdge = Vec3(v0 - vOther).GetNormalized();
							const Vec3 up = edge.Cross(otherEdge);
							const Vec3 out = up.Cross(edge);

							pBorders[numBorders * 3 + 0] = v0;
							pBorders[numBorders * 3 + 1] = v1;
							pBorders[numBorders * 3 + 2] = out;
						}

						++numBorders;

						if (pBorders != NULL && numBorders == maxBorderCount)
							return numBorders;
					}
				}
			}
		}
	}

	return numBorders;
}

size_t NavigationSystem::GetTriangleInfo(const NavigationMeshID meshID, const AABB& aabb, Vec3* centerLocations, uint32* islandids, size_t max_count, float minIslandArea) const
{
	if (m_meshes.validate(meshID))
	{
		const MNM::vector3_t min(MNM::real_t(aabb.min.x), MNM::real_t(aabb.min.y), MNM::real_t(aabb.min.z));
		const MNM::vector3_t max(MNM::real_t(aabb.max.x), MNM::real_t(aabb.max.y), MNM::real_t(aabb.max.z));
		const NavigationMesh& mesh = m_meshes[meshID];
		const MNM::aabb_t aabb(min, max);
		const size_t maxTriangleCount = 4096;
		MNM::TriangleID triangleIDs[maxTriangleCount];
		const size_t triangleCount = mesh.navMesh.GetTriangles(aabb, triangleIDs, maxTriangleCount, minIslandArea);
		MNM::Tile::STriangle triangle;

		if (triangleCount > 0)
		{
			MNM::vector3_t a, b, c;

			size_t i = 0;
			size_t num_tris = 0;
			for (i = 0; i < triangleCount; ++i)
			{
				mesh.navMesh.GetTriangle(triangleIDs[i], triangle);
				mesh.navMesh.GetVertices(triangleIDs[i], a, b, c);
				centerLocations[num_tris] = ((a + b + c) * MNM::real_t(0.33333f)).GetVec3();
				islandids[num_tris] = triangle.islandID;
				num_tris++;

				if (num_tris == max_count)
					return num_tris;
			}

			return num_tris;
		}
	}

	return 0;
}

// Helper function to read various navigationId types from file without creating intermediate uint32.
template<typename TId>
static void ReadNavigationIdType(CCryFile& file, TId& outId)
{
	static_assert(sizeof(TId) == sizeof(uint32), "Navigation ID underlying type have changed");
	uint32 id;
	file.ReadType(&id);
	outId = TId(id);
}

template<typename TId>
static void WriteNavigationIdType(CCryFile& file, const TId& id)
{
	static_assert(sizeof(TId) == sizeof(uint32), "Navigation ID underlying type have changed");
	const uint32 uid = id;
	file.WriteType<uint32>(&uid);
}

bool NavigationSystem::ReadFromFile(const char* fileName, bool bAfterExporting)
{
	MEMSTAT_CONTEXT(EMemStatContextTypes::MSC_Other, 0, "Navigation Meshes (Read File)");

	bool fileLoaded = false;

	m_pEditorBackgroundUpdate->Pause(true);

	m_volumesManager.ClearLoadedAreas();

	CCryFile file;
	if (false != file.Open(fileName, "rb"))
	{
		bool fileVersionCompatible = true;

		uint16 nFileVersion = BAI_NAVIGATION_FILE_VERSION;
		file.ReadType(&nFileVersion);

		//Verify version of exported file in first place
		if (nFileVersion != BAI_NAVIGATION_FILE_VERSION)
		{
			AIWarning("Wrong BAI file version (found %d expected %d)!! Regenerate Navigation data in the editor.",
			          nFileVersion, BAI_NAVIGATION_FILE_VERSION);

			fileVersionCompatible = false;
		}
		else
		{
			uint32 nConfigurationVersion = 0;
			file.ReadType(&nConfigurationVersion);

			if (nConfigurationVersion != m_configurationVersion)
			{
				AIWarning("Navigation.xml config version mismatch (found %d expected %d)!! Regenerate Navigation data in the editor.",
				          nConfigurationVersion, m_configurationVersion);

				// In the launcher we still read the navigation data even if the configuration file
				// contains different version than the exported one
				if (gEnv->IsEditor())
					fileVersionCompatible = false;
			}
#ifdef SW_NAVMESH_USE_GUID
			else
			{
				uint32 useGUID = 0;
				file.ReadType(&useGUID);
				if (useGUID != BAI_NAVIGATION_GUID_FLAG)
				{
					AIWarning("Navigation GUID config mismatch (found %d expected %d)!! Regenerate Navigation data in the editor.",
					          useGUID, BAI_NAVIGATION_GUID_FLAG);

					fileVersionCompatible = false;
				}
			}
#endif
		}

		if (fileVersionCompatible)
		{
			// Loading boundary volumes, their ID's and names
			{
				std::vector<Vec3> volumeVerticesBuffer;
				std::vector<char> volumeAreaNameBuffer;
				string volumeAreaName;

				uint32 usedVolumesCount;
				file.ReadType(&usedVolumesCount);

				for (uint32 idx = 0; idx < usedVolumesCount; ++idx)
				{
					// Read volume data
					NavigationVolumeID volumeId;
					float volumeHeight;
					uint32 verticesCount;
					uint32 volumeAreaNameSize;

					ReadNavigationIdType(file, volumeId);
					file.ReadType(&volumeHeight);
					file.ReadType(&verticesCount);

					volumeVerticesBuffer.resize(verticesCount);
					for (uint32 vtxIdx = 0; vtxIdx < verticesCount; ++vtxIdx)
					{
						Vec3& vtx = volumeVerticesBuffer[vtxIdx];
						file.ReadType(&vtx.x, 3);
					}

					file.ReadType(&volumeAreaNameSize);
					if (volumeAreaNameSize > 0)
					{
						volumeAreaNameBuffer.resize(volumeAreaNameSize, '\0');
						file.ReadType(&volumeAreaNameBuffer.front(), volumeAreaNameSize);

						volumeAreaName.assign(&volumeAreaNameBuffer.front(), (&volumeAreaNameBuffer.back()) + 1);
					}
					else
					{
						volumeAreaName.clear();
					}

					// Create volume

					if (volumeId == NavigationVolumeID())
					{
						AIWarning("NavigationSystem::ReadFromFile: file contains invalid Navigation Volume ID");
						continue;
					}

					if (m_volumes.validate(volumeId))
					{
						AIWarning("NavigationSystem::ReadFromFile: Navigation Volume with volumeId=%u (name '%s') is already registered", (unsigned int)volumeId, volumeAreaName.c_str());
						continue;
					}

					const NavigationVolumeID createdVolumeId = CreateVolume(&volumeVerticesBuffer.front(), verticesCount, volumeHeight, volumeId);
					CRY_ASSERT(volumeId == createdVolumeId);

					m_volumesManager.RegisterAreaFromLoadedData(volumeAreaName.c_str(), volumeId);
				}
			}

			uint32 agentsCount;
			file.ReadType(&agentsCount);
			for (uint32 i = 0; i < agentsCount; ++i)
			{
				uint32 nameLength;
				file.ReadType(&nameLength);
				char agentName[MAX_NAME_LENGTH];
				nameLength = std::min(nameLength, (uint32)MAX_NAME_LENGTH - 1);
				file.ReadType(agentName, nameLength);
				agentName[nameLength] = '\0';

				// Reading total amount of memory used for the current agent
				uint32 totalAgentMemory = 0;
				file.ReadType(&totalAgentMemory);

				const size_t fileSeekPositionForNextAgent = file.GetPosition() + totalAgentMemory;
				const NavigationAgentTypeID agentTypeID = GetAgentTypeID(agentName);
				if (agentTypeID == 0)
				{
					AIWarning("The agent '%s' doesn't exist between the ones loaded from the Navigation.xml", agentName);
					file.Seek(fileSeekPositionForNextAgent, SEEK_SET);
					continue;
				}
				// ---------------------------------------------
				// Reading navmesh for the different agents type

				uint32 meshesCount = 0;
				file.ReadType(&meshesCount);

				for (uint32 meshCounter = 0; meshCounter < meshesCount; ++meshCounter)
				{
					// Reading mesh id
#ifdef SW_NAVMESH_USE_GUID
					NavigationMeshGUID meshGUID = 0;
					file.ReadType(&meshGUID);
#else
					uint32 meshIDuint32 = 0;
					file.ReadType(&meshIDuint32);
#endif

					// Reading mesh name
					uint32 meshNameLength = 0;
					file.ReadType(&meshNameLength);
					char meshName[MAX_NAME_LENGTH];
					meshNameLength = std::min(meshNameLength, (uint32)MAX_NAME_LENGTH - 1);
					file.ReadType(meshName, meshNameLength);
					meshName[meshNameLength] = '\0';

					// Reading the amount of islands in the mesh
					MNM::StaticIslandID totalIslands = 0;
					file.ReadType(&totalIslands);

					// Reading total mesh memory
					uint32 totalMeshMemory = 0;
					file.ReadType(&totalMeshMemory);

					const size_t fileSeekPositionForNextMesh = file.GetPosition() + totalMeshMemory;

					// Reading mesh boundary
#ifdef SW_NAVMESH_USE_GUID
					NavigationVolumeGUID boundaryGUID = 0;
					file.ReadType(&boundaryGUID);
#else
					NavigationVolumeID boundaryID;
					ReadNavigationIdType(file, boundaryID);
#endif

					{
						if (m_volumesManager.GetLoadedAreaID(meshName) != boundaryID)
						{
							AIWarning("The NavMesh '%s' (agent = '%s', meshId = %u, boundaryVolumeId = %u) and the loaded corresponding Navigation Area have different IDs. Data might be corrupted.",
							          meshName, agentName, meshIDuint32, (uint32)boundaryID);
						}

						const NavigationVolumeID existingAreaId = m_volumesManager.GetAreaID(meshName);
						if (existingAreaId != boundaryID)
						{
							if (!m_volumesManager.IsAreaPresent(meshName))
							{
								if (!m_volumesManager.IsLoadedAreaPresent(meshName))
								{
									AIWarning("The NavMesh '%s' (agent = '%s', meshId = %u, boundaryVolumeId = %u) doesn't have a loaded corresponding Navigation Area. Data might be corrupted.",
									          meshName, agentName, meshIDuint32, (uint32)boundaryID);
								}
							}
							else
							{
								if (existingAreaId == NavigationVolumeID() && bAfterExporting)
								{
									// Expected situation
								}
								else
								{
									AIWarning("The NavMesh '%s' (agent = '%s', meshId = %u, boundaryVolumeId = %u) and the existing corresponding Navigation Area have different IDs. Data might be corrupted.",
									          meshName, agentName, meshIDuint32, (uint32)boundaryID);
								}
							}
						}
					}

					// Reading mesh exclusion shapes
					uint32 exclusionShapesCount = 0;
					file.ReadType(&exclusionShapesCount);
					AgentType::ExclusionVolumes exclusions;
#ifdef SW_NAVMESH_USE_GUID
					for (uint32 exclusionsCounter = 0; exclusionsCounter < exclusionShapesCount; ++exclusionsCounter)
					{
						NavigationVolumeGUID exclusionGUID = 0;
						file.ReadType(&exclusionGUID);
						VolumeMap::iterator it = m_swVolumes.find(exclusionGUID);
						if (it != m_swVolumes.end())
							exclusions.push_back(it->second);
					}
#else
					exclusions.reserve(exclusionShapesCount);
					for (uint32 exclusionsCounter = 0; exclusionsCounter < exclusionShapesCount; ++exclusionsCounter)
					{
						NavigationVolumeID exclusionId;
						ReadNavigationIdType(file, exclusionId);
						// Save the exclusion shape with the read ID
						exclusions.push_back(exclusionId);
					}
#endif
					m_agentTypes[agentTypeID - 1].exclusions = exclusions;

					// Reading tile count
					uint32 tilesCount = 0;
					file.ReadType(&tilesCount);

					// Reading NavMesh grid params
					MNM::CNavMesh::SGridParams params;
					file.ReadType(&(params.origin.x));
					file.ReadType(&(params.origin.y));
					file.ReadType(&(params.origin.z));
					file.ReadType(&(params.tileSize.x));
					file.ReadType(&(params.tileSize.y));
					file.ReadType(&(params.tileSize.z));
					file.ReadType(&(params.voxelSize.x));
					file.ReadType(&(params.voxelSize.y));
					file.ReadType(&(params.voxelSize.z));
					file.ReadType(&(params.tileCount));
					// If we are full reloading the mnm then we also want to create a new grid with the parameters
					// written in the file

					CreateMeshParams createParams;
					createParams.origin = params.origin;
					createParams.tileSize = params.tileSize;
					createParams.tileCount = tilesCount;

#ifdef SW_NAVMESH_USE_GUID
					NavigationMeshID newMeshID = CreateMesh(meshName, agentTypeID, createParams, meshGUID);
#else
					NavigationMeshID newMeshID = NavigationMeshID(meshIDuint32);
					if (!m_meshes.validate(meshIDuint32))
						newMeshID = CreateMesh(meshName, agentTypeID, createParams, newMeshID);
#endif

					if (newMeshID == 0)
					{
						AIWarning("Unable to create mesh '%s'", meshName);
						file.Seek(fileSeekPositionForNextMesh, SEEK_SET);
						continue;
					}

#if !defined(SW_NAVMESH_USE_GUID)
					if (newMeshID != meshIDuint32)
					{
						AIWarning("The restored mesh has a different ID compared to the saved one.");
					}
#endif

					NavigationMesh& mesh = m_meshes[newMeshID];
#ifdef SW_NAVMESH_USE_GUID
					SetMeshBoundaryVolume(newMeshID, boundaryID, boundaryGUID);
#else
					SetMeshBoundaryVolume(newMeshID, boundaryID);
#endif
					mesh.exclusions = exclusions;
					mesh.navMesh.SetTotalIslands(totalIslands);
					for (uint32 j = 0; j < tilesCount; ++j)
					{
						// Reading Tile indexes
						uint16 x, y, z;
						uint32 hashValue;
						file.ReadType(&x);
						file.ReadType(&y);
						file.ReadType(&z);
						file.ReadType(&hashValue);

						// Reading triangles
						uint16 triangleCount = 0;
						file.ReadType(&triangleCount);
						std::unique_ptr<MNM::Tile::STriangle[]> pTriangles;
						if (triangleCount)
						{
							pTriangles.reset(new MNM::Tile::STriangle[triangleCount]);
							file.ReadType(pTriangles.get(), triangleCount);
						}

						// Reading Vertices
						uint16 vertexCount = 0;
						file.ReadType(&vertexCount);
						std::unique_ptr<MNM::Tile::Vertex[]> pVertices;
						if (vertexCount)
						{
							pVertices.reset(new MNM::Tile::Vertex[vertexCount]);
							file.ReadType(pVertices.get(), vertexCount);
						}

						// Reading Links
						uint16 linkCount;
						file.ReadType(&linkCount);
						std::unique_ptr<MNM::Tile::SLink[]> pLinks;
						if (linkCount)
						{
							pLinks.reset(new MNM::Tile::SLink[linkCount]);
							file.ReadType(pLinks.get(), linkCount);
						}

						// Reading nodes
						uint16 nodeCount;
						file.ReadType(&nodeCount);
						std::unique_ptr<MNM::Tile::SBVNode[]> pNodes;
						if (nodeCount)
						{
							pNodes.reset(new MNM::Tile::SBVNode[nodeCount]);
							file.ReadType(pNodes.get(), nodeCount);
						}

						// Creating and swapping the tile
						MNM::STile tile = MNM::STile();
						tile.SetTriangles(std::move(pTriangles), triangleCount);
						tile.SetVertices(std::move(pVertices), vertexCount);
						tile.SetLinks(std::move(pLinks), linkCount);
						tile.SetNodes(std::move(pNodes), nodeCount);
						tile.SetHashValue(hashValue);

						mesh.navMesh.SetTile(x, y, z, tile);
					}
				}
			}

			fileLoaded = true;
		}

		file.Close();
	}

	m_volumesManager.ValidateAndSanitizeLoadedAreas(*this);

	ENavigationEvent navigationEvent = (bAfterExporting) ? MeshReloadedAfterExporting : MeshReloaded;
	UpdateAllListener(navigationEvent);

	m_offMeshNavigationManager.OnNavigationLoadedComplete();

	m_pEditorBackgroundUpdate->Pause(false);

	return fileLoaded;
}

//////////////////////////////////////////////////////////////////////////
/// From the original tile, it generates triangles and links with no off-mesh information
/// Returns the new number of links (triangle count is the same)
#define DO_FILTERING_CONSISTENCY_CHECK 0

uint16 FilterOffMeshLinksForTile(const MNM::STile& tile, MNM::Tile::STriangle* pTrianglesBuffer, uint16 trianglesBufferSize, MNM::Tile::SLink* pLinksBuffer, uint16 linksBufferSize)
{
	assert(pTrianglesBuffer);
	assert(pLinksBuffer);
	assert(tile.GetTrianglesCount() <= trianglesBufferSize);
	assert(tile.GetLinksCount() <= linksBufferSize);

	uint16 newLinkCount = 0;
	uint16 offMeshLinksCount = 0;

	const uint16 trianglesCount = tile.GetTrianglesCount();
	const MNM::Tile::STriangle* pTriangles = tile.GetTriangles();

	if (const MNM::Tile::SLink* pLinks = tile.GetLinks())
	{
		const uint16 linksCount = tile.GetLinksCount();

		//Re-adjust link indices for triangles
		for (uint16 t = 0; t < trianglesCount; ++t)
		{
			const MNM::Tile::STriangle& triangle = pTriangles[t];
			pTrianglesBuffer[t] = triangle;

			pTrianglesBuffer[t].firstLink = triangle.firstLink - offMeshLinksCount;

			if ((triangle.linkCount > 0) && (pLinks[triangle.firstLink].side == MNM::Tile::SLink::OffMesh))
			{
				pTrianglesBuffer[t].linkCount--;
				offMeshLinksCount++;
			}
		}

		//Now copy links except off-mesh ones
		for (uint16 l = 0; l < linksCount; ++l)
		{
			const MNM::Tile::SLink& link = pLinks[l];

			if (link.side != MNM::Tile::SLink::OffMesh)
			{
				pLinksBuffer[newLinkCount] = link;
				newLinkCount++;
			}
		}
	}
	else
	{
		//Just copy the triangles as they are
		memcpy(pTrianglesBuffer, pTriangles, sizeof(MNM::Tile::STriangle) * trianglesCount);
	}

#if DO_FILTERING_CONSISTENCY_CHECK
	if (newLinkCount > 0)
	{
		for (uint16 i = 0; i < trianglesCount; ++i)
		{
			if ((pTrianglesBuffer[i].firstLink + pTrianglesBuffer[i].linkCount) > newLinkCount)
			{
				__debugbreak();
			}
		}
	}
#endif

	assert(newLinkCount == (tile.GetLinksCount() - offMeshLinksCount));

	return newLinkCount;
}

void NavigationSystem::GatherNavigationVolumesToSave(std::vector<NavigationVolumeID>& usedVolumes) const
{
	// #MNM_TODO pavloi 2016.10.21: it may be faster to iterate through m_volumes and gather all volumes from there.
	// But there is no guarantee yet, that the registered volumes are actually used.

	usedVolumes.reserve(m_volumes.size() * m_agentTypes.size());

	for (const AgentType& agentType : m_agentTypes)
	{
		for (const AgentType::MeshInfo& meshInfo : agentType.meshes)
		{
			CRY_ASSERT(m_meshes.validate(meshInfo.id));
			const NavigationMesh& mesh = m_meshes.get(meshInfo.id);

			CRY_ASSERT(mesh.boundary);
			CRY_ASSERT(m_volumes.validate(mesh.boundary));
			if (mesh.boundary && m_volumes.validate(mesh.boundary))
			{
				usedVolumes.push_back(mesh.boundary);
			}
		}

		for (const NavigationVolumeID& volumeId : agentType.exclusions)
		{
			CRY_ASSERT(volumeId);
			CRY_ASSERT(m_volumes.validate(volumeId));
			if (volumeId && m_volumes.validate(volumeId))
			{
				usedVolumes.push_back(volumeId);
			}
		}
	}
	std::sort(usedVolumes.begin(), usedVolumes.end());
	auto lastUniqueIter = std::unique(usedVolumes.begin(), usedVolumes.end());
	usedVolumes.erase(lastUniqueIter, usedVolumes.end());

	if (usedVolumes.size() != m_volumes.size())
	{
		AIWarning("NavigationSystem::GatherNavigationVolumesToSave: there are registered navigation volumes, which are not used by any navigation mesh. They will not be saved.");
	}
}

#if defined(SEG_WORLD)
bool NavigationSystem::SaveToFile(const char* fileName, const AABB& segmentAABB) const PREFAST_SUPPRESS_WARNING(6262)
#else
bool NavigationSystem::SaveToFile(const char* fileName) const PREFAST_SUPPRESS_WARNING(6262)
#endif
{
#if NAVIGATION_SYSTEM_PC_ONLY

	m_pEditorBackgroundUpdate->Pause(true);

	CCryFile file;
	if (false != file.Open(fileName, "wb"))
	{
		const int maxTriangles = 1024;
		const int maxLinks = maxTriangles * 6;
		MNM::Tile::STriangle triangleBuffer[maxTriangles];
		MNM::Tile::SLink linkBuffer[maxLinks];

		// Saving file data version
		uint16 nFileVersion = BAI_NAVIGATION_FILE_VERSION;
		file.Write(&nFileVersion, sizeof(nFileVersion));
		file.Write(&m_configurationVersion, sizeof(m_configurationVersion));
	#ifdef SW_NAVMESH_USE_GUID
		uint32 useGUID = BAI_NAVIGATION_GUID_FLAG;
		file.Write(&useGUID, sizeof(useGUID));
	#endif

		// Saving boundary volumes, their ID's and names
		{
	#if SEG_WORLD
			static_assert(false, "Segmented world is deprecated and not supported anymore by the current implementation of NavigationSystem");
	#endif

			std::vector<NavigationVolumeID> usedVolumes;
			GatherNavigationVolumesToSave(*&usedVolumes);

			const uint32 usedVolumesCount = static_cast<uint32>(usedVolumes.size());
			file.WriteType(&usedVolumesCount);

			string volumeAreaName;
			for (uint32 idx = 0; idx < usedVolumesCount; ++idx)
			{
				const NavigationVolumeID volumeId = usedVolumes[idx];
				CRY_ASSERT(m_volumes.validate(volumeId));
				const MNM::BoundingVolume& volume = m_volumes.get(volumeId);

				const uint32 verticesCount = volume.vertices.size();

				volumeAreaName.clear();
				m_volumesManager.GetAreaName(volumeId, *&volumeAreaName);
				const uint32 volumeAreaNameSize = static_cast<uint32>(volumeAreaName.size());

				WriteNavigationIdType(file, volumeId);
				file.WriteType(&volume.height);
				file.WriteType(&verticesCount);
				for (const Vec3& vertex : volume.vertices)
				{
					file.WriteType(&vertex.x, 3);
				}
				file.WriteType(&volumeAreaNameSize);
				file.WriteType(volumeAreaName.c_str(), volumeAreaNameSize);
			}
		}

		// Saving number of agents
		uint32 agentsCount = static_cast<uint32>(GetAgentTypeCount());
		file.Write(&agentsCount, sizeof(agentsCount));
		std::vector<string> agentsNamesList;

		AgentTypes::const_iterator typeIt = m_agentTypes.begin();
		AgentTypes::const_iterator typeEnd = m_agentTypes.end();

		for (; typeIt != typeEnd; ++typeIt)
		{
			const AgentType& agentType = *typeIt;
			uint32 nameLength = agentType.name.length();
			nameLength = std::min(nameLength, (uint32)MAX_NAME_LENGTH - 1);
			// Saving name length and the name itself
			file.Write(&nameLength, sizeof(nameLength));
			file.Write(agentType.name.c_str(), sizeof(char) * nameLength);

			// Saving the amount of memory this agent is using inside the file to be able to skip it during loading
			uint32 totalAgentMemory = 0;
			size_t totalAgentMemoryPositionInFile = file.GetPosition();
			file.Write(&totalAgentMemory, sizeof(totalAgentMemory));

			AgentType::Meshes::const_iterator mit = agentType.meshes.begin();
			AgentType::Meshes::const_iterator mend = agentType.meshes.end();
	#if defined(SEG_WORLD)
			size_t writtenMeshCountDataPosition = file.GetPosition();
			uint32 actualWrittenMeshCount = 0;
	#endif
			uint32 meshesCount = agentType.meshes.size();
			file.Write(&meshesCount, sizeof(meshesCount));

			for (; mit != mend; ++mit)
			{
				const uint32 meshIDuint32 = mit->id;
				const NavigationMesh& mesh = m_meshes[NavigationMeshID(meshIDuint32)];
				const MNM::BoundingVolume& volume = m_volumes[mesh.boundary];
				const MNM::CNavMesh& navMesh = mesh.navMesh;

	#ifdef SEG_WORLD
				if (!segmentAABB.IsIntersectBox(volume.aabb))
					continue;

				++actualWrittenMeshCount;
	#endif

				// Saving mesh id
	#ifdef SW_NAVMESH_USE_GUID
				const NavigationMeshGUID meshGUID = mit->guid;
				file.Write(&meshGUID, sizeof(meshGUID));
	#else
				file.Write(&meshIDuint32, sizeof(meshIDuint32));
	#endif

				// Saving mesh name
				uint32 meshNameLength = mesh.name.length();
				meshNameLength = std::min(meshNameLength, (uint32)MAX_NAME_LENGTH - 1);
				file.Write(&meshNameLength, sizeof(meshNameLength));
				file.Write(mesh.name.c_str(), sizeof(char) * meshNameLength);

				// Saving total islands
				uint32 totalIslands = mesh.navMesh.GetTotalIslands();
				file.Write(&totalIslands, sizeof(totalIslands));

				uint32 totalMeshMemory = 0;
				size_t totalMeshMemoryPositionInFile = file.GetPosition();
				file.Write(&totalMeshMemory, sizeof(totalMeshMemory));

				// Saving mesh boundary id
				/*
				   Let's check if this boundary id matches the id of the
				   volume stored in the volumes manager.
				   It's an additional check for the consistency of the
				   saved binary data.
				 */

				if (!m_volumes.validate(mesh.boundary) || m_volumesManager.GetAreaID(mesh.name.c_str()) != mesh.boundary)
				{
					CryMessageBox("Sandbox detected a possible data corruption during the save of the navigation mesh."
					              "Trigger a full rebuild and re-export to engine to fix"
					              " the binary data associated with the MNM.", "Navigation Save Error");
				}
	#ifdef SW_NAVMESH_USE_GUID
				file.Write(&mesh.boundaryGUID, sizeof(mesh.boundaryGUID));
	#else
				WriteNavigationIdType(file, mesh.boundary);
	#endif

				// Saving mesh exclusion shapes
	#ifdef SW_NAVMESH_USE_GUID
				uint32 exclusionShapesCount = mesh.exclusionsGUID.size();
				file.Write(&exclusionShapesCount, sizeof(exclusionShapesCount));
				for (uint32 exclusionCounter = 0; exclusionCounter < exclusionShapesCount; ++exclusionCounter)
				{
					NavigationVolumeGUID exclusionGuid = mesh.exclusionsGUID[exclusionCounter];
					file.Write(&(exclusionGuid), sizeof(exclusionGuid));
				}
	#else
				{
					// Figure out which of the exclusion volume IDs are valid in order to export only those.
					// This check will also fix maps that get exported after 2016-11-23. All maps prior to that date might contain invalid exclusion volume IDs and need to get exported again.
					NavigationMesh::ExclusionVolumes validExlusionVolumes;
					for (NavigationVolumeID volumeID : mesh.exclusions)
					{
						if (m_volumes.validate(volumeID))
						{
							validExlusionVolumes.push_back(volumeID);
						}
					}
					
					// Now export only the valid exclusion volume IDs.
					uint32 exclusionShapesCount = validExlusionVolumes.size();
					file.Write(&exclusionShapesCount, sizeof(exclusionShapesCount));
					for (uint32 exclusionCounter = 0; exclusionCounter < exclusionShapesCount; ++exclusionCounter)
					{
						WriteNavigationIdType(file, validExlusionVolumes[exclusionCounter]);
					}
				}
	#endif

				// Saving tiles count
				uint32 tileCount = navMesh.GetTileCount();
				file.Write(&tileCount, sizeof(tileCount));

				// Saving grid params (Not all of this params are actually important to recreate the mesh but
				// we save all for possible further utilization)
				const MNM::CNavMesh::SGridParams paramsGrid = navMesh.GetGridParams();
				file.Write(&(paramsGrid.origin.x), sizeof(paramsGrid.origin.x));
				file.Write(&(paramsGrid.origin.y), sizeof(paramsGrid.origin.y));
				file.Write(&(paramsGrid.origin.z), sizeof(paramsGrid.origin.z));
				file.Write(&(paramsGrid.tileSize.x), sizeof(paramsGrid.tileSize.x));
				file.Write(&(paramsGrid.tileSize.y), sizeof(paramsGrid.tileSize.y));
				file.Write(&(paramsGrid.tileSize.z), sizeof(paramsGrid.tileSize.z));
				file.Write(&(paramsGrid.voxelSize.x), sizeof(paramsGrid.voxelSize.x));
				file.Write(&(paramsGrid.voxelSize.y), sizeof(paramsGrid.voxelSize.y));
				file.Write(&(paramsGrid.voxelSize.z), sizeof(paramsGrid.voxelSize.z));
				file.Write(&(paramsGrid.tileCount), sizeof(paramsGrid.tileCount));

				const AABB& boundary = m_volumes[mesh.boundary].aabb;

				Vec3 bmin(std::max(0.0f, boundary.min.x - paramsGrid.origin.x),
				          std::max(0.0f, boundary.min.y - paramsGrid.origin.y),
				          std::max(0.0f, boundary.min.z - paramsGrid.origin.z));

				Vec3 bmax(std::max(0.0f, boundary.max.x - paramsGrid.origin.x),
				          std::max(0.0f, boundary.max.y - paramsGrid.origin.y),
				          std::max(0.0f, boundary.max.z - paramsGrid.origin.z));

				uint16 xmin = (uint16)(floor_tpl(bmin.x / (float)paramsGrid.tileSize.x));
				uint16 xmax = (uint16)(floor_tpl(bmax.x / (float)paramsGrid.tileSize.x));

				uint16 ymin = (uint16)(floor_tpl(bmin.y / (float)paramsGrid.tileSize.y));
				uint16 ymax = (uint16)(floor_tpl(bmax.y / (float)paramsGrid.tileSize.y));

				uint16 zmin = (uint16)(floor_tpl(bmin.z / (float)paramsGrid.tileSize.z));
				uint16 zmax = (uint16)(floor_tpl(bmax.z / (float)paramsGrid.tileSize.z));

				for (uint16 x = xmin; x < xmax + 1; ++x)
				{
					for (uint16 y = ymin; y < ymax + 1; ++y)
					{
						for (uint16 z = zmin; z < zmax + 1; ++z)
						{
							MNM::TileID i = navMesh.GetTileID(x, y, z);
							// Skipping tile id that are not used (This should never happen now)
							if (i == 0)
							{
								continue;
							}

							// Saving tile indexes
							file.Write(&x, sizeof(x));
							file.Write(&y, sizeof(y));
							file.Write(&z, sizeof(z));
							const MNM::STile& tile = navMesh.GetTile(i);
							const uint32 tileHashValue = tile.GetHashValue();
							file.Write(&tileHashValue, sizeof(tileHashValue));

							// NOTE pavloi 2016.07.22: triangles and links are not saved as is - instead they are filtered and copied into triangleBuffer and linkBuffer
							const uint16 saveLinkCount = FilterOffMeshLinksForTile(tile, triangleBuffer, maxTriangles, linkBuffer, maxLinks);

							// Saving triangles
							const uint16 trianglesCount = tile.GetTrianglesCount();
							file.Write(&trianglesCount, sizeof(trianglesCount));
							file.Write(triangleBuffer, sizeof(MNM::Tile::STriangle) * trianglesCount);

							// Saving vertices
							const MNM::Tile::Vertex* pVertices = tile.GetVertices();
							const uint16 verticesCount = tile.GetVerticesCount();
							file.Write(&verticesCount, sizeof(verticesCount));
							file.Write(pVertices, sizeof(MNM::Tile::Vertex) * verticesCount);

							// Saving links
							file.Write(&saveLinkCount, sizeof(saveLinkCount));
							file.Write(linkBuffer, sizeof(MNM::Tile::SLink) * saveLinkCount);

							// Saving nodes
							const MNM::Tile::SBVNode* pNodes = tile.GetBVNodes();
							const uint16 nodesCount = tile.GetBVNodesCount();
							file.Write(&nodesCount, sizeof(nodesCount));
							file.Write(pNodes, sizeof(MNM::Tile::SBVNode) * nodesCount);

							// Compile-time asserts to catch data type changes - don't forget to bump BAI file version number
							static_assert(sizeof(uint16) == sizeof(tile.GetLinksCount()), "Invalid type size!");
							static_assert(sizeof(uint16) == sizeof(tile.GetTrianglesCount()), "Invalid type size!");
							static_assert(sizeof(uint16) == sizeof(tile.GetVerticesCount()), "Invalid type size!");
							static_assert(sizeof(uint16) == sizeof(tile.GetBVNodesCount()), "Invalid type size!");
							static_assert(sizeof(uint16) == sizeof(trianglesCount), "Invalid type size!");
							static_assert(sizeof(uint16) == sizeof(verticesCount), "Invalid type size!");
							static_assert(sizeof(uint16) == sizeof(saveLinkCount), "Invalid type size!");
							static_assert(sizeof(uint16) == sizeof(nodesCount), "Invalid type size!");
							static_assert(sizeof(MNM::Tile::Vertex) == 6, "Invalid type size!");
							static_assert(sizeof(MNM::Tile::STriangle) == 16, "Invalid type size!");
							static_assert(sizeof(MNM::Tile::SLink) == 2, "Invalid type size!");
							static_assert(sizeof(MNM::Tile::SBVNode) == 14, "Invalid type size!");
							static_assert(sizeof(uint32) == sizeof(tile.GetHashValue()), "Invalid type size!");
						}
					}

				}
				size_t endingMeshDataPosition = file.GetPosition();
				totalMeshMemory = endingMeshDataPosition - totalMeshMemoryPositionInFile - sizeof(totalMeshMemory);
				file.Seek(totalMeshMemoryPositionInFile, SEEK_SET);
				file.Write(&totalMeshMemory, sizeof(totalMeshMemory));
				file.Seek(endingMeshDataPosition, SEEK_SET);
			}

			size_t endingAgentDataPosition = file.GetPosition();
			totalAgentMemory = endingAgentDataPosition - totalAgentMemoryPositionInFile - sizeof(totalAgentMemory);
			file.Seek(totalAgentMemoryPositionInFile, SEEK_SET);
			file.Write(&totalAgentMemory, sizeof(totalAgentMemory));

	#if defined(SEG_WORLD)
			file.Seek(writtenMeshCountDataPosition, SEEK_SET);
			file.Write(&actualWrittenMeshCount, sizeof(actualWrittenMeshCount));

			file.Seek(areasCountDataPosition, SEEK_SET);
			file.Write(&actualWrittenAreasCount, sizeof(actualWrittenAreasCount));
	#endif

			file.Seek(endingAgentDataPosition, SEEK_SET);
		}
		file.Close();
	}

	m_pEditorBackgroundUpdate->Pause(false);

#endif
	return true;
}

void NavigationSystem::UpdateAllListener(const ENavigationEvent event)
{
	for (NavigationListeners::Notifier notifier(m_listenersList); notifier.IsValid(); notifier.Next())
	{
		notifier->OnNavigationEvent(event);
	}
}

void NavigationSystem::DebugDraw()
{
	m_debugDraw.DebugDraw(*this);
}

void NavigationSystem::Reset()
{
	ResetAllNavigationSystemUsers();
}

void NavigationSystem::GetMemoryStatistics(ICrySizer* pSizer)
{
#if DEBUG_MNM_ENABLED
	size_t totalMemory = 0;
	for (uint16 i = 0; i < m_meshes.capacity(); ++i)
	{
		if (!m_meshes.index_free(i))
		{
			const NavigationMeshID meshID(m_meshes.get_index_id(i));

			const NavigationMesh& mesh = m_meshes[meshID];
			const MNM::OffMeshNavigation& offMeshNavigation = m_offMeshNavigationManager.GetOffMeshNavigationForMesh(meshID);
			const AgentType& agentType = m_agentTypes[mesh.agentTypeID - 1];

			const NavigationMesh::ProfileMemoryStats meshMemStats = mesh.GetMemoryStats(pSizer);
			const MNM::OffMeshNavigation::ProfileMemoryStats offMeshMemStats = offMeshNavigation.GetMemoryStats(pSizer);
			const OffMeshNavigationManager::ProfileMemoryStats linksMemStats = m_offMeshNavigationManager.GetMemoryStats(pSizer, meshID);

			totalMemory = meshMemStats.totalNavigationMeshMemory + offMeshMemStats.totalSize + linksMemStats.totalSize;
		}
	}
#endif
}

void NavigationSystem::SetDebugDisplayAgentType(NavigationAgentTypeID agentTypeID)
{
	m_debugDraw.SetAgentType(agentTypeID);
}

NavigationAgentTypeID NavigationSystem::GetDebugDisplayAgentType() const
{
	return m_debugDraw.GetAgentType();
}

void NavigationSystem::OffsetBoundingVolume(const Vec3& additionalOffset, const NavigationVolumeID volumeId)
{
	if (volumeId && m_volumes.validate(volumeId))
	{
		m_volumes[volumeId].OffsetVerticesAndAABB(additionalOffset);
	}
}

void NavigationSystem::OffsetAllMeshes(const Vec3& additionalOffset)
{
	AgentTypes::iterator it = m_agentTypes.begin();
	AgentTypes::iterator end = m_agentTypes.end();

	std::set<NavigationVolumeID> volumeSet;
	for (; it != end; ++it)
	{
		AgentType& agentType = *it;

		AgentType::Meshes::const_iterator mit = agentType.meshes.begin();
		AgentType::Meshes::const_iterator mend = agentType.meshes.end();

		for (; mit != mend; ++mit)
		{
			const NavigationMeshID meshId = mit->id;
			if (meshId && m_meshes.validate(meshId))
			{
				NavigationMesh& mesh = m_meshes[meshId];
				mesh.navMesh.OffsetOrigin(additionalOffset);
				NavigationVolumeID volumeId = mesh.boundary;
				volumeSet.insert(volumeId);
			}
		}
	}

	std::set<NavigationVolumeID>::iterator volumeIt = volumeSet.begin();
	std::set<NavigationVolumeID>::iterator volumeEnd = volumeSet.end();
	for (; volumeIt != volumeEnd; ++volumeIt)
	{
		OffsetBoundingVolume(additionalOffset, *volumeIt);
	}
}

void NavigationSystem::OnSystemEvent(ESystemEvent event, UINT_PTR wparam, UINT_PTR lparam)
{
	switch (event)
	{
	case ESYSTEM_EVENT_SW_SHIFT_WORLD:
		OffsetAllMeshes(*(const Vec3*)wparam);
		break;
	}
}

TileGeneratorExtensionID NavigationSystem::RegisterTileGeneratorExtension(MNM::TileGenerator::IExtension& extension)
{
	const TileGeneratorExtensionID newId = TileGeneratorExtensionID(m_tileGeneratorExtensionsContainer.idCounter + 1);

	if (newId != TileGeneratorExtensionID())
	{
		m_tileGeneratorExtensionsContainer.idCounter = newId;
	}
	else
	{
		CRY_ASSERT_MESSAGE(newId != TileGeneratorExtensionID(), "TileGeneratorExtensionID counter is exausted");
		return TileGeneratorExtensionID();
	}

	{
		AUTO_MODIFYLOCK(m_tileGeneratorExtensionsContainer.extensionsLock);

		m_tileGeneratorExtensionsContainer.extensions[newId] = &extension;
		return newId;
	}
}

bool NavigationSystem::UnRegisterTileGeneratorExtension(const TileGeneratorExtensionID extensionId)
{
	AUTO_MODIFYLOCK(m_tileGeneratorExtensionsContainer.extensionsLock);
	auto iter = m_tileGeneratorExtensionsContainer.extensions.find(extensionId);
	if (iter != m_tileGeneratorExtensionsContainer.extensions.end())
	{
		m_tileGeneratorExtensionsContainer.extensions.erase(extensionId);
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

#if DEBUG_MNM_ENABLED

// Only needed for the NavigationSystemDebugDraw
	#include "Navigation/PathHolder.h"
	#include "Navigation/MNM/OffGridLinks.h"

void NavigationSystemDebugDraw::DebugDraw(NavigationSystem& navigationSystem)
{
	CRY_PROFILE_FUNCTION(PROFILE_AI);
	
	const bool validDebugAgent = m_agentTypeID && (m_agentTypeID <= navigationSystem.GetAgentTypeCount());

	if (validDebugAgent)
	{
		static int lastFrameID = 0;

		const int frameID = gEnv->pRenderer->GetFrameID();
		if (frameID == lastFrameID)
			return;

		lastFrameID = frameID;

		MNM::TileID excludeTileID(0);
		DebugDrawSettings settings = GetDebugDrawSettings(navigationSystem);

		if (settings.Valid())
		{
			excludeTileID = DebugDrawTileGeneration(navigationSystem, settings);
		}

		DebugDrawRayCast(navigationSystem, settings);

		DebugDrawPathFinder(navigationSystem, settings);

		DebugDrawClosestPoint(navigationSystem, settings);

		DebugDrawGroundPoint(navigationSystem, settings);

		DebugDrawIslandConnection(navigationSystem, settings);

		DebugDrawNavigationMeshesForSelectedAgent(navigationSystem, excludeTileID);

		m_progress.Draw();

		if (navigationSystem.IsInUse())
		{
			navigationSystem.m_offMeshNavigationManager.UpdateEditorDebugHelpers();
		}
	}

	DebugDrawNavigationSystemState(navigationSystem);

	DebugDrawMemoryStats(navigationSystem);
}

void NavigationSystemDebugDraw::UpdateWorkingProgress(const float frameTime, const size_t queueSize)
{
	m_progress.Update(frameTime, queueSize);
}

MNM::TileID NavigationSystemDebugDraw::DebugDrawTileGeneration(NavigationSystem& navigationSystem, const DebugDrawSettings& settings)
{
	MNM::TileID debugTileID(0);
	#if DEBUG_MNM_ENABLED

	// TODO pavloi 2016.03.09: instead of calling GetAsyncKeyState(), register for events with GetISystem()->GetIInput()->AddEventListener().

	static MNM::CTileGenerator debugGenerator;
	static MNM::TileID tileID(0);
	static bool prevKeyState = false;
	static size_t drawMode = (size_t)MNM::CTileGenerator::EDrawMode::DrawNone;
	static bool bDrawAdditionalInfo = false;

	NavigationMesh& mesh = navigationSystem.m_meshes[settings.meshID];
	const MNM::CNavMesh::SGridParams& paramsGrid = mesh.navMesh.GetGridParams();
	const AgentType& agentType = navigationSystem.m_agentTypes[m_agentTypeID - 1];

	bool forceGeneration = settings.forceGeneration;
	size_t selectedX = settings.selectedX;
	size_t selectedY = settings.selectedY;
	size_t selectedZ = settings.selectedZ;

	CDebugDrawContext dc;

	bool keyState = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;

	if (keyState && keyState != prevKeyState)
	{
		if (((GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0))
		{
			forceGeneration = true;
		}
		else if (((GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0))
			--drawMode;
		else
			++drawMode;

		if (drawMode == (size_t)MNM::CTileGenerator::EDrawMode::LastDrawMode)
			drawMode = (size_t)MNM::CTileGenerator::EDrawMode::DrawNone;
		else if (drawMode > (size_t)MNM::CTileGenerator::EDrawMode::LastDrawMode)
			drawMode = (size_t)MNM::CTileGenerator::EDrawMode::LastDrawMode - 1;
	}

	prevKeyState = keyState;

	{
		static size_t selectPrevKeyState = 0;
		size_t selectKeyState = 0;

		selectKeyState |= ((GetAsyncKeyState(VK_UP) & 0x8000) != 0);
		selectKeyState |= 2 * ((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0);
		selectKeyState |= 4 * ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0);
		selectKeyState |= 8 * ((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0);
		selectKeyState |= 16 * ((GetAsyncKeyState(VK_ADD) & 0x8000) != 0);
		selectKeyState |= 32 * ((GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0);

		int dirX = 0;
		int dirY = 0;
		int dirZ = 0;

		if ((selectKeyState & 1 << 0) && ((selectPrevKeyState & 1 << 0) == 0))
			dirY += 1;
		if ((selectKeyState & 1 << 1) && ((selectPrevKeyState & 1 << 1) == 0))
			dirX += 1;
		if ((selectKeyState & 1 << 2) && ((selectPrevKeyState & 1 << 2) == 0))
			dirY -= 1;
		if ((selectKeyState & 1 << 3) && ((selectPrevKeyState & 1 << 3) == 0))
			dirX -= 1;
		if ((selectKeyState & 1 << 4) && ((selectPrevKeyState & 1 << 4) == 0))
			dirZ += 1;
		if ((selectKeyState & 1 << 5) && ((selectPrevKeyState & 1 << 5) == 0))
			dirZ -= 1;

		if (!mesh.boundary)
		{
			selectedX += dirX;
			selectedY += dirY;
			selectedZ += dirZ;
		}
		else
		{
			Vec3 tileOrigin = paramsGrid.origin +
			                  Vec3((float)(selectedX + dirX) * paramsGrid.tileSize.x,
			                       (float)(selectedY + dirY) * paramsGrid.tileSize.y,
			                       (float)(selectedZ + dirZ) * paramsGrid.tileSize.z);

			if (navigationSystem.m_volumes[mesh.boundary].Contains(tileOrigin))
			{
				selectedX += dirX;
				selectedY += dirY;
				selectedZ += dirZ;
			}
		}

		selectPrevKeyState = selectKeyState;
	}

	{
		static bool bPrevAdditionalInfoKeyState = false;
		bool bAdditionalInfoKeyState = (GetAsyncKeyState(VK_DIVIDE) & 0x8000) != 0;
		if (bAdditionalInfoKeyState && bAdditionalInfoKeyState != bPrevAdditionalInfoKeyState)
		{
			bDrawAdditionalInfo = !bDrawAdditionalInfo;
		}
		bPrevAdditionalInfoKeyState = bAdditionalInfoKeyState;
	}

	if (forceGeneration)
	{
		tileID = 0;
		debugGenerator = MNM::CTileGenerator();

		MNM::STile tile;
		MNM::CTileGenerator::Params params;

		std::vector<MNM::BoundingVolume> exclusions;
		exclusions.resize(mesh.exclusions.size());

		NavigationMesh::ExclusionVolumes::const_iterator eit = mesh.exclusions.begin();
		NavigationMesh::ExclusionVolumes::const_iterator eend = mesh.exclusions.end();

		for (size_t eindex = 0; eit != eend; ++eit, ++eindex)
			exclusions[eindex] = navigationSystem.m_volumes[*eit];

		navigationSystem.SetupGenerator(settings.meshID, paramsGrid, static_cast<uint16>(selectedX), static_cast<uint16>(selectedY), static_cast<uint16>(selectedZ), params,
		                                mesh.boundary ? &navigationSystem.m_volumes[mesh.boundary] : 0,
		                                exclusions.empty() ? 0 : &exclusions[0], exclusions.size());

		params.flags |= MNM::CTileGenerator::Params::DebugInfo | MNM::CTileGenerator::Params::NoHashTest;

		if (debugGenerator.Generate(params, tile, 0))
		{
			tileID = mesh.navMesh.SetTile(selectedX, selectedY, selectedZ, tile);

			mesh.navMesh.ConnectToNetwork(tileID);
		}
		else if (tileID = mesh.navMesh.GetTileID(selectedX, selectedY, selectedZ))
			mesh.navMesh.ClearTile(tileID);
	}

	debugGenerator.Draw((MNM::CTileGenerator::EDrawMode)drawMode, bDrawAdditionalInfo);

	const char* drawModeName = "None";

	switch ((MNM::CTileGenerator::EDrawMode)drawMode)
	{
	case MNM::CTileGenerator::EDrawMode::DrawNone:
		break;

	case MNM::CTileGenerator::EDrawMode::DrawRawInputGeometry:
		drawModeName = "Raw Input Geometry";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawRawVoxels:
		drawModeName = "Raw Voxels";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawFilteredVoxels:
		drawModeName = "Filtered Voxels - filtered after walkable test";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawFlaggedVoxels:
		drawModeName = "Flagged Voxels";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawDistanceTransform:
		drawModeName = "Distance Transform";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawPainting:
		drawModeName = "Painting";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawSegmentation:
		drawModeName = "Segmentation";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawNumberedContourVertices:
	case MNM::CTileGenerator::EDrawMode::DrawContourVertices:
		drawModeName = "Contour Vertices";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawTracers:
		drawModeName = "Tracers";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawSimplifiedContours:
		drawModeName = "Simplified Contours";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawTriangulation:
		drawModeName = "Triangulation";
		break;

	case MNM::CTileGenerator::EDrawMode::DrawBVTree:
		drawModeName = "BV Tree";
		break;

	case MNM::CTileGenerator::EDrawMode::LastDrawMode:
	default:
		drawModeName = "Unknown";
		break;
	}

	dc->Draw2dLabel(10.0f, 5.0f, 1.6f, Col_White, false, "TileID %d - Drawing %s", tileID, drawModeName);

	const MNM::CTileGenerator::ProfilerType& profilerInfo = debugGenerator.GetProfiler();

	dc->Draw2dLabel(10.0f, 28.0f, 1.25f, Col_White, false,
	                "Total: %.1f - Voxelizer(%.2fK tris): %.1f - Filter: %.1f\n"
	                "Contour(%d regs): %.1f - Simplify: %.1f\n"
	                "Triangulate(%d vtx/%d tris): %.1f - BVTree(%d nodes): %.1f",
	                profilerInfo.GetTotalElapsed().GetMilliSeconds(),
	                profilerInfo[MNM::CTileGenerator::VoxelizationTriCount] / 1000.0f,
	                profilerInfo[MNM::CTileGenerator::Voxelization].elapsed.GetMilliSeconds(),
	                profilerInfo[MNM::CTileGenerator::Filter].elapsed.GetMilliSeconds(),
	                profilerInfo[MNM::CTileGenerator::RegionCount],
	                profilerInfo[MNM::CTileGenerator::ContourExtraction].elapsed.GetMilliSeconds(),
	                profilerInfo[MNM::CTileGenerator::Simplification].elapsed.GetMilliSeconds(),
	                profilerInfo[MNM::CTileGenerator::VertexCount],
	                profilerInfo[MNM::CTileGenerator::TriangleCount],
	                profilerInfo[MNM::CTileGenerator::Triangulation].elapsed.GetMilliSeconds(),
	                profilerInfo[MNM::CTileGenerator::BVTreeNodeCount],
	                profilerInfo[MNM::CTileGenerator::BVTreeConstruction].elapsed.GetMilliSeconds()
	                );

	dc->Draw2dLabel(10.0f, 84.0f, 1.4f, Col_White, false,
	                "Peak Memory: %.2fKB", profilerInfo.GetMemoryPeak() / 1024.0f);

	size_t vertexMemory = profilerInfo[MNM::CTileGenerator::VertexMemory].used;
	size_t triangleMemory = profilerInfo[MNM::CTileGenerator::TriangleMemory].used;
	size_t bvTreeMemory = profilerInfo[MNM::CTileGenerator::BVTreeMemory].used;
	size_t tileMemory = vertexMemory + triangleMemory + bvTreeMemory;

	dc->Draw2dLabel(10.0f, 98.0f, 1.4f, Col_White, false,
	                "Tile Memory: %.2fKB (Vtx: %db, Tri: %db, BVT: %db)",
	                tileMemory / 1024.0f,
	                vertexMemory,
	                triangleMemory,
	                bvTreeMemory);

	if (drawMode != (size_t)MNM::CTileGenerator::EDrawMode::DrawNone)
	{
		debugTileID = mesh.navMesh.GetTileID(selectedX, selectedY, selectedZ);
	}
	#endif // DEBUG_MNM_ENABLED

	return debugTileID;
}

void NavigationSystemDebugDraw::DebugDrawRayCast(NavigationSystem& navigationSystem, const DebugDrawSettings& settings)
{
	if (CAIObject* debugObjectStart = gAIEnv.pAIObjectManager->GetAIObjectByName("MNMRayStart"))
	{
		if (CAIObject* debugObjectEnd = gAIEnv.pAIObjectManager->GetAIObjectByName("MNMRayEnd"))
		{
			NavigationMeshID meshID = navigationSystem.GetEnclosingMeshID(m_agentTypeID, debugObjectStart->GetPos());
			IF_UNLIKELY (!meshID)
				return;

			NavigationMesh& mesh = navigationSystem.m_meshes[meshID];
			const MNM::CNavMesh::SGridParams& paramsGrid = mesh.navMesh.GetGridParams();
			const MNM::CNavMesh& navMesh = mesh.navMesh;

			const MNM::vector3_t origin = MNM::vector3_t(MNM::real_t(paramsGrid.origin.x),
			                                             MNM::real_t(paramsGrid.origin.y),
			                                             MNM::real_t(paramsGrid.origin.z));
			const MNM::vector3_t originOffset = origin + MNM::vector3_t(0, 0, MNM::real_t::fraction(725, 10000));

			IRenderAuxGeom* renderAuxGeom = gEnv->pRenderer->GetIRenderAuxGeom();

			const Vec3 startLoc = debugObjectStart->GetPos();
			const Vec3 endLoc = debugObjectEnd->GetPos();

			const MNM::real_t range = MNM::real_t(1.0f);

			MNM::vector3_t start = MNM::vector3_t(
				MNM::real_t(startLoc.x), MNM::real_t(startLoc.y), MNM::real_t(startLoc.z)) - origin;
			MNM::vector3_t end = MNM::vector3_t(
				MNM::real_t(endLoc.x), MNM::real_t(endLoc.y), MNM::real_t(endLoc.z)) - origin;

			MNM::TriangleID triStart = navMesh.GetTriangleAt(start, range, range);

			if (triStart)
			{
				SAuxGeomRenderFlags oldFlags = renderAuxGeom->GetRenderFlags();

				SAuxGeomRenderFlags renderFlags(e_Def3DPublicRenderflags);
				renderFlags.SetAlphaBlendMode(e_AlphaBlended);
				renderAuxGeom->SetRenderFlags(renderFlags);
				
				MNM::TriangleID triEnd = navMesh.GetTriangleAt(end, range, range);

				MNM::CNavMesh::RayCastRequest<512> raycastRequest;

				MNM::CNavMesh::ERayCastResult result = navMesh.RayCast(start, triStart, end, triEnd, raycastRequest);

				for (size_t i = 0; i < raycastRequest.wayTriCount; ++i)
				{
					MNM::vector3_t a, b, c;

					navMesh.GetVertices(raycastRequest.way[i], a, b, c);

					renderAuxGeom->DrawTriangle(
					  (a + originOffset).GetVec3(), ColorB(Col_Maroon, 0.5f),
					  (b + originOffset).GetVec3(), ColorB(Col_Maroon, 0.5f),
					  (c + originOffset).GetVec3(), ColorB(Col_Maroon, 0.5f));
				}

				if (triStart)
				{
					MNM::vector3_t a, b, c;
					navMesh.GetVertices(triStart, a, b, c);

					renderAuxGeom->DrawTriangle(
					  (a + originOffset).GetVec3(), ColorB(Col_GreenYellow, 0.5f),
					  (b + originOffset).GetVec3(), ColorB(Col_GreenYellow, 0.5f),
					  (c + originOffset).GetVec3(), ColorB(Col_GreenYellow, 0.5f));
				}

				if (triEnd)
				{
					MNM::vector3_t a, b, c;
					navMesh.GetVertices(triEnd, a, b, c);

					renderAuxGeom->DrawTriangle(
					  (a + originOffset).GetVec3(), ColorB(Col_Red, 0.5f),
					  (b + originOffset).GetVec3(), ColorB(Col_Red, 0.5f),
					  (c + originOffset).GetVec3(), ColorB(Col_Red, 0.5f));
				}

				const Vec3 offset(0.0f, 0.0f, 0.085f);

				if (result == MNM::CNavMesh::eRayCastResult_NoHit)
				{
					renderAuxGeom->DrawLine(startLoc + offset, Col_YellowGreen, endLoc + offset, Col_YellowGreen, 8.0f);
				}
				else
				{
					const MNM::CNavMesh::RayHit& hit = raycastRequest.hit;
					Vec3 hitLoc = (result == MNM::CNavMesh::eRayCastResult_Hit) ? startLoc + ((endLoc - startLoc) * hit.distance.as_float()) : startLoc;
					renderAuxGeom->DrawLine(startLoc + offset, Col_YellowGreen, hitLoc + offset, Col_YellowGreen, 8.0f);
					renderAuxGeom->DrawLine(hitLoc + offset, Col_Red, endLoc + offset, Col_Red, 8.0f);
				}

				renderAuxGeom->SetRenderFlags(oldFlags);
			}
		}
	}
}

void NavigationSystemDebugDraw::DebugDrawClosestPoint(NavigationSystem& navigationSystem, const DebugDrawSettings& settings)
{
	if (CAIObject* debugObject = gAIEnv.pAIObjectManager->GetAIObjectByName("MNMClosestPoint"))
	{
		NavigationMeshID meshID = navigationSystem.GetEnclosingMeshID(m_agentTypeID, debugObject->GetPos());
		IF_UNLIKELY (!meshID)
			return;

		NavigationMesh& mesh = navigationSystem.m_meshes[meshID];
		const MNM::CNavMesh& navMesh = mesh.navMesh;
		const MNM::CNavMesh::SGridParams& paramsGrid = mesh.navMesh.GetGridParams();

		const MNM::vector3_t origin = MNM::vector3_t(MNM::real_t(paramsGrid.origin.x),
		                                             MNM::real_t(paramsGrid.origin.y),
		                                             MNM::real_t(paramsGrid.origin.z));
		const MNM::vector3_t originOffset = origin + MNM::vector3_t(0, 0, MNM::real_t::fraction(725, 10000));

		const Vec3 startLoc = debugObject->GetEntity() ? debugObject->GetEntity()->GetWorldPos() : debugObject->GetPos();
		const MNM::vector3_t fixedPointStartLoc(MNM::real_t(startLoc.x), MNM::real_t(startLoc.y), MNM::real_t(startLoc.z));
		const MNM::real_t range = MNM::real_t(5.0f);

		MNM::real_t distance(.0f);
		MNM::vector3_t closestPosition;
		if (MNM::TriangleID closestTriangle = navMesh.GetClosestTriangle(fixedPointStartLoc, range, range, &distance, &closestPosition))
		{
			IRenderAuxGeom* renderAuxGeom = gEnv->pRenderer->GetIRenderAuxGeom();
			const Vec3 verticalOffset = Vec3(.0f, .0f, .1f);
			const Vec3 endPos(closestPosition.GetVec3() + origin.GetVec3());
			renderAuxGeom->DrawSphere(endPos + verticalOffset, 0.05f, ColorB(Col_Red));
			renderAuxGeom->DrawSphere(fixedPointStartLoc.GetVec3() + verticalOffset, 0.05f, ColorB(Col_Black));

			CDebugDrawContext dc;
			dc->Draw2dLabel(10.0f, 10.0f, 1.3f, Col_White, false,
			                "Distance of the ending result position from the original one: %f", distance.as_float());
		}
	}
}

void NavigationSystemDebugDraw::DebugDrawGroundPoint(NavigationSystem& navigationSystem, const DebugDrawSettings& settings)
{
	if (CAIObject* debugObject = gAIEnv.pAIObjectManager->GetAIObjectByName("MNMGroundPoint"))
	{
		NavigationMeshID meshID = navigationSystem.GetEnclosingMeshID(m_agentTypeID, debugObject->GetPos());
		IF_UNLIKELY (!meshID)
			return;

		NavigationMesh& mesh = navigationSystem.m_meshes[meshID];
		const MNM::CNavMesh& navMesh = mesh.navMesh;
		const MNM::CNavMesh::SGridParams& paramsGrid = mesh.navMesh.GetGridParams();

		const MNM::vector3_t origin = MNM::vector3_t(MNM::real_t(paramsGrid.origin.x),
		                                             MNM::real_t(paramsGrid.origin.y), MNM::real_t(paramsGrid.origin.z));
		const Vec3 startLoc = debugObject->GetEntity() ? debugObject->GetEntity()->GetWorldPos() : debugObject->GetPos();

		Vec3 closestPosition;
		if (navigationSystem.GetGroundLocationInMesh(meshID, startLoc, 100.0f, 0.25f, &closestPosition))
		{
			IRenderAuxGeom* renderAuxGeom = gEnv->pRenderer->GetIRenderAuxGeom();
			const Vec3 verticalOffset = Vec3(.0f, .0f, .1f);
			const Vec3 endPos(closestPosition + origin.GetVec3());
			renderAuxGeom->DrawSphere(endPos + verticalOffset, 0.05f, ColorB(Col_Red));
			renderAuxGeom->DrawSphere(startLoc, 0.05f, ColorB(Col_Black));
		}
	}
}

void NavigationSystemDebugDraw::DebugDrawPathFinder(NavigationSystem& navigationSystem, const DebugDrawSettings& settings)
{
	if (CAIObject* debugObjectStart = gAIEnv.pAIObjectManager->GetAIObjectByName("MNMPathStart"))
	{
		if (CAIObject* debugObjectEnd = gAIEnv.pAIObjectManager->GetAIObjectByName("MNMPathEnd"))
		{
			NavigationMeshID meshID = navigationSystem.GetEnclosingMeshID(m_agentTypeID, debugObjectStart->GetPos());
			IF_UNLIKELY (!meshID)
				return;

			NavigationMesh& mesh = navigationSystem.m_meshes[meshID];
			const MNM::CNavMesh& navMesh = mesh.navMesh;
			const MNM::CNavMesh::SGridParams& paramsGrid = mesh.navMesh.GetGridParams();

			const OffMeshNavigationManager* offMeshNavigationManager = navigationSystem.GetOffMeshNavigationManager();
			assert(offMeshNavigationManager);
			const MNM::OffMeshNavigation& offMeshNavigation = offMeshNavigationManager->GetOffMeshNavigationForMesh(meshID);

			const MNM::vector3_t origin = MNM::vector3_t(MNM::real_t(paramsGrid.origin.x),
			                                             MNM::real_t(paramsGrid.origin.y),
			                                             MNM::real_t(paramsGrid.origin.z));
			const bool bOffsetTriangleUp = false;
			const MNM::vector3_t originOffset = origin + MNM::vector3_t(0, 0, MNM::real_t::fraction(725, 10000));

			auto drawTriangle = [originOffset, bOffsetTriangleUp](IRenderAuxGeom* renderAuxGeom, const MNM::vector3_t& a, const MNM::vector3_t& b, const MNM::vector3_t& c, const ColorB& color)
			{
				Vec3 va, vb, vc;
				if (bOffsetTriangleUp)
				{
					va = (a + originOffset).GetVec3();
					vb = (b + originOffset).GetVec3();
					vc = (c + originOffset).GetVec3();
				}
				else
				{
					const Vec3 vao = a.GetVec3();
					const Vec3 vbo = b.GetVec3();
					const Vec3 vco = c.GetVec3();

					Triangle t(vao, vbo, vco);
					const Vec3 n = t.GetNormal() * 0.07f;

					va = vao + n;
					vb = vbo + n;
					vc = vco + n;
				}
				renderAuxGeom->DrawTriangle(va, color, vb, color, vc, color);
			};

			auto drawPath = [](IRenderAuxGeom* pRenderAuxGeom, const CPathHolder<PathPointDescriptor>& path, const ColorB& color, const Vec3& offset)
			{
				const size_t pathSize = path.Size();
				if (pathSize > 0)
				{
					const float radius = 0.015f;

					for (size_t j = 0; j < pathSize - 1; ++j)
					{
						const Vec3 start = path.At(j);
						const Vec3 end = path.At(j + 1);
						pRenderAuxGeom->DrawLine(start + offset, color, end + offset, color, 4.0f);
						pRenderAuxGeom->DrawSphere(start + offset, radius, color);
					}

					pRenderAuxGeom->DrawSphere(path.At(pathSize - 1) + offset, radius, color);
				}
			};

			IRenderAuxGeom* renderAuxGeom = gEnv->pRenderer->GetIRenderAuxGeom();

			const Vec3 startLoc = debugObjectStart->GetEntity() ? debugObjectStart->GetEntity()->GetWorldPos() : debugObjectStart->GetPos();
			const Vec3 endLoc = debugObjectEnd->GetPos();

			const MNM::real_t hrange = MNM::real_t(1.0f);
			const MNM::real_t vrange = MNM::real_t(1.0f);

			MNM::vector3_t fixedPointStartLoc;
			const MNM::TriangleID triStart = navMesh.GetClosestTriangle(
			  MNM::vector3_t(startLoc) - origin, vrange, hrange, nullptr, &fixedPointStartLoc);
			//fixedPointStartLoc += origin;

			if (triStart)
			{
				MNM::vector3_t a, b, c;
				navMesh.GetVertices(triStart, a, b, c);

				drawTriangle(renderAuxGeom, a, b, c, ColorB(ColorF(Col_GreenYellow, 0.5f)));
			}

			MNM::vector3_t fixedPointEndLoc;
			const MNM::TriangleID triEnd = navMesh.GetClosestTriangle(
			  MNM::vector3_t(endLoc) - origin, vrange, hrange, nullptr, &fixedPointEndLoc);

			if (triEnd)
			{
				MNM::vector3_t a, b, c;
				navMesh.GetVertices(triEnd, a, b, c);

				drawTriangle(renderAuxGeom, a, b, c, ColorB(Col_MidnightBlue));
			}

			CTimeValue timeTotal(0ll);
			CTimeValue stringPullingTotalTime(0ll);
			float totalPathLength = 0;
			if (triStart && triEnd)
			{
				const MNM::vector3_t startToEnd = (fixedPointStartLoc - fixedPointEndLoc);
				const MNM::real_t startToEndDist = startToEnd.lenNoOverflow();
				MNM::CNavMesh::WayQueryWorkingSet workingSet;
				workingSet.aStarOpenList.SetFrameTimeQuota(0.0f);
				workingSet.aStarOpenList.SetUpForPathSolving(navMesh.GetTriangleCount(), triStart, fixedPointStartLoc, startToEndDist);

				CTimeValue timeStart = gEnv->pTimer->GetAsyncTime();

				MNM::DangerousAreasList dangersInfo;
				MNM::DangerAreaConstPtr info;
				const Vec3& cameraPos = gAIEnv.GetDebugRenderer()->GetCameraPos(); // To simulate the player position and evaluate the path generation
				info.reset(new MNM::DangerAreaT<MNM::eWCT_Direction>(cameraPos, 0.0f, gAIEnv.CVars.PathfinderDangerCostForAttentionTarget));
				dangersInfo.push_back(info);
				// This object is used to simulate the explosive threat and debug draw the behavior of the pathfinding
				CAIObject* debugObjectExplosiveThreat = gAIEnv.pAIObjectManager->GetAIObjectByName("MNMPathExplosiveThreat");
				if (debugObjectExplosiveThreat)
				{
					info.reset(new MNM::DangerAreaT<MNM::eWCT_Range>(debugObjectExplosiveThreat->GetPos(),
					                                                 gAIEnv.CVars.PathfinderExplosiveDangerRadius, gAIEnv.CVars.PathfinderDangerCostForExplosives));
					dangersInfo.push_back(info);
				}

				const size_t k_MaxWaySize = 512;
				const float pathSharingPenalty = .0f;
				const float pathLinkSharingPenalty = .0f;
				MNM::CNavMesh::WayQueryRequest inputParams(debugObjectStart->CastToIAIActor(), triStart, startLoc, triEnd, endLoc,
				                                           offMeshNavigation, *offMeshNavigationManager, dangersInfo);
				MNM::CNavMesh::WayQueryResult result(k_MaxWaySize);

				const bool hasPathfindingFinished = (navMesh.FindWay(inputParams, workingSet, result) == MNM::CNavMesh::eWQR_Done);

				CTimeValue timeEnd = gEnv->pTimer->GetAsyncTime();
				timeTotal = timeEnd - timeStart;

				assert(hasPathfindingFinished);

				const MNM::WayTriangleData* const pOutputWay = result.GetWayData();
				const size_t outputWaySize = result.GetWaySize();

				for (size_t i = 0; i < outputWaySize; ++i)
				{
					if ((pOutputWay[i].triangleID != triStart) && (pOutputWay[i].triangleID != triEnd))
					{
						MNM::vector3_t a, b, c;

						navMesh.GetVertices(pOutputWay[i].triangleID, a, b, c);

						drawTriangle(renderAuxGeom, a, b, c, ColorB(ColorF(Col_Maroon, 0.5f)));
					}
				}

				const bool bPathFound = (result.GetWaySize() != 0);
				if (bPathFound)
				{
					CPathHolder<PathPointDescriptor> outputPath;
					if (CMNMPathfinder::ConstructPathFromFoundWay(result, navMesh, offMeshNavigationManager, fixedPointStartLoc.GetVec3(), fixedPointEndLoc.GetVec3(), *&outputPath))
					{
						const Vec3 pathVerticalOffset = Vec3(.0f, .0f, .1f);
						drawPath(renderAuxGeom, outputPath, Col_Gray, pathVerticalOffset);

						const bool bBeautifyPath = (gAIEnv.CVars.BeautifyPath != 0);
						CTimeValue stringPullingStartTime = gEnv->pTimer->GetAsyncTime();
						if (bBeautifyPath)
						{
							outputPath.PullPathOnNavigationMesh(navMesh, gAIEnv.CVars.PathStringPullingIterations);
						}
						stringPullingTotalTime = gEnv->pTimer->GetAsyncTime() - stringPullingStartTime;

						if (bBeautifyPath)
						{
							drawPath(renderAuxGeom, outputPath, Col_Black, pathVerticalOffset);
						}

						const size_t pathSize = outputPath.Size();
						for (size_t j = 0; pathSize > 0 && j < pathSize - 1; ++j)
						{
							const Vec3 start = outputPath.At(j);
							const Vec3 end = outputPath.At(j + 1);
							totalPathLength += Distance::Point_Point(start, end);
						}
					}
				}
			}

			const stack_string predictionName = gAIEnv.CVars.MNMPathfinderPositionInTrianglePredictionType ? "Advanced prediction" : "Triangle Center";

			CDebugDrawContext dc;

			dc->Draw2dLabel(10.0f, 172.0f, 1.3f, Col_White, false,
			                "Start: %08x  -  End: %08x - Total Pathfinding time: %.4fms -- Type of prediction for the point inside each triangle: %s", triStart, triEnd, timeTotal.GetMilliSeconds(), predictionName.c_str());
			dc->Draw2dLabel(10.0f, 184.0f, 1.3f, Col_White, false,
			                "String pulling operation - Iteration %d  -  Total time: %.4fms -- Total Length: %f", gAIEnv.CVars.PathStringPullingIterations, stringPullingTotalTime.GetMilliSeconds(), totalPathLength);
		}
	}
}

static bool FindObjectToTestIslandConnectivity(const char* szName, Vec3& outPos, IEntity** ppOutEntityToTestOffGridLinks)
{
	if (const CAIObject* pAiObject = gAIEnv.pAIObjectManager->GetAIObjectByName(szName))
	{
		outPos = pAiObject->GetPos();

		if (ppOutEntityToTestOffGridLinks)
		{
			const IAIPathAgent* pPathAgent = pAiObject->CastToIAIActor();
			assert(pPathAgent);
			if (pPathAgent)
			{
				(*ppOutEntityToTestOffGridLinks) = pPathAgent->GetPathAgentEntity();
			}
		}
		return true;
	}
	else if (IEntity* pEntity = gEnv->pEntitySystem->FindEntityByName(szName))
	{
		outPos = pEntity->GetWorldPos();

		if (ppOutEntityToTestOffGridLinks)
		{
			(*ppOutEntityToTestOffGridLinks) = pEntity;
		}
		return true;
	}
	return false;
}

void NavigationSystemDebugDraw::DebugDrawIslandConnection(NavigationSystem& navigationSystem, const DebugDrawSettings& settings)
{
	// NOTE: difference between two possible start entities:
	// - "MNMIslandStart" is used during island connectivity check to traverse OffNavMesh links;
	// - "MNMIslandStartAnyLink" is not used during connectivity check, so any OffNavMesh link is considered to be traversable all the time.
	const char* szStartName = "MNMIslandStart";
	const char* szStartNameAnyLink = "MNMIslandStartAnyLink";
	const char* szEndName = "MNMIslandEnd";

	Vec3 startPos;
	Vec3 endPos;
	IEntity* pEntityToTestOffGridLinksOrNull = nullptr;

	if (!FindObjectToTestIslandConnectivity(szStartName, startPos, &pEntityToTestOffGridLinksOrNull))
	{
		if (!FindObjectToTestIslandConnectivity(szStartNameAnyLink, startPos, nullptr))
		{
			return;
		}
	}

	if (!FindObjectToTestIslandConnectivity(szEndName, endPos, nullptr))
	{
		return;
	}

	const bool isReachable = gAIEnv.pNavigationSystem->IsPointReachableFromPosition(m_agentTypeID, pEntityToTestOffGridLinksOrNull, startPos, endPos);

	CDebugDrawContext dc;
	dc->Draw2dLabel(10.0f, 250.0f, 1.6f, isReachable ? Col_ForestGreen : Col_VioletRed, false, isReachable ? "The two islands ARE connected" : "The two islands ARE NOT connected");
}

void NavigationSystemDebugDraw::DebugDrawNavigationMeshesForSelectedAgent(NavigationSystem& navigationSystem, MNM::TileID excludeTileID)
{
	FRAME_PROFILER("NavigationSystemDebugDraw::DebugDrawNavigationMeshesForSelectedAgent()", gEnv->pSystem, PROFILE_AI);

	AgentType& agentType = navigationSystem.m_agentTypes[m_agentTypeID - 1];
	AgentType::Meshes::const_iterator it = agentType.meshes.begin();
	AgentType::Meshes::const_iterator end = agentType.meshes.end();

	CCamera& viewCamera = gEnv->pSystem->GetViewCamera();

	for (; it != end; ++it)
	{
		const NavigationMesh& mesh = navigationSystem.GetMesh(it->id);

		if(!viewCamera.IsAABBVisible_F(navigationSystem.m_volumes[mesh.boundary].aabb))
			continue;

		size_t drawFlag = MNM::STile::DrawTriangles | MNM::STile::DrawMeshBoundaries;
		if (gAIEnv.CVars.MNMDebugAccessibility)
			drawFlag |= MNM::STile::DrawAccessibility;

		switch (gAIEnv.CVars.DebugDrawNavigation)
		{
		case 0:
		case 1:
			mesh.navMesh.Draw(drawFlag, excludeTileID);
			break;
		case 2:
			mesh.navMesh.Draw(drawFlag | MNM::STile::DrawInternalLinks, excludeTileID);
			break;
		case 3:
			mesh.navMesh.Draw(drawFlag | MNM::STile::DrawInternalLinks |
			                  MNM::STile::DrawExternalLinks | MNM::STile::DrawOffMeshLinks, excludeTileID);
			break;
		case 4:
			mesh.navMesh.Draw(drawFlag | MNM::STile::DrawInternalLinks |
			                  MNM::STile::DrawExternalLinks | MNM::STile::DrawOffMeshLinks |
			                  MNM::STile::DrawTrianglesId, excludeTileID);
			break;
		case 5:
			mesh.navMesh.Draw(drawFlag | MNM::STile::DrawInternalLinks | MNM::STile::DrawExternalLinks |
			                  MNM::STile::DrawOffMeshLinks | MNM::STile::DrawTrianglesId | MNM::STile::DrawIslandsId, excludeTileID);
			break;
		case 6:
			mesh.navMesh.Draw(drawFlag | MNM::STile::DrawInternalLinks |
			                  MNM::STile::DrawExternalLinks | MNM::STile::DrawOffMeshLinks | MNM::STile::DrawTriangleBackfaces, excludeTileID);
			break;

		default:
			break;
		}
	}
}

void NavigationSystemDebugDraw::DebugDrawNavigationSystemState(NavigationSystem& navigationSystem)
{
	CDebugDrawContext dc;

	if (gAIEnv.CVars.DebugDrawNavigation)
	{
		switch (navigationSystem.m_state)
		{
		case NavigationSystem::Working:
			dc->Draw2dLabel(10.0f, 300.0f, 1.6f, Col_Yellow, false, "Navigation System Working");
			dc->Draw2dLabel(10.0f, 322.0f, 1.2f, Col_White, false, "Processing: %d\nRemaining: %d\nThroughput: %.2f/s\n"
			                                                       "Cache Hits: %.2f/s",
			                navigationSystem.m_runningTasks.size(), navigationSystem.GetWorkingQueueSize(), navigationSystem.m_throughput, navigationSystem.m_cacheHitRate);
			break;
		case NavigationSystem::Idle:
			dc->Draw2dLabel(10.0f, 300.0f, 1.6f, Col_ForestGreen, false, "Navigation System Idle");
			break;
		default:
			assert(0);
			break;
		}
		static_cast<CMNMUpdatesManager*>(navigationSystem.GetUpdateManager())->DebugDraw();
	}
}

void NavigationSystemDebugDraw::DebugDrawMemoryStats(NavigationSystem& navigationSystem)
{
	if (gAIEnv.CVars.MNMProfileMemory)
	{
		const float kbInvert = 1.0f / 1024.0f;

		const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		const float grey[4] = { 0.65f, 0.65f, 0.65f, 1.0f };

		float posY = 60.f;
		float posX = 30.f;

		size_t totalNavigationSystemMemory = 0;

		ICrySizer* pSizer = gEnv->pSystem->CreateSizer();

		for (uint16 i = 0; i < navigationSystem.m_meshes.capacity(); ++i)
		{
			if (!navigationSystem.m_meshes.index_free(i))
			{
				const NavigationMeshID meshID(navigationSystem.m_meshes.get_index_id(i));

				const NavigationMesh& mesh = navigationSystem.m_meshes[meshID];
				const MNM::OffMeshNavigation& offMeshNavigation = navigationSystem.GetOffMeshNavigationManager()->GetOffMeshNavigationForMesh(meshID);
				const AgentType& agentType = navigationSystem.m_agentTypes[mesh.agentTypeID - 1];

				const NavigationMesh::ProfileMemoryStats meshMemStats = mesh.GetMemoryStats(pSizer);
				const MNM::OffMeshNavigation::ProfileMemoryStats offMeshMemStats = offMeshNavigation.GetMemoryStats(pSizer);
				const OffMeshNavigationManager::ProfileMemoryStats linkMemStats = navigationSystem.GetOffMeshNavigationManager()->GetMemoryStats(pSizer, meshID);

				size_t totalMemory = meshMemStats.totalNavigationMeshMemory + offMeshMemStats.totalSize + linkMemStats.totalSize;

				IRenderAuxText::Draw2dLabel(posX, posY, 1.3f, white, false, "Mesh: %s Agent: %s - Total Memory %.3f KB : Mesh %.3f KB / Grid %.3f KB / OffMesh %.3f",
				                            mesh.name.c_str(), agentType.name.c_str(),
				                            totalMemory * kbInvert, meshMemStats.totalNavigationMeshMemory * kbInvert, meshMemStats.navMeshProfiler.GetMemoryUsage() * kbInvert, (offMeshMemStats.totalSize + linkMemStats.totalSize) * kbInvert);
				posY += 12.0f;
				IRenderAuxText::Draw2dLabel(posX, posY, 1.3f, grey, false, "Tiles [%d] / Vertices [%d] - %.3f KB / Triangles [%d] - %.3f KB / Links [%d] - %.3f KB / BVNodes [%d] - %.3f KB",
				                            meshMemStats.navMeshProfiler[MNM::CNavMesh::TileCount],
				                            meshMemStats.navMeshProfiler[MNM::CNavMesh::VertexCount], meshMemStats.navMeshProfiler[MNM::CNavMesh::VertexMemory].used * kbInvert,
				                            meshMemStats.navMeshProfiler[MNM::CNavMesh::TriangleCount], meshMemStats.navMeshProfiler[MNM::CNavMesh::TriangleMemory].used * kbInvert,
				                            meshMemStats.navMeshProfiler[MNM::CNavMesh::LinkCount], meshMemStats.navMeshProfiler[MNM::CNavMesh::LinkMemory].used * kbInvert,
				                            meshMemStats.navMeshProfiler[MNM::CNavMesh::BVTreeNodeCount], meshMemStats.navMeshProfiler[MNM::CNavMesh::BVTreeMemory].used * kbInvert
				                            );

				posY += 12.0f;
				IRenderAuxText::Draw2dLabel(posX, posY, 1.3f, grey, false, "OffMesh Memory : Tile Links %.3f KB / Object Info %.3f KB",
				                            offMeshMemStats.offMeshTileLinksMemory * kbInvert,
				                            linkMemStats.linkInfoSize * kbInvert
				                            );
				posY += 13.0f;

				totalNavigationSystemMemory += totalMemory;
			}
		}

		//TODO: Add Navigation system itself (internal containers and others)

		IRenderAuxText::Draw2dLabel(40.0f, 20.0f, 1.5f, white, false, "Navigation System: %.3f KB", totalNavigationSystemMemory * kbInvert);

		pSizer->Release();
	}
}

NavigationSystemDebugDraw::DebugDrawSettings NavigationSystemDebugDraw::GetDebugDrawSettings(NavigationSystem& navigationSystem)
{
	static Vec3 lastLocation(ZERO);

	static size_t selectedX = 0;
	static size_t selectedY = 0;
	static size_t selectedZ = 0;

	DebugDrawSettings settings;
	settings.forceGeneration = false;

	if (CAIObject* pMNMDebugLocator = gAIEnv.pAIObjectManager->GetAIObjectByName("MNMDebugLocator"))
	{
		const Vec3 debugLocation = pMNMDebugLocator->GetPos();

		if ((lastLocation - debugLocation).len2() > 0.00001f)
		{
			settings.meshID = navigationSystem.GetEnclosingMeshID(m_agentTypeID, debugLocation);

			const NavigationMesh& mesh = navigationSystem.GetMesh(settings.meshID);
			const MNM::CNavMesh::SGridParams& params = mesh.navMesh.GetGridParams();

			size_t x = (size_t)((debugLocation.x - params.origin.x) / (float)params.tileSize.x);
			size_t y = (size_t)((debugLocation.y - params.origin.y) / (float)params.tileSize.y);
			size_t z = (size_t)((debugLocation.z - params.origin.z) / (float)params.tileSize.z);

			if ((x != selectedX) || (y != selectedY) || (z != selectedZ))
			{
				settings.forceGeneration = true;

				selectedX = x;
				selectedY = y;
				selectedZ = z;
			}

			lastLocation = debugLocation;
		}
	}
	else
	{
		lastLocation.zero();
	}

	settings.selectedX = selectedX;
	settings.selectedY = selectedY;
	settings.selectedZ = selectedZ;

	if (!settings.meshID)
	{
		settings.meshID = navigationSystem.GetEnclosingMeshID(m_agentTypeID, lastLocation);
	}

	return settings;
}

//////////////////////////////////////////////////////////////////////////

void NavigationSystemDebugDraw::NavigationSystemWorkingProgress::Update(const float frameTime, const size_t queueSize)
{
	m_currentQueueSize = queueSize;
	m_initialQueueSize = (queueSize > 0) ? max(m_initialQueueSize, queueSize) : 0;

	const float updateTime = (queueSize > 0) ? frameTime : -2.0f * frameTime;
	m_timeUpdating = clamp_tpl(m_timeUpdating + updateTime, 0.0f, 1.0f);
}

void NavigationSystemDebugDraw::NavigationSystemWorkingProgress::Draw()
{
	const bool draw = (m_timeUpdating > 0.0f);

	if (!draw)
		return;

	BeginDraw();

	const float width = (float)gEnv->pRenderer->GetWidth();
	const float height = (float)gEnv->pRenderer->GetHeight();

	const ColorB backGroundColor(0, 255, 0, CLAMP((int)(0.35f * m_timeUpdating * 255.0f), 0, 255));
	const ColorB progressColor(0, 255, 0, CLAMP((int)(0.8f * m_timeUpdating * 255.0f), 0, 255));

	const float progressFraction = (m_initialQueueSize > 0) ? clamp_tpl(1.0f - ((float)m_currentQueueSize / (float)m_initialQueueSize), 0.0f, 1.0f) : 1.0f;

	const Vec2 progressBarLocation(0.1f, 0.91f);
	const Vec2 progressBarSize(0.2f, 0.025f);

	const float white[4] = { 1.0f, 1.0f, 1.0f, 0.85f * m_timeUpdating };

	IRenderAuxText::Draw2dLabel(progressBarLocation.x * width, (progressBarLocation.y * height) - 18.0f, 1.4f, white, false, "Processing Navigation Meshes");

	DrawQuad(progressBarLocation, progressBarSize, backGroundColor);
	DrawQuad(progressBarLocation, Vec2(progressBarSize.x * progressFraction, progressBarSize.y), progressColor);

	EndDraw();
}

void NavigationSystemDebugDraw::NavigationSystemWorkingProgress::BeginDraw()
{
	IRenderAuxGeom* pRenderAux = gEnv->pRenderer->GetIRenderAuxGeom();
	if (pRenderAux)
	{
		m_oldRenderFlags = pRenderAux->GetRenderFlags();

		SAuxGeomRenderFlags newFlags = e_Def3DPublicRenderflags;
		newFlags.SetMode2D3DFlag(e_Mode2D);
		newFlags.SetAlphaBlendMode(e_AlphaBlended);

		pRenderAux->SetRenderFlags(newFlags);
	}
}

void NavigationSystemDebugDraw::NavigationSystemWorkingProgress::EndDraw()
{
	IRenderAuxGeom* pRenderAux = gEnv->pRenderer->GetIRenderAuxGeom();
	if (pRenderAux)
	{
		pRenderAux->SetRenderFlags(m_oldRenderFlags);
	}
}

void NavigationSystemDebugDraw::NavigationSystemWorkingProgress::DrawQuad(const Vec2& origin, const Vec2& size, const ColorB& color)
{
	Vec3 quadVertices[4];
	const vtx_idx auxIndices[6] = { 2, 1, 0, 2, 3, 1 };

	quadVertices[0] = Vec3(origin.x, origin.y, 1.0f);
	quadVertices[1] = Vec3(origin.x + size.x, origin.y, 1.0f);
	quadVertices[2] = Vec3(origin.x, origin.y + size.y, 1.0f);
	quadVertices[3] = Vec3(origin.x + size.x, origin.y + size.y, 1.0f);

	IRenderAuxGeom* pRenderAux = gEnv->pRenderer->GetIRenderAuxGeom();
	if (pRenderAux)
	{
		const SAuxGeomRenderFlags oldFlags = pRenderAux->GetRenderFlags();
		SAuxGeomRenderFlags flags = oldFlags;
		flags.SetMode2D3DFlag(e_Mode2D);
		flags.SetDrawInFrontMode(e_DrawInFrontOn);
		flags.SetDepthTestFlag(e_DepthTestOff);
		pRenderAux->SetRenderFlags(flags);
		pRenderAux->DrawTriangles(quadVertices, 4, auxIndices, 6, color);
		pRenderAux->SetRenderFlags(oldFlags);
	}
}

//////////////////////////////////////////////////////////////////////////

NavigationMesh::ProfileMemoryStats NavigationMesh::GetMemoryStats(ICrySizer* pSizer) const
{
	ProfileMemoryStats memoryStats(navMesh.GetProfiler());

	size_t initialSize = pSizer->GetTotalSize();
	{
		pSizer->AddObjectSize(this);
		pSizer->AddObject(&navMesh, memoryStats.navMeshProfiler.GetMemoryUsage());
		pSizer->AddContainer(exclusions);
		pSizer->AddString(name);

		memoryStats.totalNavigationMeshMemory = pSizer->GetTotalSize() - initialSize;
	}

	return memoryStats;
}

#endif

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

#if NAVIGATION_SYSTEM_EDITOR_BACKGROUND_UPDATE

void NavigationSystemBackgroundUpdate::Thread::ThreadEntry()
{
	while (!m_requestedStop)
	{
		if (m_navigationSystem.GetState() == INavigationSystem::Working)
		{
			const CTimeValue startedUpdate = gEnv->pTimer->GetAsyncTime();

			m_navigationSystem.UpdateMeshes(0.0333f, false, true, true);

			const CTimeValue lastUpdateTime = gEnv->pTimer->GetAsyncTime() - startedUpdate;

			const unsigned int sleepTime = max(10u, min(0u, 33u - (unsigned int)lastUpdateTime.GetMilliSeconds()));

			CrySleep(sleepTime);
		}
		else
		{
			CrySleep(50);
		}
	}
}

void NavigationSystemBackgroundUpdate::Thread::SignalStopWork()
{
	m_requestedStop = true;
}

//////////////////////////////////////////////////////////////////////////

bool NavigationSystemBackgroundUpdate::Start()
{
	if (m_pBackgroundThread == NULL)
	{
		m_pBackgroundThread = new Thread(m_navigationSystem);
		if (!gEnv->pThreadManager->SpawnThread(m_pBackgroundThread, "NavigationSystemBackgroundUpdate"))
		{
			CRY_ASSERT_MESSAGE(false, "Error spawning \"NavigationSystemBackgroundUpdate\" thread.");
			delete m_pBackgroundThread;
			m_pBackgroundThread = NULL;
			return false;
		}

		return true;
	}

	return false;
}

bool NavigationSystemBackgroundUpdate::Stop()
{
	if (m_pBackgroundThread != NULL)
	{
		m_pBackgroundThread->SignalStopWork();
		gEnv->pThreadManager->JoinThread(m_pBackgroundThread, eJM_Join);

		delete m_pBackgroundThread;
		m_pBackgroundThread = NULL;

		return true;
	}

	return false;
}

void NavigationSystemBackgroundUpdate::OnSystemEvent(ESystemEvent event, UINT_PTR wparam, UINT_PTR lparam)
{
	CRY_ASSERT(gEnv->IsEditor());

	if (event == ESYSTEM_EVENT_LEVEL_UNLOAD ||
	    event == ESYSTEM_EVENT_LEVEL_LOAD_START)
	{
		Pause(true);
	}
	else if (event == ESYSTEM_EVENT_LEVEL_LOAD_END)
	{
		Pause(false);
	}
	else if (event == ESYSTEM_EVENT_CHANGE_FOCUS)
	{
		// wparam != 0 is focused, wparam == 0 is not focused
		const bool startBackGroundUpdate = (wparam == 0) && (gAIEnv.CVars.MNMEditorBackgroundUpdate != 0) && (m_navigationSystem.GetState() == INavigationSystem::Working) && !m_paused;

		if (startBackGroundUpdate)
		{
			if (Start())
			{
				CryLog("NavMesh generation background thread started");
			}
		}
		else
		{
			if (Stop())
			{
				CryLog("NavMesh generation background thread stopped");
			}
		}
	}
}

void NavigationSystemBackgroundUpdate::RegisterAsSystemListener()
{
	if (IsEnabled())
	{
		gEnv->pSystem->GetISystemEventDispatcher()->RegisterListener(this, "NavigationSystemBackgroundUpdate");
	}
}

void NavigationSystemBackgroundUpdate::RemoveAsSystemListener()
{
	if (IsEnabled())
	{
		gEnv->pSystem->GetISystemEventDispatcher()->RemoveListener(this);
	}
}

#endif
