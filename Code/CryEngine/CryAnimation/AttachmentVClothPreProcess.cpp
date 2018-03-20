// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#include "stdafx.h"
#include <CryMath/Cry_Math.h>
#include <CryPhysics/primitives.h>
#include "../CryPhysics/quotient.h"
#include "AttachmentVClothPreProcess.h"

// boost is needed here for path finding algorithm over mesh (i.e., dijkstra)
#include <iostream>
#include <fstream>
#include "CryCore/BoostHelpers.h"

// suppress warning from boost on MSVC: warning C4172: returning address of local variable or temporary
// warning suppression has to go outside of the function to take effect.
#ifdef CRY_PLATFORM_WINDOWS
	#pragma warning(push)
	#pragma warning(disable: 4172)
#endif
#ifdef CRY_PLATFORM_DURANGO
	#pragma warning(push)
	#pragma warning(disable: 4172)
#endif

#include <boost/config.hpp>
#include <boost/graph/graph_traits.hpp>
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !! the following fixes compiling problems with boost on orbis (i.e. include path problems with <boost/predef/other/endian.h>) :
// !! orbis defines __FreeBSD__, but boost needs __Open_BSD__ defined to include <boost/predef/other/endian.h> within "adjacency_list.hpp" properly
// !! might/SHOULD be removed if boost is updated and has fixed that problem
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

#if CRY_PLATFORM_ORBIS
	#ifdef __FreeBSD__
		#undef __FreeBSD__
		#define __OpenBSD__
		#define RESET__FreeBSD__
	#endif
	#include <boost/predef/other/endian.h>
	#ifdef RESET__FreeBSD__
		#define __FreeBSD__
		#undef __OpenBSD__
		#undef RESET__FreeBSD__
		#undef BOOST_OS_BSD_OPEN
		#undef BOOST_OS_BSD
		#undef __OPEN_BSD__
	#endif
#endif
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>

#ifdef CRY_PLATFORM_WINDOWS
	#pragma warning(pop)
#endif
#ifdef CRY_PLATFORM_DURANGO
	#pragma warning(pop)
#endif
///////////////////////////////////////////////////////////////////////////////

bool AttachmentVClothPreProcess::PreProcess(std::vector<Vec3> const& vtx, std::vector<int> const& idx, std::vector<bool> const& attached)
{
	bool valid = true;

	// init data structures
	m_lra.resize(vtx.size());

	// preprocess
	LraDijkstra(vtx, idx, attached);
	BendByTriangleAngle(vtx, idx, attached);
	CreateLinks(vtx, idx);

	//////////////////////////////////////////////////////////////////////////

	// Debug output:
	//CryWarning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, "[Cloth] No of Stretch Links %i", m_linksStretch.size());
	//CryWarning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, "[Cloth] No of Shear Links %i", m_linksShear.size());
	//CryWarning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, "[Cloth] No of bend Links %i", m_linksBend.size());

	return valid;
}

