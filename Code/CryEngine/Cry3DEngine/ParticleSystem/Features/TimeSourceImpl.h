// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

namespace pfx2
{

namespace detail
{




ILINE const CParticleContainer& GetContainer(const SUpdateContext context, EModDomain domain)
{
	return domain == EMD_PerParticle ? context.m_container : context.m_parentContainer;
}

class CSelfStreamSampler
{
public:
	CSelfStreamSampler(const SUpdateContext& context, EParticleDataType sourceStreamType, EModDomain domain = EMD_PerParticle)
		: sourceStream(GetContainer(context, domain).GetIFStream(sourceStreamType))
	{}
	ILINE floatv Sample(TParticleGroupId particleId) const
	{
		return sourceStream.Load(particleId);
	}
private:
	IFStream sourceStream;
};

class CParentStreamSampler
{
public:
	CParentStreamSampler(const SUpdateContext& context, EParticleDataType sourceStreamType)
		: parentSourceStream(context.m_parentContainer.GetIFStream(sourceStreamType, 1.0f))
		, parentIds(context.m_container.GetIPidStream(EPDT_ParentId))
	{}
	ILINE floatv Sample(TParticleGroupId particleId) const
	{
		const TParticleIdv parentId = parentIds.Load(particleId);
		return parentSourceStream.Load(parentId);
	}
private:
	IFStream   parentSourceStream;
	IPidStream parentIds;
};

class CParentAgeSampler
{
public:
	CParentAgeSampler(const SUpdateContext& context)
		: deltaTime(ToFloatv(context.m_deltaTime))
		, selfAges(context.m_container.GetIFStream(EPDT_NormalAge))
		, parentAges(context.m_parentContainer.GetIFStream(EPDT_NormalAge))
		, parentInvLifeTimes(context.m_parentContainer.GetIFStream(EPDT_InvLifeTime))
		, parentIds(context.m_container.GetIPidStream(EPDT_ParentId))
	{}
	ILINE floatv Sample(TParticleGroupId particleId) const
	{
		const TParticleIdv parentId = parentIds.Load(particleId);
		const floatv selfAge = selfAges.Load(particleId);
		const floatv parentAge = parentAges.Load(parentId);
		const floatv parentInvLifeTime = parentInvLifeTimes.Load(parentId);

		// anti-alias parent age
		const floatv tempAntAliasParentAge = MAdd(selfAge * parentInvLifeTime, deltaTime, parentAge);
		const floatv sample = __fsel(selfAge, parentAge, tempAntAliasParentAge);
		return sample;
	}
private:
	IFStream   selfAges;
	IFStream   parentAges;
	IFStream   parentInvLifeTimes;
	IPidStream parentIds;
	floatv     deltaTime;
};

class CLevelTimeSampler
{
public:
	CLevelTimeSampler(const SUpdateContext& context)
		: deltaTime(ToFloatv(context.m_deltaTime))
		, selfAges(context.m_container.GetIFStream(EPDT_NormalAge))
		, levelTime(ToFloatv(context.m_time))
	{}
	ILINE floatv Sample(TParticleGroupId particleId) const
	{
		const floatv selfAge = selfAges.Load(particleId);
		return StartTime(levelTime, deltaTime, selfAge);
	}
private:
	IFStream selfAges;
	floatv   levelTime;
	floatv   deltaTime;
};

class CSelfSpeedSampler
{
public:
	CSelfSpeedSampler(const SUpdateContext& context, EModDomain domain = EMD_PerParticle)
		: velocities(GetContainer(context, domain).GetIVec3Stream(EPVF_Velocity))
	{}
	ILINE floatv Sample(TParticleGroupId particleId) const
	{
		const Vec3v velocity = velocities.Load(particleId);
		return velocity.GetLength();
	}
private:
	IVec3Stream velocities;
};

class CParentSpeedSampler
{
public:
	CParentSpeedSampler(const SUpdateContext& context)
		: parentVelocities(context.m_parentContainer.GetIVec3Stream(EPVF_Velocity))
		, parentIds(context.m_container.GetIPidStream(EPDT_ParentId))
	{}
	ILINE floatv Sample(TParticleGroupId particleId) const
	{
		const uint32v parentId = parentIds.Load(particleId);
		const Vec3v velocity = parentVelocities.Load(parentId);
		const floatv speed = velocity.GetLength();
		const floatv sample = if_else_zero(parentId != ToUint32v(gInvalidId), speed);
		return sample;
	}
private:
	IVec3Stream parentVelocities;
	IPidStream  parentIds;
};

class CAttributeSampler
{
public:
	CAttributeSampler(const SUpdateContext& context, const string& m_attributeName)
	{
		const CAttributeInstance& attributes = context.m_runtime.GetEmitter()->GetAttributeInstance();
		auto attributeId = attributes.FindAttributeIdByName(m_attributeName.c_str());
		m_attributeValue = ToFloatv(attributes.GetAsFloat(attributeId, 1.0f));
	}
	ILINE floatv Sample(TParticleGroupId particleId) const
	{
		return m_attributeValue;
	}
private:
	floatv m_attributeValue;
};

class CChaosSampler
{
public:
	CChaosSampler(const SUpdateContext& context)
		: m_chaos(context.m_spawnRngv) {}
	ILINE floatv Sample(TParticleGroupId particleId) const
	{
		return m_chaos.RandUNorm();
	}
private:
	SChaosKeyV& m_chaos;
};

class CViewAngleSampler
{
public:
	CViewAngleSampler(const SUpdateContext& context, EModDomain domain = EMD_PerParticle)
		: m_positions(GetContainer(context, domain).GetIVec3Stream(EPVF_Position))
		, m_orientations(GetContainer(context, domain).GetIQuatStream(EPQF_Orientation))
		, m_cameraPosition(ToVec3v(gEnv->p3DEngine->GetRenderingCamera().GetPosition())) {}
	ILINE floatv Sample(TParticleGroupId particleId) const
	{
		const Vec3v position = m_positions.Load(particleId);
		const Quatv orientation = m_orientations.Load(particleId);
		const Vec3v normal = GetColumn2(orientation);
		const Vec3v toCamera = GetNormalized(m_cameraPosition - position);
		const floatv cosAngle = fabs_tpl(toCamera.Dot(normal));
		return cosAngle;
	}
private:
	Vec3v GetColumn2(Quatv q) const 
	{
		const floatv two = ToFloatv(2.0f);
		const floatv one = ToFloatv(1.0f);
		return Vec3v(
			two * (q.v.x * q.v.z + q.v.y * q.w),
			two * (q.v.y * q.v.z - q.v.x * q.w),
			two * (q.v.z * q.v.z + q.w * q.w) - one);
	}
	Vec3v GetNormalized(Vec3v v) const
	{
		#ifdef CRY_PFX2_USE_SSE
		return v * _mm_rsqrt_ps(v.Dot(v));
		#else
		return v.GetNormalized();
		#endif
	}
	IVec3Stream m_positions;
	IQuatStream m_orientations;
	Vec3v       m_cameraPosition;
};

class CCameraDistanceSampler
{
public:
	CCameraDistanceSampler(const SUpdateContext& context, EModDomain domain = EMD_PerParticle)
		: m_positions(GetContainer(context, domain).GetIVec3Stream(EPVF_Position))
		, m_cameraPosition(ToVec3v(gEnv->p3DEngine->GetRenderingCamera().GetPosition())) {}
	ILINE floatv Sample(TParticleGroupId particleId) const
	{
		const Vec3v position = m_positions.Load(particleId);
		const floatv distance = position.GetDistance(m_cameraPosition);
		return distance;
	}
private:
	IVec3Stream m_positions;
	Vec3v       m_cameraPosition;
};

}

ILINE CTimeSource::CTimeSource()
	: m_timeSource(ETimeSource::Age)
	, m_fieldSource(ETimeSourceField(EPDT_LifeTime))
	, m_sourceOwner(ETimeSourceOwner::Self)
	, m_timeScale(1.0f)
	, m_timeBias(0.0f)
	, m_spawnOnly(true)
{
}

template<typename TParam, typename TMod>
ILINE void CTimeSource::AddToParam(CParticleComponent* pComponent, TParam* pParam, TMod* pModifier)
{
	if (m_spawnOnly)
		pParam->AddToInitParticles(pModifier);
	else
		pParam->AddToUpdate(pModifier);

	CParticleComponent* pSourceComponent = m_sourceOwner == ETimeSourceOwner::Parent ? pComponent->GetParentComponent() : pComponent;
	if (pSourceComponent)
	{
		if (m_timeSource == ETimeSource::SpawnFraction)
			pSourceComponent->AddParticleData(EPDT_SpawnFraction);
		else if (m_timeSource == ETimeSource::Speed)
			pSourceComponent->AddParticleData(EPVF_Velocity);
		else if (m_timeSource == ETimeSource::Field)
			pSourceComponent->AddParticleData((EParticleDataType)m_fieldSource);
		else if (m_timeSource == ETimeSource::ViewAngle)
			pSourceComponent->AddParticleData(EPQF_Orientation);
	}
}

ILINE EModDomain CTimeSource::GetDomain() const
{
	switch (m_sourceOwner)
	{
	case ETimeSourceOwner::Self:
		return EMD_PerParticle;
	case ETimeSourceOwner::Parent:
		return EMD_PerInstance;
	default:
		return EMD_PerEffect;
	}
}
ILINE EParticleDataType CTimeSource::GetDataType() const
{
	switch (m_timeSource)
	{
	case ETimeSource::Age:
		return EPDT_NormalAge;
	case ETimeSource::SpawnFraction:
		return EPDT_SpawnFraction;
	case ETimeSource::Field:
		return EParticleDataType(m_fieldSource);
	default:
		return EParticleDataType::size();
	}
}

template<typename TBase, typename TStream>
ILINE void CTimeSource::Dispatch(const SUpdateContext& context, const SUpdateRange& range, TStream stream, EModDomain domain) const
{
	switch (m_timeSource)
	{
	case ETimeSource::LevelTime:
		((TBase*)this)->DoModify(
		  context, range, stream,
		  detail::CLevelTimeSampler(context));
		break;
	case ETimeSource::Attribute:
		((TBase*)this)->DoModify(
		  context, range, stream,
		  detail::CAttributeSampler(context, m_attributeName));
		break;
	case ETimeSource::Random:
		((TBase*)this)->DoModify(
		  context, range, stream,
		  detail::CChaosSampler(context));
		break;
	case ETimeSource::ViewAngle:
		((TBase*)this)->DoModify(
			context, range, stream,
			detail::CViewAngleSampler(context, domain));
		break;
	case ETimeSource::CameraDistance:
		((TBase*)this)->DoModify(
			context, range, stream,
			detail::CCameraDistanceSampler(context, domain));
		break;
	case ETimeSource::Speed:
		if (m_sourceOwner == ETimeSourceOwner::Self || domain == EMD_PerInstance)
			((TBase*)this)->DoModify(
			  context, range, stream,
			  detail::CSelfSpeedSampler(context, domain));
		else
			((TBase*)this)->DoModify(
			  context, range, stream,
			  detail::CParentSpeedSampler(context));
		break;
	default:
		if (m_sourceOwner == ETimeSourceOwner::Self || domain == EMD_PerInstance)
			((TBase*)this)->DoModify(
				context, range, stream,
				detail::CSelfStreamSampler(context, GetDataType(), domain));
		else if (GetDataType() == EPDT_NormalAge)
			((TBase*)this)->DoModify(
				context, range, stream,
				detail::CParentAgeSampler(context));
		else
			((TBase*)this)->DoModify(
				context, range, stream,
				detail::CParentStreamSampler(context, GetDataType()));
	}
}

ILINE IParamModContext& CTimeSource::GetContext(Serialization::IArchive& ar) const
{
	IParamModContext* pContext = ar.context<IParamModContext>();
	CRY_PFX2_ASSERT(pContext != nullptr);
	return *pContext;
}

}
