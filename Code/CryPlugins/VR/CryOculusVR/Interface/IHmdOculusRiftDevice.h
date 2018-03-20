// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include <CrySystem/VR/IHMDDevice.h>
#include <CryRenderer/IStereoRenderer.h>

struct IUnknown;

// Forward declaration of Oculus-specific internal data structures
struct ovrTextureSwapChainData;
struct ovrMirrorTextureData;

namespace CryVR
{
namespace Oculus {

struct TextureDesc
{
	uint32 width;
	uint32 height;
	uint32 format;
};

// Set of ID3D11Texture2D* (DX11) textures from a singe swap chain to be sent to the Hmd
struct STextureSwapChain
{
	ovrTextureSwapChainData* pDeviceTextureSwapChain; // data structure wrapping a texture set for a single image
	IUnknown**               pTextures;               // texture set of ID3D11Texture2D* (DX11) / ID3D12Resource* (DX12) inside pDeviceTextureSwapChain
	uint32                   numTextures;             // number of textures in the set

	STextureSwapChain()
		: pDeviceTextureSwapChain(nullptr)
		, pTextures(nullptr)
		, numTextures(0)
	{
	}
};

struct STexture
{
	ovrMirrorTextureData* pDeviceTexture; // data structure wrapping a single image
	IUnknown*             pTexture;       // ID3D11Texture2D* (DX11) / ID3D12Resource* (DX12) inside pDeviceTexture

	STexture()
		: pDeviceTexture(nullptr)
		, pTexture(nullptr)
	{
	}
};

// Info required render a texture set into a particular device layer
// This info is passed across DLL boundaries from the renderer to the Hmd device
struct SHmdSwapChainInfo
{
	ovrTextureSwapChainData* pDeviceTextureSwapChain; // data structure wrapping a texture set for a single image
	Vec2i                    viewportPosition;
	Vec2i                    viewportSize;
	uint8                    eye;         // only for Scene3D layer (0 left , 1 right)
	bool                     bActive;     // should this layer be sent to the Hmd?
	RenderLayer::ELayerType  layerType;
	RenderLayer::TLayerId    layerId;
	QuatTS                   pose;        // only for Quad layers. (in camera space) layer position, orientation and scale
};

// Info required to send all device layers to the Hmd
// This info is passed across DLL boundaries from the renderer to the Hmd device
struct SHmdSubmitFrameData
{
	SHmdSwapChainInfo* pLeftEyeScene3D;
	SHmdSwapChainInfo* pRightEyeScene3D;
	SHmdSwapChainInfo* pQuadLayersSwapChains;
	uint32             numQuadLayersSwapChains;
};

// Hmd device specialization for Oculus
struct IOculusDevice : public IHmdDevice
{
public:
	virtual bool CreateSwapTextureSetD3D12(IUnknown* d3d12CommandQueue, TextureDesc desc, STextureSwapChain* set) = 0;
	virtual bool CreateMirrorTextureD3D12(IUnknown* d3d12CommandQueue, TextureDesc desc, STexture* texture) = 0;
	virtual bool CreateSwapTextureSetD3D11(IUnknown* d3d11Device, TextureDesc desc, STextureSwapChain* set) = 0;
	virtual bool CreateMirrorTextureD3D11(IUnknown* d3d11Device, TextureDesc desc, STexture* texture) = 0;

	virtual void DestroySwapTextureSet(STextureSwapChain* set) = 0;
	virtual void DestroyMirrorTexture(STexture* texture) = 0;
	virtual void PrepareTexture(STextureSwapChain* set, uint32 frameIndex) = 0; // deprecated? check HTC Vive and PS VR
	virtual void SubmitFrame(const SHmdSubmitFrameData pData) = 0;
	virtual int  GetCurrentSwapChainIndex(void* pSwapChain) const = 0;
protected:
	virtual ~IOculusDevice() {}
};

} // namespace Oculus
} // namespace CryVR