bool AttachmentVClothPreProcess::LraDijkstra(std::vector<Vec3> const& vtx, std::vector<int> const& idx, std::vector<bool> const& attached)
{
	using namespace boost;

	typedef adjacency_list<listS, vecS, undirectedS,
	                       boost::property<boost::vertex_index_t, int>, property<edge_weight_t, float>> graph_t;
	typedef graph_traits<graph_t>::vertex_descriptor vertex_descriptor;
	typedef std::pair<int, int>                      Edge;

	int nVtx = (int)vtx.size();
	int nTriangles = (int)idx.size() / 3;
	int nEdges = (int)idx.size();

	// generate data structure for Dijkstra

	// add edges of all triangles
	std::vector<Edge> edgeArray;
	edgeArray.reserve(nEdges);
	const int* tri = &idx[0];
	for (int i = 0; i < nTriangles; i++)
	{
		int idx = i * 3;
		edgeArray.push_back(Edge(tri[idx], tri[idx + 1]));
		edgeArray.push_back(Edge(tri[idx + 1], tri[idx + 2]));
		edgeArray.push_back(Edge(tri[idx], tri[idx + 2]));
	}

	// fill weights with length of edges
	std::vector<float> weights;
	weights.reserve(edgeArray.size());
	for (auto it = edgeArray.begin(); it != edgeArray.end(); ++it)
	{
		const float distance = (vtx[(*it).first] - vtx[(*it).second]).GetLengthFast();
		weights.push_back(distance);
	}

	// add one special node with weight 0, which is connected to all attached vtx
	// this is later on used as source, thus, multiple sources can be actually handled, which is not possible directly with boost::dijkstra
	for (int i = 0; i < nVtx; i++)
	{
		if (attached[i])
		{
			edgeArray.push_back(Edge(nVtx, i));
			weights.push_back(0.0f);
		}
	}
	// update sizes
	nVtx += 1;
	nEdges = (int)edgeArray.size();

	// init graph
	{
		graph_t g(&edgeArray[0], &edgeArray[0] + edgeArray.size(), &weights[0], nVtx);

		// acquire containers for dijkstra-results, i.e. direct-neighbor-parent and distance
		property_map<graph_t, edge_weight_t>::type weightmap = get(edge_weight, g);
		std::vector<vertex_descriptor> p(num_vertices(g)); // will take each vtx next parent
		std::vector<float> d(num_vertices(g));             // will take distance to source
		int source = nVtx - 1;
		vertex_descriptor s = vertex(source, g);

		// search shortest paths
		dijkstra_shortest_paths(g, s,
		                        predecessor_map(boost::make_iterator_property_map(p.begin(), get(boost::vertex_index, g))).
		                        distance_map(boost::make_iterator_property_map(d.begin(), get(boost::vertex_index, g))));

		// store best source & distance to that one
		{
			graph_traits<graph_t>::vertex_iterator vi, vend;
			for (std::tie(vi, vend) = vertices(g); vi != vend; ++vi)
			{
				const int idx = *vi;

				bool _continue = false;
				if (idx < 0) _continue = true;
				if (idx >= d.size()) _continue = true;
				if (idx >= p.size()) _continue = true;
				if (idx >= *vend) _continue = true;
				if (idx >= m_lra.size()) _continue = true; // this occurs, since size of m_lra is one less than the others

				if (_continue)
				{
					// CryWarning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_WARNING, "[Cloth] LraDijkstra::CONTINUE due to idx out of range:: %i   # %i,%i,%i,%i[d.size,p.size,*vend,m_lra.size()]", idx, d.size(), p.size(), *vend, m_lra.size());
					continue;
				}

				assert(idx < *vend);
				assert(idx < d.size());
				const float distance = d[idx];
				const int nextParent = p[idx];

				m_lra[idx].lraNextParent = -1;
				if (idx == source) continue;     // this is the source node, not existing in m_particlesHot, since added for graph traversing reasons, thus, do nothing
				if (idx == nextParent) continue; // this is the source node or something went wrong, do nothing
				m_lra[idx].lraDist = 0;          // default: no lra, i.e. no path existing / disconnected parts
				m_lra[idx].lraIdx = -1;          // default: no lra, i.e. no path existing / disconnected parts
				m_lra[idx].lraNextParent = nextParent;
				if (distance < 0.001f) continue;         // this is an attached vtx, thus, no lra
				if (distance > FLT_MAX * 0.5f) continue; // no path existing, disconnected mesh, thus, no lra

				// if we are here a path has been found
				m_lra[idx].lraDist = distance;
				m_lra[idx].lraNextParent = nextParent;
				m_lra[idx].lraIdx = 0; // this enables later the search for the closest attached constraint
			}
		}

		// all paths are set, thus, search for each particle the closest attached vtx by walking the whole path until next parent equals 'source'
		nVtx = vtx.size();
		for (int idx = 0; idx < nVtx; idx++)
		{
			if (attached[idx]) continue;
			if (m_lra[idx].lraIdx == -1) continue; // no path available, (-1 is default value, see above)
			int actualNode = idx;
			while (m_lra[actualNode].lraNextParent != source)
			{
				int nextParentNode = m_lra[actualNode].lraNextParent;
				assert(nextParentNode >= 0);
				assert(nextParentNode < nVtx); // +1 // +1 due to source
				actualNode = nextParentNode;
			}
			m_lra[idx].lraIdx = actualNode;
		}

		// free by hand to let boost be faster
		g.m_edges.clear();
		g.m_vertices.clear();
		g.m_edges.resize(0);
		g.m_vertices.resize(0);
		g.clear();
		stl::free_container(p);
		stl::free_container(d);
	}

	// extension: sort by distance
	{
		// 1. sort not attached particles by distance to attachment
		struct MyClassSort
		{
			int   idx;
			float dist;
			MyClassSort(int _idx, float _dist) : idx(_idx), dist(_dist)
			{
			}
		};
		std::vector<MyClassSort> myClassSortList;
		myClassSortList.reserve(nVtx);
		for (int i = 0; i < nVtx; i++)
		{
			if (attached[i]) continue;
			myClassSortList.push_back(MyClassSort(i, m_lra[i].lraDist));
		}
		std::sort(myClassSortList.begin(), myClassSortList.end(), [](const MyClassSort& a, const MyClassSort& b) { return a.dist < b.dist; });
		// copy list of indices to m_lraNotAttachedOrderedIdx
		m_lraNotAttachedOrderedIdx.clear();
		m_lraNotAttachedOrderedIdx.reserve(myClassSortList.size());
		for (auto it = myClassSortList.begin(); it != myClassSortList.end(); ++it)
		{
			m_lraNotAttachedOrderedIdx.push_back((*it).idx);
		}

		stl::free_container(myClassSortList); // assist VC memory management
	}

	stl::free_container(edgeArray); // assist VC memory management
	stl::free_container(weights);   // assist VC memory management

	return true;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

namespace VClothPreProcessUtils
{
static inline void BendEdgeEnsureOrder(std::pair<int, int>& e)
{
	if (e.first > e.second) std::swap(e.first, e.second);
}

static inline int AddToMapTriangleIdxToListBendTrianglesIdx(
  std::vector<SBendTriangle>& listBendTriangles,
  std::unordered_map<int, int>& mapTriangleIdxToListBendTrianglesIdx,
  const int* meshIdx,
  const Vec3* meshVtx,
  int idxTriangle
  )
{
	if (mapTriangleIdxToListBendTrianglesIdx.find(idxTriangle) != mapTriangleIdxToListBendTrianglesIdx.end())
	{
		return mapTriangleIdxToListBendTrianglesIdx[idxTriangle];     // already existing
	}

	int idxP0 = meshIdx[idxTriangle * 3 + 0];
	int idxP1 = meshIdx[idxTriangle * 3 + 1];
	int idxP2 = meshIdx[idxTriangle * 3 + 2];
	const Vec3& p0 = meshVtx[idxP0];
	const Vec3& p1 = meshVtx[idxP1];
	const Vec3& p2 = meshVtx[idxP2];
	// check for area, if valid triangle
	float s = 100.0f;                                                                          // fight floating point precision for cross-product & length
	if (((p1 * s - p0 * s).cross(p2 * s - p0 * s)).GetLengthSquared() < 0.0001f) return -1;    // degenerated triangle

	// else: add to map
	mapTriangleIdxToListBendTrianglesIdx[idxTriangle] = (int)listBendTriangles.size();
	SBendTriangle bendTriangle(idxP0, idxP1, idxP2);
	listBendTriangles.push_back(bendTriangle);
	return mapTriangleIdxToListBendTrianglesIdx[idxTriangle];
}

static inline void AddBendTrianglePair(
  std::vector<SBendTrianglePair>& listBendTrianglePair,
  const int* meshIdx,
  int idxTriangle0,
  int idxTriangle1,
  int idxListBendTriangles0,
  int idxListBendTriangles1
  )
{
	const int* t0 = &meshIdx[idxTriangle0 * 3];
	const int* t1 = &meshIdx[idxTriangle1 * 3];

	// find same edge
	bool found;
	int edge0Idx = 0;
	int edge1Idx = 0;
	found = false;
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{

			if (!found)
			{
				edge0Idx = i;
				found = (t0[i] == t1[j]);
			}
		}
	}
	assert(found);
	found = false;
	for (int i = 2; i >= 0; i--)
		for (int j = 0; j < 3; j++)
			if (!found)
			{
				edge1Idx = i;
				found = (t0[i] == t1[j]);
			}
	assert(found);

	// find vtx of each triangle, not belonging to edge
	int idxP2 = edge0Idx == 1 ? 0 : (edge1Idx == 1 ? 2 : 1);
	int idxP3 = 0;
	found = false;
	for (int i = 0; i < 3; i++)
	{
		if ((t1[i] != t0[edge0Idx]) && (t1[i] != t0[edge1Idx]))
		{
			found = true;
			idxP3 = i;
		}
	}
	assert(found);

	int idxNormal0 = idxListBendTriangles0;
	int idxNormal1 = idxListBendTriangles1;

	SBendTrianglePair bendTrianglePair(t0[edge0Idx], t0[edge1Idx], t0[idxP2], t1[idxP3], idxNormal0, idxNormal1);
	listBendTrianglePair.push_back(bendTrianglePair);
}

static inline float BendDetermineAngle(const Vec3& n0, const Vec3& n1, const Vec3 pRef0, const Vec3& pRef3)
{
	float nDotN = n0.dot(n1);
	if (nDotN > 1.0f) nDotN = 1.0f;
	if (nDotN < -1.0f) nDotN = -1.0f;
	float alpha = acos(nDotN);
	float sign = n0.dot(pRef3 - pRef0) > 0.0f ? -1.0f : 1.0f;
	alpha *= sign;
	return alpha;
}
}

