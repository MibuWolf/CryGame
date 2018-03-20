// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

namespace CryVR
{
namespace Osvr
{
class Device;

class Resources
{
public:
	static Device* GetAssociatedDevice() { return ms_pRes ? ms_pRes->m_pDevice : 0; }

	static void    Init();
	static void    Shutdown();

private:
	Resources();
	~Resources();

private:
	static Resources* ms_pRes;
	static bool       ms_libInitialized;
private:
	Device*           m_pDevice; // Attached device (only one supported at the moment)
};
} // namespace Osvr
} // namespace CryVR