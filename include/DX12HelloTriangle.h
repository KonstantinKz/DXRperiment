// This code is based on the code provided 
// by Microsoft in DX12 sample projects 

//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

#include <vector>
#include <dxcapi.h>

#include "DXPipeline.h"
#include "TopLevelASGenerator.h"
#include "ShaderBindingTableGenerator.h"

using namespace DirectX;

using Microsoft::WRL::ComPtr;

// DXR
struct AccelerationStructureBuffers
{
	ComPtr<ID3D12Resource> pScratch;      // Scratch memory for AS builder
	ComPtr<ID3D12Resource> pResult;       // Where the AS is
	ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances
};

class DX12HelloTriangle : public DXPipeline
{
public:
	DX12HelloTriangle(uint32_t viewportWidth, uint32_t viewportHeight, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

private:
	static const uint32_t frameCount = 2;

	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT4 color;
	};

	// Pipeline objects
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapchain;
	ComPtr<ID3D12Device5> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[frameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;
	uint32_t m_rtvDescriptorSize;

	// App resources
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

	// Synchronization objects
	uint32_t m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	uint64_t m_fenceValue;
	bool m_raster = true;


	// DXR AS
	ComPtr<ID3D12Resource> m_bottomLevelAS;
	nv_helpers_dx12::TopLevelASGenerator m_topLevelASGenerator;
	AccelerationStructureBuffers m_topLevelASBuffers;
	std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> m_instances;

	// DXR 
	ComPtr<IDxcBlob> m_rayGenLibrary;
	ComPtr<IDxcBlob> m_missLibrary;
	ComPtr<IDxcBlob> m_hitLibrary;

	ComPtr<ID3D12RootSignature> m_rayGenSignature;
	ComPtr<ID3D12RootSignature> m_missSignature;
	ComPtr<ID3D12RootSignature> m_hitSignature;
	
	ComPtr<ID3D12Resource> m_outputResource;
	ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;

	nv_helpers_dx12::ShaderBindingTableGenerator m_sbtHelper;
	ComPtr<ID3D12Resource> m_sbtStorage;

	// RT pipeline state
	ComPtr<ID3D12StateObject> m_rtStateObject;
	// Ray tracing pipeline state properties, retaining the shader identifiers
	// to use in the Shader Binding Table
	ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;


	void InitPipelineObjects();
	void LoadAssets();
	void PopulateCommandList();
	void WaitForPreviousFrame();
	void CheckRaytracingSupport();
	virtual void OnKeyUp(uint8_t key);

	// DXR AS
	AccelerationStructureBuffers CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers);
	void CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>& instances);
	void CreateAccelerationStructures();

	// DXR
	ComPtr<ID3D12RootSignature> CreateGenSignature();
	ComPtr<ID3D12RootSignature> CreateMissSignature();
	ComPtr<ID3D12RootSignature> CreateHitSignature();
	void CreateRaytracingPipeline();
	void CreateRaytracingOutputBuffer();
	void CreateShaderResourceHeap();
	void CreateShaderBindingTable();
};