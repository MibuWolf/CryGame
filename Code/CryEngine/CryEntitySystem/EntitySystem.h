// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  File name:   EntitySystem.h
//  Version:     v1.00
//  Created:     24/5/2004 by Timur.
//  Compilers:   Visual Studio.NET 2003
//  Description:
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#ifndef __EntitySystem_h__
#define __EntitySystem_h__
#pragma once

#include <CryEntitySystem/IEntitySystem.h>
#include <CrySystem/ISystem.h>
#include <CrySystem/ITimer.h>
#include "SaltBufferArray.h"          // SaltBufferArray<>
#include "EntityTimeoutList.h"
#include <CryCore/StlUtils.h>
#include <CryMemory/STLPoolAllocator.h>
#include <CryMemory/STLGlobalAllocator.h>

//////////////////////////////////////////////////////////////////////////
// forward declarations.
//////////////////////////////////////////////////////////////////////////
class CEntity;
struct ICVar;
struct IPhysicalEntity;
struct IEntityComponent;
class CEntityClassRegistry;
class CScriptBind_Entity;
class CPhysicsEventListener;
class CAreaManager;
class CBreakableManager;
class CEntityArchetypeManager;
class CPartitionGrid;
class CProximityTriggerSystem;
class CEntityLayer;
class CEntityLoadManager;
struct SEntityLayerGarbage;
class CGeomCacheAttachmentManager;
class CCharacterBoneAttachmentManager;
class CEntitiesComponentPropertyCache;
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
typedef std::unordered_map<EntityId, CEntity*, stl::hash_uint32> EntityMap;
typedef std::vector<EntityId>                                    EntityIdVector;
typedef std::vector<EntityGUID>                                  EntityGuidVector;

//////////////////////////////////////////////////////////////////////////
struct SEntityTimerEvent
{
	EntityId entityId;
	int      nTimerId;
	int      nMilliSeconds;
};

//////////////////////////////////////////////////////////////////////////
struct SEntityAttachment
{
	EntityId   child;
	EntityId   parent;
	EntityGUID parentGuid;
	Vec3       pos;
	Quat       rot;
	Vec3       scale;
	int        flags;
	string     target;

	SEntityAttachment() : child(0), parent(0), pos(ZERO), rot(ZERO), scale(ZERO), flags(0) {}
};

//////////////////////////////////////////////////////////////////////////
// Structure for extended information about loading an entity
//	Supports reusing an entity container if specified
struct SEntityLoadParams
{
	SEntitySpawnParams spawnParams;
	bool               bCallInit = false;

	SEntityLoadParams();
	SEntityLoadParams(CEntity* pReuseEntity, SEntitySpawnParams& resetParams);
	~SEntityLoadParams();

	SEntityLoadParams& operator=(const SEntityLoadParams& other);
	SEntityLoadParams(const SEntityLoadParams& other) { *this = other; }

private:
	void        AddRef();
	void        RemoveRef();

	static bool CloneXmlNode(const XmlNodeRef sourceNode, XmlNodeRef destNode);
};
typedef std::vector<SEntityLoadParams>              TEntityLoadParamsContainer;

typedef CSaltHandle<unsigned short, unsigned short> CEntityHandle;

// Mutatable set is a specialized customization of the std::set where items can be added or removed within foreach iteration.
template<typename T>
class CMutatableSet
{
public:
	void insert(const T& item)
	{
		if (m_bIterating)
			m_changeRequests.emplace_back(item, EChangeRequest::Add);
		else
			m_set.insert(item);
	}
	void erase(const T& item)
	{
		if (m_bIterating)
			m_changeRequests.emplace_back(item, EChangeRequest::Remove);
		else
			m_set.erase(item);
	}
	void clear()
	{
		m_set.clear();
		m_changeRequests.clear();
	}
	size_t size() const               { return m_set.size(); }
	size_t count(const T& item) const { return m_set.count(item); }

