// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

namespace pfx2
{

namespace detail
{

ILINE floatv LoadIndexed4(const float* __restrict pStream, const uint32v index, floatv defaultVal)
{
	const mask32v4 mask = index == convert<uint32v>(gInvalidId);
	const uint32v mIndex = _mm_andnot_si128(mask, index);
	const uint32v mFirst = _mm_shuffle_epi32(mIndex, 0);

	if (All(index == mFirst + convert<uint32v>(0, 1, 2, 3)))
		return _mm_loadu_ps(pStream + get_element<0>(mFirst));

	floatv output = _mm_load1_ps(pStream + get_element<0>((mFirst)));
	if (!All(mFirst == mIndex))
	{
		const float f1 = *(pStream + get_element<1>(mIndex));
		const float f2 = *(pStream + get_element<2>(mIndex));
		const float f3 = *(pStream + get_element<3>(mIndex));
		const floatv m = vcast<floatv>(convert<uint32v>(~0, 0, 0, 0));
		const floatv v0 = convert<floatv>(0.0f, f1, f2, f3);
		output = _mm_or_ps(v0, _mm_and_ps(output, m));
	}

	return if_else(mask, defaultVal, output);
}

ILINE floatv LoadIndexed4(const float* __restrict pStream, const uint32v index, float defaultVal)
{
	return LoadIndexed4(pStream, index, to_v4(defaultVal));
}

}

//////////////////////////////////////////////////////////////////////////
// floatv

template<>
ILINE floatv TIStream<float, floatv >::Load(TParticleGroupId pgId) const
{
	return _mm_load_ps(m_pStream + pgId);
}

template<>
ILINE floatv TIStream<float, floatv >::Load(TParticleIdv pIdv) const
{
	if (!m_safeMask)
		return m_safeSink;
	return detail::LoadIndexed4(m_pStream, pIdv, m_safeSink);
}

template<>
ILINE floatv TIStream<float, floatv >::SafeLoad(TParticleGroupId pgId) const
{
	return _mm_load_ps(m_pStream + (pgId & m_safeMask));
}

template<>
ILINE void TIOStream<float, floatv >::Store(TParticleGroupId pgId, floatv value)
{
	_mm_store_ps(const_cast<float*>(m_pStream) + pgId, value);
}

//////////////////////////////////////////////////////////////////////////
// uint32

template<>
ILINE uint32v TIStream<uint32, uint32v >::Load(TParticleGroupId pgId) const
{
	return _mm_load_si128(reinterpret_cast<const __m128i*>(m_pStream + pgId));
}

template<>
ILINE uint32v TIStream<uint32, uint32v >::Load(TParticleIdv pIdv) const
{
	if (!m_safeMask)
		return m_safeSink;
	const float* streamF = reinterpret_cast<const float*>(m_pStream);
	auto dataF = detail::LoadIndexed4(streamF, pIdv, vcast<floatv>(m_safeSink));
	return vcast<uint32v>(dataF);
}

template<>
ILINE uint32v TIStream<uint32, uint32v >::SafeLoad(TParticleGroupId pgId) const
{
	return _mm_load_si128(reinterpret_cast<const __m128i*>(m_pStream + (pgId & m_safeMask)));
}

template<>
ILINE void TIOStream<uint32, uint32v >::Store(TParticleGroupId pgId, uint32v value)
{
	_mm_store_si128(reinterpret_cast<__m128i*>(const_cast<uint32*>(m_pStream) + pgId), value);
}

//////////////////////////////////////////////////////////////////////////
// UColv

template<>
ILINE UColv TIStream<UCol, UColv >::Load(TParticleGroupId pgId) const
{
	return _mm_load_si128(reinterpret_cast<const __m128i*>(m_pStream + pgId));
}

template<>
ILINE UColv TIStream<UCol, UColv >::Load(TParticleIdv pIdv) const
{
	if (!m_safeMask)
		return m_safeSink;
	const float* streamF = reinterpret_cast<const float*>(m_pStream);
	auto dataF = detail::LoadIndexed4(streamF, pIdv, vcast<floatv>(m_safeSink));
	return vcast<UColv>(dataF);
}

template<>
ILINE UColv TIStream<UCol, UColv >::SafeLoad(TParticleGroupId pgId) const
{
	return _mm_load_si128(reinterpret_cast<const __m128i*>(m_pStream + (pgId & m_safeMask)));
}

template<>
ILINE void TIOStream<UCol, UColv >::Store(TParticleGroupId pgId, UColv value)
{
	_mm_store_si128(reinterpret_cast<__m128i*>(const_cast<UCol*>(m_pStream) + pgId), value);
}

//////////////////////////////////////////////////////////////////////////
// Vec3v

ILINE Vec3v IVec3Stream::Load(TParticleGroupId pgId) const
{
	return Vec3v(
		_mm_load_ps(m_pXStream + pgId),
		_mm_load_ps(m_pYStream + pgId),
		_mm_load_ps(m_pZStream + pgId));
}

ILINE Vec3v IVec3Stream::Load(TParticleIdv pIdv) const
{
	if (!m_safeMask)
		return m_safeSink;
	return Vec3v(
		detail::LoadIndexed4(m_pXStream, pIdv, m_safeSink.x),
		detail::LoadIndexed4(m_pYStream, pIdv, m_safeSink.y),
		detail::LoadIndexed4(m_pZStream, pIdv, m_safeSink.z));
}

ILINE Vec3v IVec3Stream::SafeLoad(TParticleGroupId pgId) const
{
	return Vec3v(
		_mm_load_ps(m_pXStream + (pgId & m_safeMask)),
		_mm_load_ps(m_pYStream + (pgId & m_safeMask)),
		_mm_load_ps(m_pZStream + (pgId & m_safeMask)));
}

ILINE void IOVec3Stream::Store(TParticleGroupId pgId, const Vec3v& value) const
{
	_mm_store_ps(const_cast<float*>(m_pXStream) + pgId, value.x);
	_mm_store_ps(const_cast<float*>(m_pYStream) + pgId, value.y);
	_mm_store_ps(const_cast<float*>(m_pZStream) + pgId, value.z);
}

//////////////////////////////////////////////////////////////////////////
// Quatv

ILINE Quatv IQuatStream::Load(TParticleGroupId pgId) const
{
	return Quatv(
		_mm_load_ps(m_pWStream + pgId),
		_mm_load_ps(m_pXStream + pgId),
		_mm_load_ps(m_pYStream + pgId),
		_mm_load_ps(m_pZStream + pgId));
}

ILINE Quatv IQuatStream::Load(TParticleIdv pIdv) const
{
	if (!m_safeMask)
		return m_safeSink;
	return Quatv(
		detail::LoadIndexed4(m_pWStream, pIdv, m_safeSink.w),
		detail::LoadIndexed4(m_pXStream, pIdv, m_safeSink.v.x),
		detail::LoadIndexed4(m_pYStream, pIdv, m_safeSink.v.y),
		detail::LoadIndexed4(m_pZStream, pIdv, m_safeSink.v.z));
}

ILINE Quatv IQuatStream::SafeLoad(TParticleGroupId pgId) const
{
	return Quatv(
		_mm_load_ps(m_pWStream + (pgId & m_safeMask)),
		_mm_load_ps(m_pXStream + (pgId & m_safeMask)),
		_mm_load_ps(m_pYStream + (pgId & m_safeMask)),
		_mm_load_ps(m_pZStream + (pgId & m_safeMask)));
}

ILINE void IOQuatStream::Store(TParticleGroupId pgId, const Quatv& value) const
{
	_mm_store_ps(const_cast<float*>(m_pWStream) + pgId, value.w);
	_mm_store_ps(const_cast<float*>(m_pXStream) + pgId, value.v.x);
	_mm_store_ps(const_cast<float*>(m_pYStream) + pgId, value.v.y);
	_mm_store_ps(const_cast<float*>(m_pZStream) + pgId, value.v.z);
}

}