bool AttachmentVClothPreProcess::BendByTriangleAngle(std::vector<Vec3> const& vtx, std::vector<int> const& idx, std::vector<bool> const& attached)
{
	typedef std::pair<int, int> BendEdge;
	std::unordered_set<BendEdge, boost::hash<std::pair<int, int>>> edgeDone;
	std::unordered_map<int, int> mapTriangleIdxToListBendTrianglesIdx;
	m_listBendTrianglePairs.clear();
	m_listBendTriangles.clear();

	int nTriangles = (int)idx.size() / 3;
	int nEdges = (int)idx.size();
	const int* meshIdx = &idx[0];
	const Vec3* meshVtx = &vtx[0];

	if (nTriangles <= 1) return false;

	// debug
	//int nEdgesWithNeighbor = 0;
	//int nEdgesFromDegeneratedTriangle = 0;
	//int nTriCandidatesGreater1=0;
	//int nTriHasNoNeighbor = 0;

	// loop over all edges of all triangles
	for (int i = 0; i < nTriangles - 1; i++)
	{
		for (int e0 = 0; e0 < 3; e0++) // loop over edges
		{
			int idx0 = meshIdx[i * 3 + e0];
			int idx1 = meshIdx[i * 3 + (e0 + 1) % 3];
			BendEdge edge0(idx0, idx1);
			VClothPreProcessUtils::BendEdgeEnsureOrder(edge0);

			if (edgeDone.find(edge0) != edgeDone.end()) continue; // already handled this edge

			// loop over all edges of all following triangles and compare edges
			int triCandidate = -1;
			int triCandidateNo = 0;
			for (int j = i + 1; j < nTriangles; j++)
			{
				for (int e1 = 0; e1 < 3; e1++)
				{
					int idx0 = meshIdx[j * 3 + e1];
					int idx1 = meshIdx[j * 3 + (e1 + 1) % 3];
					BendEdge edge1(idx0, idx1);
					VClothPreProcessUtils::BendEdgeEnsureOrder(edge1);
					if (edge0 == edge1)
					{
						triCandidate = j;
						triCandidateNo++;
					}
				}
			}

			//if (triCandidateNo > 1) nTriCandidatesGreater1++; // debug
			//if (triCandidate==-1) nTriHasNoNeighbor++; // debug

			if ((triCandidateNo == 1) && triCandidate >= 0) // found neighbor, since only two triangles are sharing this edge
			{
				int idxListBendTriangles0 = VClothPreProcessUtils::AddToMapTriangleIdxToListBendTrianglesIdx(m_listBendTriangles, mapTriangleIdxToListBendTrianglesIdx, meshIdx, meshVtx, i);
				int idxListBendTriangles1 = VClothPreProcessUtils::AddToMapTriangleIdxToListBendTrianglesIdx(m_listBendTriangles, mapTriangleIdxToListBendTrianglesIdx, meshIdx, meshVtx, triCandidate);

				bool isDegeneratedTriangle = (idxListBendTriangles0 < 0) || (idxListBendTriangles1 < 0);
				if (!isDegeneratedTriangle)
				{
					VClothPreProcessUtils::AddBendTrianglePair(m_listBendTrianglePairs, meshIdx, i, triCandidate, idxListBendTriangles0, idxListBendTriangles1);
					//nEdgesWithNeighbor++; // debug
				}
				//else { // debug
				//	nEdgesFromDegeneratedTriangle++;
				// }
			}

			edgeDone.insert(edge0);
		}
	}
	//CryWarning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_ERROR, "[Cloth] BendByTriangleAngleInit() BendEdges: Found %i edges with neighbors, out of %i edges total. Edge Done: %i", nEdgesWithNeighbor, nEdges,(int)edgeDone.size());
	//CryWarning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_ERROR, "[Cloth] BendByTriangleAngleInit() Degenerated: %i", nEdgesFromDegeneratedTriangle);
	//CryWarning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_ERROR, "[Cloth] BendByTriangleAngleInit() nTriCandidatesGreater1: %i", nTriCandidatesGreater1);
	//CryWarning(VALIDATOR_MODULE_ANIMATION, VALIDATOR_ERROR, "[Cloth] BendByTriangleAngleInit() nTriHasNoNeighbor: %i", nTriHasNoNeighbor);

	// determine phi0
	// 1st: determine all normals
	DetermineTriangleNormals(vtx, m_listBendTriangles);

	// 2nd: determine angle
	for (auto it = m_listBendTrianglePairs.begin(); it != m_listBendTrianglePairs.end(); it++)
	{
		Vec3& n0 = m_listBendTriangles[(*it).idxNormal0].normal;
		Vec3& n1 = m_listBendTriangles[(*it).idxNormal1].normal;

		it->phi0 = VClothPreProcessUtils::BendDetermineAngle(n0, n1, vtx[it->p3], vtx[it->p0]);
	}

	return true;
}

