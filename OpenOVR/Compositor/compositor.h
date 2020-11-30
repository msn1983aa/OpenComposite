#pragma once

#include "OpenVR/interfaces/vrtypes.h"
#ifdef WIN32
	// Windows template libraries
	#include <atlbase.h>
	#include <wrl/client.h>
#endif

#include "../Misc/xr_ext.h"
#include "../Misc/xrutil.h"

#include <memory>
#include <vector>

#ifdef WIN32
	using Microsoft::WRL::ComPtr;
#endif

typedef unsigned int GLuint;

class Compositor {
public:
	virtual ~Compositor();

	// Only copy a texture - this can be used for overlays and such
	virtual void Invoke(const vr::Texture_t* texture) = 0;

	virtual void Invoke(XruEye eye, const vr::Texture_t* texture, const vr::VRTextureBounds_t* bounds,
	    vr::EVRSubmitFlags submitFlags, XrCompositionLayerProjectionView& viewport)
	    = 0;

	virtual void InvokeCubemap(const vr::Texture_t* textures) = 0;
	virtual bool SupportsCubemap() { return false; }

	virtual XrSwapchain GetSwapChain() { return chain; };

	virtual unsigned int GetFlags() { return 0; }

	/**
	 * Loads and unloads some context required for submitting textures to LibOVR. LoadSubmitContext is
	 *  called before calling either Invoke or ovr_CommitTextureSwapChain, and ResetSubmitContext after
	 *  calling both of them.
	 */
	virtual void LoadSubmitContext(){};
	virtual void ResetSubmitContext(){};

protected:
	XrSwapchain chain = nullptr;

	// The request used to create the current swapchain. This can be used to check if the swapchain needs recreating.
	XrSwapchainCreateInfo createInfo{};
};

#ifndef OC_XR_PORT
class DX12Compositor : public Compositor {
public:
	DX12Compositor(vr::D3D12TextureData_t* td, OVR::Sizei& bufferSize, ovrTextureSwapChain* chains);

	// Override
	virtual void Invoke(ovrEyeType eye, const vr::Texture_t* texture, const vr::VRTextureBounds_t* bounds,
	    vr::EVRSubmitFlags submitFlags, ovrLayerEyeFov& layer) override;

private:
	ComPtr<ID3D12Device> device;
	ComPtr<ID3D12CommandQueue> queue;
	ComPtr<ID3D12GraphicsCommandList> commandList;
	OVR::Sizei singleScreenSize;

	int chainLength = -1;

	ComPtr<ID3D12CommandAllocator> allocator = NULL;
	ComPtr<ID3D12DescriptorHeap> rtvVRHeap = NULL; // Resource Target View Heap
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> texRtv;
	std::vector<ID3D12Resource*> texResource;

	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	ComPtr<ID3D12PipelineState> pipelineState;
	ComPtr<ID3D12RootSignature> rootSignature;

	ComPtr<ID3D12DescriptorHeap> srvHeap = NULL;
	UINT m_rtvDescriptorSize;
};
#endif

class DX11Compositor : public Compositor {
public:
	DX11Compositor(ID3D11Texture2D* td);

	virtual ~DX11Compositor() override;

	// Override
	virtual void Invoke(const vr::Texture_t* texture) override;

	virtual void InvokeCubemap(const vr::Texture_t* textures) override;
	virtual bool SupportsCubemap() override { return true; }

	virtual void Invoke(XruEye eye, const vr::Texture_t* texture, const vr::VRTextureBounds_t* bounds,
	    vr::EVRSubmitFlags submitFlags, XrCompositionLayerProjectionView& viewport) override;

	unsigned int GetFlags() override;

	ID3D11Device* GetDevice() { return device; }

protected:
	void CheckCreateSwapChain(const vr::Texture_t* texture, bool cube);

	void ThrowIfFailed(HRESULT test);

	bool CheckChainCompatible(D3D11_TEXTURE2D_DESC& inputDesc, vr::EColorSpace colourSpace);

	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;

	bool submitVerticallyFlipped = false;

	std::vector<XrSwapchainImageD3D11KHR> imagesHandles;
};

#ifndef OC_XR_PORT
class DX10Compositor : public DX11Compositor {
public:
	DX10Compositor(ID3D10Texture2D* td);

	virtual void Invoke(const vr::Texture_t* texture) override;

	virtual void LoadSubmitContext() override;
	virtual void ResetSubmitContext() override;

private:
	CComQIPtr<ID3D11Device1> device1 = nullptr;
	CComQIPtr<ID3D11DeviceContext1> context1 = nullptr;
	CComPtr<ID3DDeviceContextState> customContextState = nullptr;
	CComPtr<ID3DDeviceContextState> originalContextState = nullptr;
};

class GLCompositor : public Compositor {
public:
	GLCompositor(OVR::Sizei size);

	unsigned int GetFlags() override;

	// Override
	virtual void Invoke(const vr::Texture_t* texture) override;

	virtual void Invoke(ovrEyeType eye, const vr::Texture_t* texture, const vr::VRTextureBounds_t* bounds,
	    vr::EVRSubmitFlags submitFlags, ovrLayerEyeFov& layer) override;

	virtual void InvokeCubemap(const vr::Texture_t* textures) override;

private:
	GLuint fboId = 0;
};

class VkCompositor : public Compositor {
public:
	VkCompositor(const vr::Texture_t* initialTexture);

	virtual ~VkCompositor() override;

	// Override
	virtual void Invoke(const vr::Texture_t* texture) override;

	virtual void Invoke(ovrEyeType eye, const vr::Texture_t* texture, const vr::VRTextureBounds_t* bounds,
	    vr::EVRSubmitFlags submitFlags, ovrLayerEyeFov& layer) override;

	virtual void InvokeCubemap(const vr::Texture_t* textures) override;

	unsigned int GetFlags() override;

private:
	bool CheckChainCompatible(const vr::VRVulkanTextureData_t& tex, const ovrTextureSwapChainDesc& chainDesc, vr::EColorSpace colourSpace);

	bool submitVerticallyFlipped = false;
	ovrTextureSwapChainDesc chainDesc;
	uint64_t /*VkCommandPool*/ commandPool = 0;

	uint32_t graphicsQueueFamilyId = 0;
};
#endif