	//! Invokes the provided function f once with each component.
	template<typename LambdaFunction>
	void for_each(LambdaFunction f)
	{
		start_for_each();
		for (auto& item : m_set)
		{
			f(item);
		}
		end_for_each();
	}
protected:
	void start_for_each()
	{
		assert(!m_bIterating);
		m_bIterating = true;
	}
	void end_for_each()
	{
		assert(m_bIterating);
		m_bIterating = false;
		for (const auto& itemRequestPair : m_changeRequests)
		{
			const T& item = itemRequestPair.first;
			if (itemRequestPair.second == EChangeRequest::Add)
			{
				m_set.insert(item);
			}
			else
			{
				m_set.erase(item);
			}
		}
		m_changeRequests.clear();
	}
private:
	std::set<T> m_set;

	enum class EChangeRequest
	{
		Add,
		Remove
	};
	std::vector<std::pair<T, EChangeRequest>> m_changeRequests;
	bool m_bIterating = false;
};

// Mutatable map is a specialized customization of the std::map where items can be added or removed within foreach iteration.
template<typename TKey, typename TValue>
class CMutatableMap
{
	typename std::pair<TKey, TValue> pair_type;
public:
	void insert(const TKey& key, const TValue& value)
	{
		if (m_bIterating)
			m_added.push_back(std::make_pair(key, value));
		else
			m_map.insert(std::make_pair(key, value));
	}
	void erase(const TKey& key)
	{
		if (m_bIterating)
			m_removed.push_back(key);
		else
			m_map.erase(key);
	}
	void clear()
	{
		m_map.clear();
		m_added.clear();
		m_removed.clear();
	}
	size_t size() const                 { return m_map.size(); }
	size_t count(const TKey& key) const { return m_map.count(key); }

	//! Invokes the provided function f once with each component.
	template<typename LambdaFunction>
	void for_each(LambdaFunction f)
	{
		start_for_each();
		for (auto& item : m_map)
		{
			f(item.first, item.second);
		}
		end_for_each();
	}
protected:
	void start_for_each()
	{
		assert(!m_bIterating);
		m_bIterating = true;
	}
	void end_for_each()
	{
		assert(m_bIterating);
		m_bIterating = false;
		for (const auto& item : m_added)
			m_map.insert(item);
		for (const auto& item : m_removed)
			m_map.erase(item->first);
		m_added.clear();
		m_removed.clear();
	}
private:
	std::map<TKey, TValue>               m_map;
	std::vector<std::pair<TKey, TValue>> m_added;
	std::vector<TKey>                    m_removed;
	bool m_bIterating = false;
};

//////////////////////////////////////////////////////////////////////
class CEntitySystem : public IEntitySystem
{
public:
	CEntitySystem(ISystem* pSystem);
	~CEntitySystem();

	bool Init(ISystem* pSystem);

