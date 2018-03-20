// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 
#include "StdAfx.h"
#include "CryVulkan.hpp"
#include "API/VKInstance.hpp"

#if VK_USE_DXGI
	#include "CryVulkanWrappers/GI/DXGI/CCryVKGIFactory_DXGI.hpp"
#endif


HRESULT WINAPI D3DCreateBlob(SIZE_T NumBytes, ID3DBlob** ppBuffer)
{
	*ppBuffer = new CCryVKBlob(NumBytes);
	return *ppBuffer ? (*ppBuffer)->GetBufferPointer() ? S_OK : (delete *ppBuffer, E_OUTOFMEMORY) : E_FAIL;
}

HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory)
{
#if VK_USE_DXGI
	*ppFactory = CCryVKGIFactory_DXGI::Create();
#else
	*ppFactory = CCryVKGIFactory::Create();
#endif

	return *ppFactory ? 0 : -1;
}

HRESULT WINAPI VKCreateDevice(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	CONST D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	HWND outputWindow,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{

	NCryVulkan::SSurfaceCreationInfo surfaceInfo;

#ifdef CRY_PLATFORM_WINDOWS
	surfaceInfo.windowHandle = outputWindow;
	surfaceInfo.appHandle = (HINSTANCE)GetWindowLongPtr(surfaceInfo.windowHandle, GWLP_HINSTANCE);
#else
	SDL_Window* pWindowContext = reinterpret_cast<SDL_Window*>(pSwapChainDesc->OutputWindow);
	struct SDL_SysWMinfo info;
	ZeroStruct(info);
	info.version.major = SDL_MAJOR_VERSION;
	info.version.minor = SDL_MINOR_VERSION;
	if (!SDL_GetWindowWMInfo(pWindowContext, &info))
	{
		return S_FALSE;
	}

	surfaceInfo.pWindow = pWindowContext;
	surfaceInfo.pNativeWindow = info.info.android.window;
//	surfaceInfo.hNativeSurface = info.info.android.surface;
#endif

	_smart_ptr<NCryVulkan::CDevice> pDevice = pAdapter->GetFactory()->GetVkInstance()->CreateDevice(pAdapter->GetDeviceIndex(), surfaceInfo);
	if (!pDevice)
	{
		return S_FALSE;
	}

	if (ppImmediateContext)
	{
		pDevice->AddRef();
		*ppImmediateContext = pDevice.get();
	}

	*ppDevice = pDevice.ReleaseOwnership();
	*pFeatureLevel = D3D_FEATURE_LEVEL_12_1;
	return S_OK;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(USE_SDL2_VIDEO)
bool VKCreateSDLWindow(const char* szTitle, uint32 uWidth, uint32 uHeight, bool bFullScreen, HWND* pHandle)
{
	return NCryVulkan::CInstance::CreateSDLWindow(szTitle, uWidth, uHeight, bFullScreen, pHandle);
}

void VKDestroySDLWindow(HWND kHandle)
{
	NCryVulkan::CInstance::DestroySDLWindow(kHandle);
}
#endif

namespace NCryVulkan
{
	VkFormat ConvertFormat(DXGI_FORMAT format)
	{
		if (format > CRY_ARRAY_COUNT(s_FormatWithSize))
		{
			format = DXGI_FORMAT_UNKNOWN;
		}
		return s_FormatWithSize[format].Format;
	}

	DXGI_FORMAT ConvertFormat(VkFormat format)
	{
		if (format > CRY_ARRAY_COUNT(s_VkFormatToDXGI))
		{
			format = VK_FORMAT_UNDEFINED;
		}
		return s_VkFormatToDXGI[format];
	}
}
