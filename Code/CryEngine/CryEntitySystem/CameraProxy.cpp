// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

// -------------------------------------------------------------------------
//  File name:   CameraProxy.h
//  Version:     v1.00
//  Created:     5/12/2005 by Timur.
//  Compilers:   Visual Studio.NET 2003
//  Description:
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "CameraProxy.h"
#include <CryNetwork/ISerialize.h>

CRYREGISTER_CLASS(CEntityComponentCamera);

//////////////////////////////////////////////////////////////////////////
CEntityComponentCamera::CEntityComponentCamera()
{
	m_componentFlags.Add(EEntityComponentFlags::Legacy);
	m_componentFlags.Add(EEntityComponentFlags::NoSave);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentCamera::Initialize()
{
	UpdateMaterialCamera();
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentCamera::ProcessEvent(SEntityEvent& event)
{
	switch (event.event)
	{
	case ENTITY_EVENT_INIT:
	case ENTITY_EVENT_XFORM:
		{
			UpdateMaterialCamera();
		}
		break;
	}
}

//////////////////////////////////////////////////////////////////////////
uint64 CEntityComponentCamera::GetEventMask() const
{
	return BIT64(ENTITY_EVENT_XFORM) | BIT64(ENTITY_EVENT_INIT);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentCamera::UpdateMaterialCamera()
{
	float fov = m_camera.GetFov();
	m_camera = GetISystem()->GetViewCamera();
	Matrix34 wtm = m_pEntity->GetWorldTM();
	wtm.OrthonormalizeFast();
	m_camera.SetMatrix(wtm);
	m_camera.SetFrustum(m_camera.GetViewSurfaceX(), m_camera.GetViewSurfaceZ(), fov, m_camera.GetNearPlane(), m_camera.GetFarPlane(), m_camera.GetPixelAspectRatio());

	IMaterial* pMaterial = m_pEntity->GetMaterial();
	if (pMaterial)
		pMaterial->SetCamera(m_camera);
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentCamera::SetCamera(CCamera& cam)
{
	m_camera = cam;
	UpdateMaterialCamera();
}

//////////////////////////////////////////////////////////////////////////
void CEntityComponentCamera::GameSerialize(TSerialize ser)
{

}