	// interface IEntitySystem ------------------------------------------------------
	virtual void                              Release() final;
	virtual void                              Update() final;
	virtual void                              DeletePendingEntities() final;
	virtual void                              PrePhysicsUpdate() final;
	virtual void                              Reset() final;
	virtual void                              Unload() final;
	virtual void                              PurgeHeaps() final;
	virtual IEntityClassRegistry*             GetClassRegistry() final;
	virtual IEntity*                          SpawnEntity(SEntitySpawnParams& params, bool bAutoInit = true) final;
	virtual bool                              InitEntity(IEntity* pEntity, SEntitySpawnParams& params) final;
	virtual IEntity*                          GetEntity(EntityId id) const final;
	virtual IEntity*                          FindEntityByName(const char* sEntityName) const final;
	virtual void                              ReserveEntityId(const EntityId id) final;
	virtual EntityId                          ReserveUnknownEntityId() final;
	virtual void                              RemoveEntity(EntityId entity, bool bForceRemoveNow = false) final;
	virtual uint32                            GetNumEntities() const final;
	virtual IEntityIt*                        GetEntityIterator() final;
	virtual void                              SendEventToAll(SEntityEvent& event) final;
	virtual int                               QueryProximity(SEntityProximityQuery& query) final;
	virtual void                              ResizeProximityGrid(int nWidth, int nHeight) final;
	virtual int                               GetPhysicalEntitiesInBox(const Vec3& origin, float radius, IPhysicalEntity**& pList, int physFlags) const final;
	virtual IEntity*                          GetEntityFromPhysics(IPhysicalEntity* pPhysEntity) const final;
	virtual void                              AddSink(IEntitySystemSink* pSink, uint32 subscriptions, uint64 onEventSubscriptions) final;
	virtual void                              RemoveSink(IEntitySystemSink* pSink) final;
	virtual void                              PauseTimers(bool bPause, bool bResume = false) final;
	virtual bool                              IsIDUsed(EntityId nID) const final;
	virtual void                              GetMemoryStatistics(ICrySizer* pSizer) const final;
	virtual ISystem*                          GetSystem() const final { return m_pISystem; };
	virtual void                              SetNextSpawnId(EntityId id) final;
	virtual void                              ResetAreas() final;
	virtual void                              UnloadAreas() final;

	virtual void                              AddEntityEventListener(EntityId nEntity, EEntityEvent event, IEntityEventListener* pListener) final;
	virtual void                              RemoveEntityEventListener(EntityId nEntity, EEntityEvent event, IEntityEventListener* pListener) final;

	virtual void                              AddEntityLayerListener(const char* szLayerName, IEntityLayerListener* pListener, const bool bCaseSensitive = true) final;
	virtual void                              RemoveEntityLayerListener(const char* szLayerName, IEntityLayerListener* pListener, const bool bCaseSensitive = true) final;

	virtual EntityId                          FindEntityByGuid(const EntityGUID& guid) const final;

	virtual EntityId                          GenerateEntityIdFromGuid(const EntityGUID& guid) final;

	virtual IEntityArchetype*                 LoadEntityArchetype(XmlNodeRef oArchetype) final;
	virtual IEntityArchetype*                 LoadEntityArchetype(const char* sArchetype) final;
	virtual void                              UnloadEntityArchetype(const char* sArchetype) final;
	virtual IEntityArchetype*                 CreateEntityArchetype(IEntityClass* pClass, const char* sArchetype) final;
	virtual void                              RefreshEntityArchetypesInRegistry() final;
	virtual void                              SetEntityArchetypeManagerExtension(IEntityArchetypeManagerExtension* pEntityArchetypeManagerExtension) final;
	virtual IEntityArchetypeManagerExtension* GetEntityArchetypeManagerExtension() const final;

	virtual void                              Serialize(TSerialize ser) final;

	virtual void                              DumpEntities() final;

	virtual void                              ResumePhysicsForSuppressedEntities(bool bWakeUp) final;
	virtual void                              SaveInternalState(struct IDataWriteStream& writer) const final;
	virtual void                              LoadInternalState(struct IDataReadStream& reader) final;

	virtual int                               GetLayerId(const char* szLayerName) const final;
	virtual const char*                       GetLayerName(int layerId) const final;
	virtual int                               GetLayerChildCount(const char* szLayerName) const final;
	virtual const char*                       GetLayerChild(const char* szLayerName, int childIdx) const final;

	virtual int                               GetVisibleLayerIDs(uint8* pLayerMask, const uint32 maxCount) const final;

	virtual void                              ToggleLayerVisibility(const char* layer, bool isEnabled, bool includeParent = true) final;

	virtual void                              ToggleLayersBySubstring(const char* pSearchSubstring, const char* pExceptionSubstring, bool isEnable) final;

	virtual void                              LockSpawning(bool lock) final { m_bLocked = lock; }

	virtual bool                              OnLoadLevel(const char* szLevelPath) final;