bool AttachmentVClothPreProcess::DetermineTriangleNormals(std::vector<Vec3> const& vtx, std::vector<SBendTriangle>& tri)
{
	for (auto it = tri.begin(); it != tri.end(); it++)
	{
		it->normal = ((vtx[it->p1] - vtx[it->p0]).cross(vtx[it->p2] - vtx[it->p0])).GetNormalizedFast();
	}
	return true;
}

bool AttachmentVClothPreProcess::DetermineTriangleNormals(int nVertices, strided_pointer<Vec3> const& pVertices, std::vector<SBendTriangle>& tri)
{
	for (auto it = tri.begin(); it != tri.end(); it++)
	{
		it->normal = ((pVertices[it->p1] - pVertices[it->p0]).cross(pVertices[it->p2] - pVertices[it->p0])).GetNormalizedFast();
	}
	return true;
}

bool AttachmentVClothPreProcess::PruneWeldedVertices(std::vector<Vec3>& vtx, std::vector<int>& idx, std::vector<bool>& attached)
{

	// look for welded vertices and prune them
	int nWelded = 0;
	std::vector<Vec3> unweldedVerts;
	std::vector<bool> unweldedAttached;
	std::vector<int> weldMap;

	// TODO: faster welded vertices detector - this is O(n^2) - see CharacterManager::LoadVClothGeometry
	for (int i = 0; i < vtx.size(); i++)
	{
		int iMap = -1;
		for (int j = i - 1; j >= 0; j--)
		{
			Vec3 delta = vtx[i] - vtx[j];
			if (delta.len() < 0.001f)
			{
				iMap = weldMap[j];
				nWelded++;
				break;
			}
		}
		if (iMap >= 0)
			weldMap.push_back(iMap);
		else
		{
			weldMap.push_back(unweldedVerts.size());
			unweldedVerts.push_back(vtx[i]);
			unweldedAttached.push_back(attached[i]);
		}
	}

	if (nWelded)
	{
		//CryLog("[Character Cloth] Found %i welded vertices out of %i ", nWelded, (int)vtx.size());
		//printf("[Character Cloth] Found %i welded vertices out of %i ", nWelded, (int)vtx.size());
	}

	// reconstruct indices
	std::vector<int> unweldedIndices;
	for (int i = 0; i < (int)idx.size(); i++)
	{
		unweldedIndices.push_back(weldMap[idx[i]]);
	}

	//////////////////////////////////////////////////////////////////////////
	idx = unweldedIndices;
	vtx = unweldedVerts;
	attached = unweldedAttached;
	//////////////////////////////////////////////////////////////////////////

	return true;
}

