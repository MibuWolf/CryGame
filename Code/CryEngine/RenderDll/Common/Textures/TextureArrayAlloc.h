// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#ifndef TEXTUREARRAYALLOC_H
#define TEXTUREARRAYALLOC_H

template<bool LT256, bool LT65536>
struct CTextureArrayAllocHlp
{
	typedef size_t T;
};
template<>
struct CTextureArrayAllocHlp<true, true>
{
	typedef uint8 T;
};
template<>
struct CTextureArrayAllocHlp<false, true>
{
	typedef uint16 T;
};

template<typename T, size_t Capacity>
class CTextureArrayAlloc
{
public:
	typedef typename CTextureArrayAllocHlp < Capacity < 256, Capacity<65536>::T TId;

public:
	// Note: m_arr is uninitialized for performance reasons
	// cppcheck-suppress uninitMemberVar
	CTextureArrayAlloc()
	{
		for (TId i = 0; i < Capacity; ++i)
			m_freeIDs[i] = i;
		m_numFree = Capacity;
		std::make_heap(m_freeIDs, m_freeIDs + Capacity, HeapComp());
	}

	T* Allocate()
	{
		if (m_numFree > 0)
		{
			TId idx = m_freeIDs[0];
			std::pop_heap(m_freeIDs, m_freeIDs + m_numFree, HeapComp());
			--m_numFree;
			return &m_arr[idx];
		}
		return NULL;
	}

	void Release(T* p)
	{
		TId idx = static_cast<TId>(std::distance(m_arr, p));
		m_freeIDs[m_numFree++] = idx;
		std::push_heap(m_freeIDs, m_freeIDs + m_numFree, HeapComp());
	}

	T*       GetArray()             { return m_arr; }
	const T* GetArray() const       { return m_arr; }

	T*       GetPtrFromIdx(TId idx) { return &m_arr[idx]; }
	TId      GetIdxFromPtr(T* p)    { return static_cast<TId>(p - &m_arr[0]); }

	TId      GetNumLive() const     { return Capacity - m_numFree; }
	TId      GetNumFree() const     { return m_numFree; }

private:
	struct HeapComp
	{
		bool operator()(TId a, TId b) const
		{
			return b < a;
		}
	};

private:
	T   m_arr[Capacity];
	TId m_freeIDs[Capacity];
	TId m_numFree;
};

#endif
