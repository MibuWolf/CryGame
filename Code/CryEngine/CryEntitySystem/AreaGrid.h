// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#pragma once

class CArea;
class CAreaGrid;

typedef std::vector<CArea*>   TAreaPointers;
typedef std::vector<EntityId> TEntityIDs;

class CAreaGrid
{
public:

	CAreaGrid();
	~CAreaGrid();

	bool   ResetArea(CArea* pArea);
	void   Compile(TAreaPointers const& areas);
	void   Reset();
	bool   GetAreas(Vec3 const& position, TAreaPointers& outAreas);
	void   Draw();

	size_t GetNumAreas() const { return (m_pAreas != nullptr) ? m_pAreas->size() : 0; }

private:

	bool GetAreas(uint32 const x, uint32 const y, TAreaPointers& outAreas);
	bool GetAreaIndex(CArea const* const pArea, size_t& outIndex);
	void AddAreaBit(const Vec2i& start, const Vec2i& end, uint32 areaIndex);
	void RemoveAreaBit(uint32 areaIndex);
	void AddArea(CArea* pArea, uint32 areaIndex);
	void ClearAllBits();

#if defined(INCLUDE_ENTITYSYSTEM_PRODUCTION_CODE)
	void Debug_CheckBB(Vec2 const& vBBCentre, Vec2 const& vBBExtent, CArea const* const pArea);
#endif // INCLUDE_ENTITYSYSTEM_PRODUCTION_CODE

	uint32  m_numCellsPerAxis;
	uint32* m_pbitFieldX;                  // start of the X bit field
	uint32* m_pbitFieldY;                  // start of the Y bit field
	uint32  m_bitFieldSizeU32;             // number of u32s per cell
	uint32  m_maxNumAreas;                 // maximum number of areas compiled into the grid
	Vec2i(*m_pAreaBounds)[2];              // Points to area bounds in the bit field array

	// Points to area pointers array in AreaManager.
	TAreaPointers const* m_pAreas;
};