bool AttachmentVClothPreProcess::RemoveDegeneratedTriangles(std::vector<Vec3>& vtx, std::vector<int>& idx, std::vector<bool>& attached)
{
	// remove degenerated triangles, by shifting them to the end and resize - similar to CryPhysics/trimesh.h
	// move degenerate triangles to the end of the list
	int i, j;
	int nTris = (int)idx.size() / 3;
	for (i = 0, j = nTris; i < j; )
	{
		if (iszero(idx[i * 3] ^ idx[i * 3 + 1]) | iszero(idx[i * 3 + 1] ^ idx[i * 3 + 2]) |
		    iszero(idx[i * 3 + 2] ^ idx[i * 3]))
		{
			j--;
			for (int k = 0; k < 3; k++)
			{
				int itmp = idx[j * 3 + k];
				idx[j * 3 + k] = idx[i * 3 + k];
				idx[i * 3 + k] = itmp;
			}
		}
		else i++;
	}
	nTris = i;

	if (nTris * 3 < idx.size()) idx.resize(nTris * 3);

	return true;
}

//////////////////////////////////////////////////////////////////////////

namespace VClothPreProcessUtils
{
struct STriInfo
{
	index_t ibuddy[3];
};

template<class F>
inline int IntersectLists(F* pSrc0, int nSrc0, F* pSrc1, int nSrc1, F* pDst)
{
	int i0, i1, n;
	for (i0 = i1 = n = 0; ((i0 - nSrc0) & (i1 - nSrc1)) < 0; )
	{
		const F a = pSrc0[i0];
		const F b = pSrc1[i1];
		F equal = iszero(a - b);
		pDst[n] = a;
		n += equal;
		i0 += isneg(a - b) + equal;
		i1 += isneg(b - a) + equal;
	}
	return n;
}
}

