/*
	material implementation without base texture acess
*/

#include "stdafx.h"

#if defined(OFFLINE_COMPUTATION)

#include "DefaultBacklightingMaterial.h"
#include <PRT/SimpleIndexedMesh.h>

NSH::NMaterial::CDefaultBacklightingSHMaterial::CDefaultBacklightingSHMaterial
(
	CSimpleIndexedMesh *pMesh, 
	const Vec3& crBacklightingColour,
	const float cTransparencyShadowingFactor, 
	const float cAlphaIntensity
) : 
	m_pMesh(pMesh), m_RedIntensity(1.f), m_GreenIntensity(1.f), m_BlueIntensity(1.f), m_TransparencyShadowingFactor(cTransparencyShadowingFactor), m_AlphaIntensity(cAlphaIntensity), m_BacklightingColour(crBacklightingColour)
{
	assert(m_TransparencyShadowingFactor >= 0.f && m_TransparencyShadowingFactor <= 1.f);
	assert(cAlphaIntensity >= 0.f && cAlphaIntensity <= 1.f);
	assert(pMesh);
	m_TransparencyAlphaFactor = m_TransparencyShadowingFactor * m_AlphaIntensity;
	m_ApplyTransparencyAlpha = (m_TransparencyAlphaFactor > 0.f);
}

const NSH::TRGBCoeffD NSH::NMaterial::CDefaultBacklightingSHMaterial::DiffuseIntensity
	(
		const TRGBCoeffD& crIncidentIntensity, 
		const uint32 cTriangleIndex, 
		const TVec& rBaryCoord, 
		const TCartesianCoord& rIncidentDir, 
		const bool cApplyCosTerm, 
		const bool cApplyExitanceTerm,
		const bool cAbsCosTerm,
		const bool cUseTransparency
	)const
{
	//retrieve normal
	const CObjFace& f = m_pMesh->GetObjFace(cTriangleIndex);
	const Vec3& n0 = m_pMesh->GetWSNormal(f.n[0]);
	const Vec3& n1 = m_pMesh->GetWSNormal(f.n[1]);
	const Vec3& n2 = m_pMesh->GetWSNormal(f.n[2]);
	//interpolate according to barycentric coordinates
	assert(Abs(rBaryCoord.x + rBaryCoord.y + rBaryCoord.z - 1.) < 0.01);	
	const TVec normal
	(
		rBaryCoord.x * n0.x + rBaryCoord.y * n1.x + rBaryCoord.z * n2.x,
		rBaryCoord.x * n0.y + rBaryCoord.y * n1.y + rBaryCoord.z * n2.y,
		rBaryCoord.x * n0.z + rBaryCoord.y * n1.z + rBaryCoord.z * n2.z
	);
	const double cCosAngle = cAbsCosTerm?abs(normal * rIncidentDir):(normal * rIncidentDir);
	if(cApplyCosTerm && cCosAngle <= 0.f)
		return TRGBCoeffD(0.,0.,0.);
	return ComputeDiffuseIntensity(crIncidentIntensity, cApplyExitanceTerm, cApplyCosTerm, cUseTransparency, cCosAngle);
}

const NSH::TRGBCoeffD NSH::NMaterial::CDefaultBacklightingSHMaterial::DiffuseIntensity
(
	const TVec&, 
	const TVec& crVertexNormal, 
	const Vec2&, 
	const TRGBCoeffD& crIncidentIntensity, 
	const TCartesianCoord& rIncidentDir, 
	const bool cApplyCosTerm, 
	const bool cApplyExitanceTerm, 
	const bool cAbsCosTerm,
	const bool cUseTransparency
)const
{
	const double cCosAngle = cAbsCosTerm?abs(crVertexNormal * rIncidentDir):(crVertexNormal * rIncidentDir);
	if(cApplyCosTerm && cCosAngle <= 0.f)
		return TRGBCoeffD(0.,0.,0.);
	return ComputeDiffuseIntensity(crIncidentIntensity, cApplyExitanceTerm, cApplyCosTerm, cUseTransparency, cCosAngle);
}

const NSH::TRGBCoeffD NSH::NMaterial::CDefaultBacklightingSHMaterial::ComputeDiffuseIntensity
(
	const TRGBCoeffD& crIncidentIntensity,
	const bool cApplyExitanceTerm, 
	const bool cApplyCosTerm,
	const bool cUseTransparency,
	const double cCosAngle
) const
{
	assert(!cUseTransparency || !cApplyExitanceTerm); //should not be set both
	if(cApplyExitanceTerm || cUseTransparency)
	{
		NSH::TRGBCoeffD intensity(crIncidentIntensity);//basic intensity is incoming intensity
		//if exitance value is to be computed, treat cUseTransparency as false since either we are interested in the transparent intensity or in the reflecting one
		if(cApplyExitanceTerm)
		{
			intensity.x *= m_RedIntensity;
			intensity.y *= m_GreenIntensity;
			intensity.z *= m_BlueIntensity;
		}
		else
		if(cUseTransparency && m_ApplyTransparencyAlpha)
		{
			//idea: if transparent, the full transparency should be returned, otherwise the weighted backlighting colour
			intensity.x *= (float)(m_TransparencyAlphaFactor * m_BacklightingColour.x);
			intensity.y *= (float)(m_TransparencyAlphaFactor * m_BacklightingColour.y);
			intensity.z *= (float)(m_TransparencyAlphaFactor * m_BacklightingColour.z);
		}
		if(cApplyCosTerm)
			return (intensity * cCosAngle);
		else
			return intensity;
	}
	else
	{
		if(cApplyCosTerm)
			return (crIncidentIntensity * cCosAngle);
		else
			return crIncidentIntensity;
	}
}

void NSH::NMaterial::CDefaultBacklightingSHMaterial::SetDiffuseIntensity(const float cRedIntensity, const float cGreenIntensity, const float cBlueIntensity, const float cAlphaIntensity)
{
	m_RedIntensity		= cRedIntensity;
	m_GreenIntensity	= cGreenIntensity;
	m_BlueIntensity		= cBlueIntensity;
	m_AlphaIntensity	= cAlphaIntensity;
	//refresh
	m_TransparencyAlphaFactor = m_TransparencyShadowingFactor * m_AlphaIntensity;
	m_ApplyTransparencyAlpha = (m_TransparencyAlphaFactor > 0.f);
}

#endif