	virtual IEntityLayer*                     AddLayer(const char* szName, const char* szParent, uint16 id, bool bHasPhysics, int specs, bool bDefaultLoaded) final;
	virtual void                              LoadLayers(const char* dataFile) final;
	virtual void                              LinkLayerChildren() final;
	virtual void                              AddEntityToLayer(const char* layer, EntityId id) final;
	virtual void                              RemoveEntityFromLayers(EntityId id) final;
	virtual void                              ClearLayers() final;
	virtual void                              EnableDefaultLayers(bool isSerialized = true) final;
	virtual void                              EnableLayer(const char* layer, bool isEnable, bool isSerialized = true) final;
	virtual void                              EnableLayerSet(const char* const * pLayers, size_t layerCount, bool bIsSerialized = true, IEntityLayerSetUpdateListener* pListener = nullptr) final;
	virtual IEntityLayer*                     FindLayer(const char* szLayerName, const bool bCaseSensitive = true) const final;
	virtual bool                              IsLayerEnabled(const char* layer, bool bMustBeLoaded, bool bCaseSensitive = true) const final;
	virtual bool                              ShouldSerializedEntity(IEntity* pEntity) final;
	virtual void                              RegisterPhysicCallbacks() final;
	virtual void                              UnregisterPhysicCallbacks() final;

	// ------------------------------------------------------------------------
	void                                      OnLevelLoadStart();
	void                                      OnLevelLoadEnd();
	bool                                      IsLoadingLevel() const;

	CEntityLayer* GetLayerForEntity(EntityId id);
	void EnableLayer(IEntityLayer* pLayer, bool bIsEnable, bool bIsSerialized, bool bAffectsChildren);

	bool          OnBeforeSpawn(SEntitySpawnParams& params);
	void          OnEntityReused(IEntity* pEntity, SEntitySpawnParams& params);

	// Sets new entity timer event.
	void AddTimerEvent(SEntityTimerEvent& event, CTimeValue startTime = gEnv->pTimer->GetFrameStartTime());

	//////////////////////////////////////////////////////////////////////////
	// Load entities from XML.
	void         LoadEntities(XmlNodeRef& objectsNode, bool bIsLoadingLevelFile) final;
	void         LoadEntities(XmlNodeRef& objectsNode, bool bIsLoadingLevelFile, const Vec3& segmentOffset, std::vector<IEntity*>* outGlobalEntityIds, std::vector<IEntity*>* outLocalEntityIds) final;

	virtual bool ExtractArcheTypeLoadParams(XmlNodeRef& entityNode, SEntitySpawnParams& spawnParams) const final;
	virtual bool ExtractEntityLoadParams(XmlNodeRef& entityNode, SEntitySpawnParams& spawnParams) const final;
	virtual void BeginCreateEntities(int nAmtToCreate) final;
	virtual bool CreateEntity(XmlNodeRef& entityNode, SEntitySpawnParams& pParams, EntityId& outUsingId) final;
	virtual void EndCreateEntities() final;

	//////////////////////////////////////////////////////////////////////////
	// Called from CEntity implementation.
	//////////////////////////////////////////////////////////////////////////
	void RemoveTimerEvent(EntityId id, int nTimerId);

	// Puts entity into active list.
	void ActivateEntity(CEntity* pEntity, bool bActivate);
	void ActivatePrePhysicsUpdateForEntity(CEntity* pEntity, bool bActivate);
	bool IsPrePhysicsActive(CEntity* pEntity);
	void OnEntityEvent(CEntity* pEntity, SEntityEvent& event);

	// Access to class that binds script to entity functions.
	// Used by Script proxy.
	CScriptBind_Entity* GetScriptBindEntity() { return m_pEntityScriptBinding; };

	// Access to area manager.
	IAreaManager* GetAreaManager() const final { return (IAreaManager*)(m_pAreaManager); }