// on the basis of 'CTriMesh::CalculateTopology()'
// int CalculateTopology(index_t* pIndices, int bCheckOnly = 0);
bool AttachmentVClothPreProcess::CalculateTopology(std::vector<Vec3> const& vtx, std::vector<int> const& idx, std::vector<VClothPreProcessUtils::STriInfo>& pTopology)
{
	int nTris = idx.size() / 3;
	int nVertices = vtx.size();
	int i, j, i1, j1, itri, * pTris, * pVtxTris, * pEdgeTris, nBadEdges[2] = { 0, 0 };
	Vec3 edge, v0, v1;
	float elen2;
	quotientf sina, minsina;

	pTopology.resize(nTris);

	// strided_pointer<Vec3mem> pVtxMem = strided_pointer<Vec3mem>((Vec3mem*)m_pVertices.data, m_pVertices.iStride);
	const Vec3* pVtxMem = &vtx[0];

	memset(pTris = new int[nVertices + 1], 0, (nVertices + 1) * sizeof(int));  // for each used vtx, points to the corresponding tri list start
	pVtxTris = new int[nTris * 4];                                             // holds tri lists for each used vtx
	pEdgeTris = pVtxTris + nTris * 3;

	for (i = 0; i < nTris; i++)
	{
		for (j = 0; j < 3; j++)
		{
			pTris[idx[i * 3 + j]]++;
		}
	}
	for (i = 0; i < nVertices; i++)
	{
		pTris[i + 1] += pTris[i];
	}
	for (i = nTris - 1; i >= 0; i--)
	{
		for (j = 0; j < 3; j++)
		{
			pVtxTris[--pTris[idx[i * 3 + j]]] = i;
		}
	}
	for (i = 0; i < nTris; i++)
	{
		for (j = 0; j < 3; j++)
		{
			int nTrisIntersect = VClothPreProcessUtils::IntersectLists(pVtxTris + pTris[idx[i * 3 + j]], pTris[idx[i * 3 + j] + 1] - pTris[idx[i * 3 + j]],
			                                                           pVtxTris + pTris[idx[i * 3 + inc_mod3[j]]], pTris[idx[i * 3 + inc_mod3[j]] + 1] - pTris[idx[i * 3 + inc_mod3[j]]], pEdgeTris);
			assert(nTrisIntersect <= nTris);   // check for possible memory corruption in case of degenerative triangles
			// if (nTris!=2) - the mesh is not manifold
			if (nTrisIntersect == 2)
				pTopology[i].ibuddy[j] = pEdgeTris[iszero(i - pEdgeTris[0])];
			else
			{
				// among the other triangles sharing this edge, find the one that has it in reverse order
				edge = pVtxMem[idx[i * 3 + inc_mod3[j]]] - pVtxMem[idx[i * 3 + j]];
				v0 = pVtxMem[idx[i * 3 + dec_mod3[j]]] - pVtxMem[idx[i * 3 + j]];
				v0 = v0 * (elen2 = edge.len2()) - edge * (edge * v0);
				itri = -1;
				minsina.set(3.0f, 1.0f);
				for (i1 = 0; i1 < nTrisIntersect; i1++)
				{
					if (pEdgeTris[i1] != i)
						for (j1 = 0; j1 < 3; j1++)
						{
							if (idx[i * 3 + j] == idx[pEdgeTris[i1] * 3 + inc_mod3[j1]] && idx[i * 3 + inc_mod3[j]] == idx[pEdgeTris[i1] * 3 + j1])
							{
								v1 = pVtxMem[idx[pEdgeTris[i1] * 3 + dec_mod3[j1]]] - pVtxMem[idx[i * 3 + j]];
								v1 = v1 * elen2 - edge * (edge * v1);
								sina.set(sqr_signed((v0 ^ v1) * edge), v0.len2() * v1.len2() * elen2);
								float denom = isneg(1e10f - sina.y) * 1e-15f;
								sina.x *= denom;
								sina.y *= denom;
								sina += isneg(sina) * 2.0f;
								if (sina < minsina)
								{
									minsina = sina;
									itri = pEdgeTris[i1];
								}
							}
						}

				}
				pTopology[i].ibuddy[j] = itri;
				nBadEdges[min(nTrisIntersect - 1, 1)]++;
			}
		}
	}

	delete[] pVtxTris;
	delete[] pTris;
	return (nBadEdges[0] + nBadEdges[1]) == 0;
}

struct _SEdgeInfo
{
	int   m_iEdge;
	float m_cost;
	bool  m_skip;

	_SEdgeInfo() : m_iEdge(-1), m_cost(1e38f), m_skip(false) {}
};

