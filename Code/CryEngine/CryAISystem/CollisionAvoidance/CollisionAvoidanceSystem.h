// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#ifndef __CollisionAvoidanceSystem_h__
#define __CollisionAvoidanceSystem_h__

#pragma once

class CollisionAvoidanceSystem
{
public:
	CollisionAvoidanceSystem();

	typedef size_t AgentID;
	typedef size_t ObstacleID;

	struct Agent
	{
		Agent()
			: radius(0.4f)
			, maxSpeed(4.5f)
			, maxAcceleration(0.1f)
			, currentLocation(ZERO)
			, currentLookDirection(ZERO)
			, currentVelocity(ZERO)
			, desiredVelocity(ZERO)
		{
		};

		Agent(float _radius, float _maxSpeed, float _maxAcceleration, const Vec2& _currentLocation, const Vec2& _currentVelocity,
		      const Vec2& _currentLookDirection, const Vec2& _desiredVelocity)
			: radius(_radius)
			, maxSpeed(_maxSpeed)
			, maxAcceleration(_maxAcceleration)
			, currentLocation(_currentLocation)
			, currentVelocity(_currentVelocity)
			, currentLookDirection(_currentLookDirection)
			, desiredVelocity(_desiredVelocity)
		{
		};

		float radius;
		float maxSpeed;
		float maxAcceleration;

		Vec3  currentLocation;
		Vec2  currentVelocity;
		Vec2  currentLookDirection;
		Vec2  desiredVelocity;
	};

	struct Obstacle
	{
		Obstacle()
			: radius(1.0f)
			, currentLocation(ZERO)
		{
		}

		float radius;

		Vec3  currentLocation;
		Vec2  currentVelocity;
	};

	AgentID         CreateAgent(tAIObjectID objectID);
	ObstacleID      CreateObstable();

	void            RemoveAgent(AgentID agentID);
	void            RemoveObstacle(ObstacleID obstacleID);

	void            SetAgent(AgentID agentID, const Agent& params);
	const Agent&    GetAgent(AgentID agentID) const;

	void            SetObstacle(ObstacleID obstacleID, const Obstacle& params);
	const Obstacle& GetObstacle(ObstacleID obstacleID) const;

	const Vec2&     GetAvoidanceVelocity(AgentID agentID);

	void            Reset(bool bUnload = false);
	void            Update(float updateTime);

	void            DebugDraw();
private:
	ILINE float     LeftOf(const Vec2& line, const Vec2& point) const
	{
		return line.Cross(point);
	}

	struct ConstraintLine
	{
		enum EOrigin
		{
			AgentConstraint    = 0,
			ObstacleConstraint = 1,
		};

		Vec2   direction;
		Vec2   point;

		uint16 flags;
		uint16 objectID; // debug only
	};

	struct NearbyAgent
	{
		NearbyAgent()
		{
		};

		NearbyAgent(float _distanceSq, uint16 _agentID, uint16 _flags = 0)
			: distanceSq(_distanceSq)
			, agentID(_agentID)
			, flags(_flags)
		{
		};

		enum EFlags
		{
			CanSeeMe = 1 << 0,
			IsMoving = 1 << 1,
		};

		bool operator<(const NearbyAgent& other) const
		{
			return distanceSq < other.distanceSq;
		};

		float  distanceSq;
		uint16 agentID;
		uint16 flags;
	};

	struct NearbyObstacle
	{
		NearbyObstacle()
			: distanceSq(0)
			, obstacleID(0)
			, flags(0)
		{
		};

		NearbyObstacle(float _distanceSq, uint16 _agentID, uint16 _flags = 0)
			: distanceSq(_distanceSq)
			, obstacleID(_agentID)
			, flags(_flags)
		{
		};

		enum EFlags
		{
			CanSeeMe = 1 << 0,
			IsMoving = 1 << 1,
		};

		ILINE bool operator<(const NearbyObstacle& other) const
		{
			return distanceSq < other.distanceSq;
		};

		float  distanceSq;
		uint16 obstacleID;
		uint16 flags;
	};

	enum { FeasibleAreaMaxVertexCount = 64, };

	typedef std::vector<NearbyAgent>    NearbyAgents;
	typedef std::vector<NearbyObstacle> NearbyObstacles;
	typedef std::vector<ConstraintLine> ConstraintLines;

	size_t ComputeNearbyAgents(const Agent& agent, size_t agentIndex, float range, NearbyAgents& nearbyAgents) const;
	size_t ComputeNearbyObstacles(const Agent& agent, size_t agentIndex, float range, NearbyObstacles& nearbyObstacles) const;

	size_t ComputeConstraintLinesForAgent(const Agent& agent, size_t agentIndex, float timeHorizonScale,
	                                      NearbyAgents& nearbyAgents, size_t maxAgentsConsidered, NearbyObstacles& nearbyObstacles, ConstraintLines& lines) const;
	void   ComputeAgentConstraintLine(const Agent& agent, const Agent& obstacleAgent, bool reciprocal, float timeHorizonScale,
	                                  ConstraintLine& line) const;
	void   ComputeObstacleConstraintLine(const Agent& agent, const Obstacle& obstacle, float timeHorizonScale,
	                                     ConstraintLine& line) const;

	bool   ClipPolygon(const Vec2* polygon, size_t vertexCount, const ConstraintLine& line, Vec2* output,
	                   size_t* outputVertexCount) const;
	size_t ComputeFeasibleArea(const ConstraintLine* lines, size_t lineCount, float radius, Vec2* feasibleArea) const;
	bool   ClipVelocityByFeasibleArea(const Vec2& velocity, Vec2* feasibleArea, size_t vertexCount, Vec2& output) const;

	struct CandidateVelocity
	{
		float distanceSq;
		Vec2  velocity;

		ILINE bool operator<(const CandidateVelocity& other) const
		{
			return distanceSq < other.distanceSq;
		}
	};

	size_t ComputeOptimalAvoidanceVelocity(Vec2* feasibleArea, size_t vertexCount, const Agent& agent,
	                                       const float minSpeed, const float maxSpeed, CandidateVelocity* output) const;

	Vec2 ClampSpeedWithNavigationMesh(const NavigationAgentTypeID agentTypeID, const Vec3 agentPosition, const Vec2& currentVelocity, const Vec2& velocityToClamp) const;
	bool FindFirstWalkableVelocity(AgentID agentID, CandidateVelocity* candidates, size_t candidateCount,
	                               Vec2& output) const;

	bool FindLineCandidate(const ConstraintLine* lines, size_t lineCount, size_t lineNumber, float radius, const Vec2& velocity,
	                       Vec2& candidate) const;
	bool FindCandidate(const ConstraintLine* lines, size_t lineCount, float radius, const Vec2& velocity, Vec2& candidate) const;

	void DebugDrawConstraintLine(const Vec3& agentLocation, const ConstraintLine& line, const ColorB& color);

	typedef std::vector<Agent> Agents;
	Agents m_agents;

	typedef std::vector<Vec2> AgentAvoidanceVelocities;
	AgentAvoidanceVelocities m_agentAvoidanceVelocities;

	typedef std::vector<Obstacle> Obstacles;
	Obstacles       m_obstacles;

	NearbyAgents    m_nearbyAgents;
	NearbyObstacles m_nearbyObstacles;
	ConstraintLines m_constraintLines;

	typedef std::vector<tAIObjectID> AgentObjectIDs;
	AgentObjectIDs m_agentObjectIDs;

	typedef std::vector<string> AgentNames;
	AgentNames m_agentNames;
};

#endif //__CollisionAvoidanceSystem_h__