	// Access to breakable manager.
	virtual IBreakableManager*       GetBreakableManager() const final         { return m_pBreakableManager; };

	CEntityLoadManager*              GetEntityLoadManager() const              { return m_pEntityLoadManager; }

	CGeomCacheAttachmentManager*     GetGeomCacheAttachmentManager() const     { return m_pGeomCacheAttachmentManager; }
	CCharacterBoneAttachmentManager* GetCharacterBoneAttachmentManager() const { return m_pCharacterBoneAttachmentManager; }

	static ILINE uint16              IdToIndex(const EntityId id)              { return id & 0xffff; }
	static ILINE CSaltHandle<>       IdToHandle(const EntityId id)             { return CSaltHandle<>(id >> 16, id & 0xffff); }
	static ILINE EntityId            HandleToId(const CSaltHandle<> id)        { return (((uint32)id.GetSalt()) << 16) | ((uint32)id.GetIndex()); }

	EntityId                         GenerateEntityId(bool bStaticId);

	void                             RegisterEntityGuid(const EntityGUID& guid, EntityId id);
	void                             UnregisterEntityGuid(const EntityGUID& guid);

	CPartitionGrid*                  GetPartitionGrid()          { return m_pPartitionGrid; }
	CProximityTriggerSystem*         GetProximityTriggerSystem() { return m_pProximityTriggerSystem; }

	void                             ChangeEntityName(CEntity* pEntity, const char* sNewName);

	CEntity*                         GetEntityFromID(EntityId id) const;
	ILINE bool                       HasEntity(EntityId id) const { return GetEntityFromID(id) != 0; };

	virtual void                     PurgeDeferredCollisionEvents(bool bForce = false) final;

	virtual void                     DebugDraw() final;

	virtual bool                     EntitiesUseGUIDs() const final                { return m_bEntitiesUseGUIDs; }
	virtual void                     SetEntitiesUseGUIDs(const bool bEnable) final { m_bEntitiesUseGUIDs = bEnable; }

	virtual IBSPTree3D*              CreateBSPTree3D(const IBSPTree3D::FaceList& faceList) final;
	virtual void                     ReleaseBSPTree3D(IBSPTree3D*& pTree) final;

private: // -----------------------------------------------------------------
	void DoPrePhysicsUpdate();
	void DoUpdateLoop(float fFrameTime);

	void DeleteEntity(CEntity* pEntity);
	void UpdateDeletedEntities();
	void RemoveEntityFromActiveList(CEntity* pEntity);
	void UpdateEngineCVars();
	void UpdateTimers();
	void DebugDraw(CEntity* pEntity, float fUpdateTime);

	void DebugDrawEntityUsage();
	void DebugDrawLayerInfo();
	void DebugDrawProximityTriggers();

	void ClearEntityArray();

	void DumpEntity(IEntity* pEntity);

	// slow - to find specific problems
	void CheckInternalConsistency() const;

	//////////////////////////////////////////////////////////////////////////
	// Variables.
	//////////////////////////////////////////////////////////////////////////
	struct OnEventSink
	{
		OnEventSink(uint64 _subscriptions, IEntitySystemSink* _pSink)
			: subscriptions(_subscriptions)
			, pSink(_pSink)
		{
		}

		uint64             subscriptions;
		IEntitySystemSink* pSink;
	};

	typedef std::vector<OnEventSink, stl::STLGlobalAllocator<OnEventSink>>                                                                                                                              EntitySystemOnEventSinks;
	typedef std::vector<IEntitySystemSink*, stl::STLGlobalAllocator<IEntitySystemSink*>>                                                                                                                EntitySystemSinks;
	typedef std::vector<CEntity*>                                                                                                                                                                       DeletedEntities;
	typedef std::multimap<CTimeValue, SEntityTimerEvent, std::less<CTimeValue>, stl::STLPoolAllocator<std::pair<const CTimeValue, SEntityTimerEvent>, stl::PoolAllocatorSynchronizationSinglethreaded>> EntityTimersMap;
	typedef std::multimap<const char*, EntityId, stl::less_stricmp<const char*>>                                                                                                                        EntityNamesMap;
	typedef std::vector<SEntityTimerEvent>                                                                                                                                                              EntityTimersVector;