namespace VClothPreProcessUtils
{
// copied form StatObjPhys.cpp
static inline int GetEdgeByBuddy(std::vector<VClothPreProcessUtils::STriInfo> const& pTopology, int itri, int itri_buddy)
{
	int iedge = 0, imask;
	imask = pTopology[itri].ibuddy[1] - itri_buddy;
	imask = (imask - 1) >> 31 ^ imask >> 31;
	iedge = 1 & imask;
	imask = pTopology[itri].ibuddy[2] - itri_buddy;
	imask = (imask - 1) >> 31 ^ imask >> 31;
	iedge = iedge & ~imask | 2 & imask;
	return iedge;
}
}

bool AttachmentVClothPreProcess::CreateLinks(std::vector<Vec3> const& vtx, std::vector<int> const& idx)
{
	std::vector<VClothPreProcessUtils::STriInfo> pTopology;
	CalculateTopology(vtx, idx, pTopology);

	//////////////////////////////////////////////////////////////////////////

	int nTris = idx.size() / 3;

	_SEdgeInfo(*pInfo)[3] = new _SEdgeInfo[nTris][3];
	for (int i = 0; i < nTris; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			int i1 = idx[i * 3 + j];
			int i2 = idx[i * 3 + inc_mod3[j]];
			const Vec3& v1 = vtx[i1];
			const Vec3& v2 = vtx[i2];
			// compute the cost of the edge (squareness of quad)
			int adjTri = pTopology[i].ibuddy[j];
			if (adjTri >= 0)
			{
				float cost;
				int adjEdge = VClothPreProcessUtils::GetEdgeByBuddy(pTopology, adjTri, i);
				int i3 = idx[i * 3 + dec_mod3[j]];
				const Vec3& v3 = vtx[i3];
				int i4 = idx[adjTri * 3 + dec_mod3[adjEdge]];
				const Vec3& v4 = vtx[i4];
				cost = fabs((v1 - v2).len() - (v3 - v4).len());         // difference between diagonals
				// get the lowest value, although it should be the same
				if (pInfo[adjTri][adjEdge].m_cost < cost)
					cost = pInfo[adjTri][adjEdge].m_cost;
				else
					pInfo[adjTri][adjEdge].m_cost = cost;
				pInfo[i][j].m_cost = cost;
			}
		}
	}
	// count the number of edges - for each tri, mark each edge, unless buddy already did it
	int nEdges = 0;
	for (int i = 0; i < nTris; i++)
	{
		int bDegen = 0;
		float len[3];
		float minCost = 1e37f;
		int iedge = -1;
		for (int j = 0; j < 3; j++)
		{
			const Vec3& v1 = vtx[idx[i * 3 + j]];
			const Vec3& v2 = vtx[idx[i * 3 + inc_mod3[j]]];
			len[j] = (v1 - v2).len2();
			bDegen |= iszero(len[j]);
			int adjTri = pTopology[i].ibuddy[j];
			if (adjTri >= 0)
			{
				int adjEdge = VClothPreProcessUtils::GetEdgeByBuddy(pTopology, adjTri, i);
				float cost = pInfo[i][j].m_cost;
				if (pInfo[adjTri][adjEdge].m_skip)
					cost = -1e36f;
				bool otherSkipped = false;
				for (int k = 0; k < 3; k++)
				{
					if (pInfo[adjTri][k].m_skip && k != adjEdge)
					{
						otherSkipped = true;
						break;
					}
				}
				if (cost < minCost && !otherSkipped)
				{
					minCost = cost;
					iedge = j;
				}
			}
		}

		int j = 0;
		if (iedge >= 0)
		{
			j = iedge & - bDegen;
			pInfo[i][iedge].m_skip = true;
		}
		do
		{
			if (pInfo[i][j].m_iEdge < 0 && !(j == iedge && !bDegen))
			{
				int adjTri = pTopology[i].ibuddy[j];
				if (adjTri >= 0)
				{
					int adjEdge = VClothPreProcessUtils::GetEdgeByBuddy(pTopology, adjTri, i);
					pInfo[adjTri][adjEdge].m_iEdge = nEdges;
				}
				pInfo[i][j].m_iEdge = nEdges++;
			}
		}
		while (++j < 3 && !bDegen);
	}
	int num = ((nEdges / 2) + 1) * 2;
	m_linksStretch.resize(num);
	int* pVtxEdges = new int[nEdges * 2];
	for (int i = 0; i < nTris; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			int iedge = pInfo[i][j].m_iEdge;
			if (iedge >= 0)
			{
				int i0 = m_linksStretch[iedge].i1 = idx[i * 3 + j];
				int i1 = m_linksStretch[iedge].i2 = idx[i * 3 + inc_mod3[j]];
				m_linksStretch[iedge].lenSqr = (vtx[i0] - vtx[i1]).len2();
			}
		}
	}
	if (nEdges & 1)
	{
		m_linksStretch[num - 1].skip = true;
	}

	// for each vertex, trace ccw fan around it and store in m_pVtxEdges
	STopology* pTopologyCCW = new STopology[vtx.size()];
	int nVtxEdges = 0;
	for (int i = 0; i < nTris; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			int ivtx = idx[i * 3 + j];
			if (!pTopologyCCW[ivtx].iSorted)
			{
				int itri = i;
				int iedge = j;
				pTopologyCCW[ivtx].iStartEdge = nVtxEdges;
				pTopologyCCW[ivtx].bFullFan = 1;
				int itrinew;
				do
				{
					// first, trace cw fan until we find an open edge (if any)
					itrinew = pTopology[itri].ibuddy[iedge];
					if (itrinew <= 0)
						break;
					iedge = inc_mod3[VClothPreProcessUtils::GetEdgeByBuddy(pTopology, itrinew, itri)];
					itri = itrinew;
				}
				while (itri != i);
				int itri0 = itri;
				do
				{
					// now trace ccw fan
					assert(itri < nTris);
					if (pInfo[itri][iedge].m_iEdge >= 0)
						pVtxEdges[nVtxEdges++] = pInfo[itri][iedge].m_iEdge;
					itrinew = pTopology[itri].ibuddy[dec_mod3[iedge]];
					if (itrinew < 0)
					{
						if (pInfo[itri][dec_mod3[iedge]].m_iEdge >= 0)
							pVtxEdges[nVtxEdges++] = pInfo[itri][dec_mod3[iedge]].m_iEdge;
						pTopologyCCW[ivtx].bFullFan = 0;
						break;
					}
					iedge = VClothPreProcessUtils::GetEdgeByBuddy(pTopology, itrinew, itri);
					itri = itrinew;
				}
				while (itri != itri0);
				pTopologyCCW[ivtx].iEndEdge = nVtxEdges - 1;
				pTopologyCCW[ivtx].iSorted = 1;
			}
		}
	}
	delete[] pInfo;

	// add shear and bending edges
	m_linksShear.clear();
	m_linksBend.clear();
	for (int i = 0; i < nEdges; i++)
	{
		int i1 = m_linksStretch[i].i1;
		int i2 = m_linksStretch[i].i2;
		// first point 1
		float maxLen = 0;
		int maxIdx = -1;
		bool maxAdded = false;
		for (int j = pTopologyCCW[i1].iStartEdge; j < pTopologyCCW[i1].iEndEdge + pTopologyCCW[i1].bFullFan; j++)
		{
			int i3 = m_linksStretch[pVtxEdges[j]].i2 + m_linksStretch[pVtxEdges[j]].i1 - i1;
			float len = (vtx[i3] - vtx[i2]).len2();
			bool valid = i3 != i1 && i2 < i3;
			if (len > maxLen)
			{
				maxLen = len;
				maxIdx = m_linksShear.size();
				maxAdded = valid;
			}
			if (valid)
			{
				SLink link;
				link.i1 = i2;
				link.i2 = i3;
				link.lenSqr = len;
				m_linksShear.push_back(link);
			}
		}
		// we make the assumption that the longest edge of all is a bending one
		if (maxIdx >= 0 && maxAdded && pTopologyCCW[i1].bFullFan)
		{
			m_linksBend.push_back(m_linksShear[maxIdx]);
			m_linksShear.erase(m_linksShear.begin() + maxIdx);
		}
		// then point 2
		maxLen = 0;
		maxIdx = -1;
		maxAdded = false;
		for (int j = pTopologyCCW[i2].iStartEdge; j < pTopologyCCW[i2].iEndEdge + pTopologyCCW[i2].bFullFan; j++)
		{
			int i3 = m_linksStretch[pVtxEdges[j]].i2 + m_linksStretch[pVtxEdges[j]].i1 - i2;
			float len = (vtx[i3] - vtx[i1]).len2();
			bool valid = i3 != i2 && i1 < i3;
			if (len > maxLen)
			{
				maxLen = len;
				maxIdx = m_linksShear.size();
				maxAdded = valid;
			}
			if (valid)
			{
				SLink link;
				link.i1 = i1;
				link.i2 = i3;
				link.lenSqr = len;
				m_linksShear.push_back(link);
			}
		}
		if (maxIdx >= 0 && maxAdded && pTopologyCCW[i2].bFullFan)
		{
			m_linksBend.push_back(m_linksShear[maxIdx]);
			m_linksShear.erase(m_linksShear.begin() + maxIdx);
		}
	}

	//delete[] pTopology;
	delete[] pVtxEdges;
	m_linksStretch.resize(nEdges);

	return true;
}