	EntitySystemSinks        m_sinks[SinkMaxEventSubscriptionCount];        // registered sinks get callbacks for creation and removal
	EntitySystemOnEventSinks m_onEventSinks;

	ISystem*                 m_pISystem;
	std::vector<CEntity*>    m_EntityArray;                 // [id.GetIndex()]=CEntity
	DeletedEntities          m_deletedEntities;
	std::vector<CEntity*>    m_deferredUsedEntities;

	CMutatableSet<EntityId>  m_mapActiveEntities;           // Map of currently active entities (All entities that need per frame update).
	CMutatableSet<EntityId>  m_mapPrePhysicsEntities;       // map of entities requiring pre-physics activation

	EntityNamesMap           m_mapEntityNames;         // Map entity name to entity ID.

	CSaltBufferArray<>       m_EntitySaltBuffer;            // used to create new entity ids (with uniqueid=salt)
	//////////////////////////////////////////////////////////////////////////

	// Entity timers.
	EntityTimersMap    m_timersMap;
	EntityTimersVector m_currentTimers;
	bool               m_bTimersPause;
	CTimeValue         m_nStartPause;

	// Binding entity.
	CScriptBind_Entity* m_pEntityScriptBinding;

	// Entity class registry.
	CEntityClassRegistry*  m_pClassRegistry;
	CPhysicsEventListener* m_pPhysicsEventListener;

	CAreaManager*          m_pAreaManager;

	CEntityLoadManager*    m_pEntityLoadManager;

	typedef std::unordered_map<EntityGUID, EntityId> EntityGuidMap;
	EntityGuidMap                    m_guidMap;
	EntityGuidMap                    m_genIdMap;

	IBreakableManager*               m_pBreakableManager;
	CEntityArchetypeManager*         m_pEntityArchetypeManager;
	CGeomCacheAttachmentManager*     m_pGeomCacheAttachmentManager;
	CCharacterBoneAttachmentManager* m_pCharacterBoneAttachmentManager;

	// Partition grid used by the entity system
	CPartitionGrid*          m_pPartitionGrid;
	CProximityTriggerSystem* m_pProximityTriggerSystem;

	EntityId                 m_idForced;

	//don't spawn any entities without being forced to
	bool m_bLocked;

	bool m_bLoadingLevel = false;

	bool m_bSupportLegacy64bitGuids = false;

	friend class CEntityItMap;
	class CCompareEntityIdsByClass;

	// helper to satisfy GCC
	static IEntityClass* GetClassForEntity(CEntity*);

	typedef std::map<string, CEntityLayer*>  TLayers;
	typedef std::vector<SEntityLayerGarbage> THeaps;

	TLayers m_layers;
	THeaps  m_garbageLayerHeaps;
	bool    m_bEntitiesUseGUIDs;
	int     m_nGeneratedFromGuid;

	std::unique_ptr<CEntitiesComponentPropertyCache> m_entitiesPropertyCache;

public:
	std::unique_ptr<class CEntityObjectDebugger> m_pEntityObjectDebugger;

#ifdef ENABLE_PROFILING_CODE
public:
	struct SLayerProfile
	{
		float         fTimeMS;
		float         fTimeOn;
		bool          isEnable;
		CEntityLayer* pLayer;
	};

	std::vector<SLayerProfile> m_layerProfiles;
#endif //ENABLE_PROFILING_CODE
};

//////////////////////////////////////////////////////////////////////////
// Precache resources mode state.
//////////////////////////////////////////////////////////////////////////
extern bool gPrecacheResourcesMode;

#endif // __EntitySystem_h__
