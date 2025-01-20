#include <vector>
#include <stdexcept>
#include <dxcapi.h>

#include "stdafx.h"
#include "DX12HelloTriangle.h"
#include "DXRHelper.h"
#include "BottomLevelASGenerator.h"
#include "RaytracingPipelineGenerator.h"
#include "RootSignatureGenerator.h"
#include "gtc/type_ptr.hpp"
#include "Windowsx.h"

DX12HelloTriangle::DX12HelloTriangle(uint32_t viewportWidth, uint32_t viewportHeight, std::wstring name) : DXPipeline(viewportWidth, viewportHeight, name),
																										   m_frameIndex(0),
																										   m_viewport(0.f, 0.f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)),
																										   m_scissorRect(0.f, 0.f, static_cast<LONG>(viewportWidth), static_cast<LONG>(viewportHeight))
{
}

void DX12HelloTriangle::OnInit()
{
	InitPipelineObjects();
	LoadAssets();

	CheckRaytracingSupport();
	CreateAccelerationStructures();
	ThrowIfFailed(m_commandList->Close());

	CreateRaytracingPipeline();
	CreateRaytracingOutputBuffer();
	CreateCameraBuffer();
	m_cameraEye = {0.f, 0.f, 3.f};
	m_cameraDir = {0.f, 0.f, -3.f};
	m_cameraYaw = -90.0f;
	m_cameraPitch = 0;
	m_mousePosX = m_viewport.Width / 2;
	m_mousePosY = m_viewport.Height / 2;

	CreateShaderResourceHeap();
	CreateShaderBindingTable();
}

void DX12HelloTriangle::OnUpdate()
{
	UpdateCameraBuffer();
}

void DX12HelloTriangle::OnRender()
{
	PopulateCommandList();

	// Execute command list
	ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame
	ThrowIfFailed(m_swapchain->Present(1, 0));

	WaitForPreviousFrame();
}

void DX12HelloTriangle::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForPreviousFrame();

	CloseHandle(m_fenceEvent);
}

void DX12HelloTriangle::InitPipelineObjects()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the
	// active device.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device)));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);
		ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device)));
	}

	// Command queue
	D3D12_COMMAND_QUEUE_DESC queueDescription = {};
	queueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&m_commandQueue)));

	// Swapchain
	DXGI_SWAP_CHAIN_DESC1 swapchainDescription = {};
	swapchainDescription.BufferCount = frameCount;
	swapchainDescription.Width = m_width;
	swapchainDescription.Height = m_height;
	swapchainDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDescription.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapchain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),
		Win32Application::GetHwnd(),
		&swapchainDescription,
		nullptr, // full screen desc
		nullptr, // restrict to output
		&swapchain));

	ThrowIfFailed(swapchain.As(&m_swapchain));
	m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();

	// Descriptor heaps
	D3D12_DESCRIPTOR_HEAP_DESC heapDescription = {};
	heapDescription.NumDescriptors = frameCount;
	heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(m_device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&m_rtvHeap)));
	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Frame resources
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

	// RTV for each frame
	for (uint32_t n = 0; n < frameCount; n++)
	{
		ThrowIfFailed(m_swapchain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
		m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, descriptorHandle);
		descriptorHandle.Offset(1, m_rtvDescriptorSize);
	}

	ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

void DX12HelloTriangle::LoadAssets()
{
	// Camera constant buffer
	CD3DX12_ROOT_PARAMETER constantParameter;
	CD3DX12_DESCRIPTOR_RANGE range;
	range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	constantParameter.InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_ALL);

	// Root signature
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDescription;
	rootSignatureDescription.Init(1, &constantParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDescription, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
	ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

	// Pipeline state
	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif

	ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vertexShader, &error));
	ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &pixelShader, &error));

	// Define the vertex input layout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		};

	// Describe and create the graphics pipeline state object (PSO)
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

	// Create command list
	ThrowIfFailed(m_device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_commandAllocator.Get(),
		m_pipelineState.Get(),
		IID_PPV_ARGS(&m_commandList)));

	// Create vertex buffer
	{
		Vertex triangleVertices[] =
			{
				{{0.0f, 0.25f * m_aspectRatio, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
				{{0.25f, -0.25f * m_aspectRatio, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
				{{-0.25f, -0.25f * m_aspectRatio, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}};

		const uint32_t vertexBufferSize = sizeof(triangleVertices);

		CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufferdesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

		ThrowIfFailed(m_device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&bufferdesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)));

		// Copy the triangle data to the vertex buffer.
		UINT8 *pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		m_vertexBuffer->Unmap(0, nullptr);

		// Initialize the vertex buffer view.
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes = sizeof(Vertex);
		m_vertexBufferView.SizeInBytes = vertexBufferSize;
	}

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
	m_fenceValue = 1;

	// Create an event handle to use for frame synchronization
	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (m_fenceEvent == nullptr)
	{
		ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

	// Wait for the command list to execute
	WaitForPreviousFrame();
}

void DX12HelloTriangle::PopulateCommandList()
{
	// Technically fences should be used here
	// to determine GPU execution progress
	ThrowIfFailed(m_commandAllocator->Reset());

	// However, when using ExecuteCommandList(),
	// command list can be reset at any time and must
	// be before re-recording
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

	// Set necessary state
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicates that the back buffer will be used as a render target
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	m_commandList->ResourceBarrier(1, &barrier);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
		m_frameIndex,
		m_rtvDescriptorSize);

	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// Record commands
	if (m_raster)
	{
		std::vector<ID3D12DescriptorHeap *> heaps = {m_constHeap.Get()};
		m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

		m_commandList->SetGraphicsRootDescriptorTable(0, m_constHeap->GetGPUDescriptorHandleForHeapStart());

		const float clearCoat[] = {0.f, 0.2f, 0.4f, 1.0f};
		m_commandList->ClearRenderTargetView(rtvHandle, clearCoat, 0, nullptr);
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		m_commandList->DrawInstanced(3, 1, 0, 0);
	}
	else
	{
		// Set desctiptor heaps
		std::vector<ID3D12DescriptorHeap *> heaps = {m_srvUavHeap.Get()};
		m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
		CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
			m_outputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		m_commandList->ResourceBarrier(1, &transition);

		D3D12_DISPATCH_RAYS_DESC desc = {};
		uint32_t rayGenSectionSize = m_sbtHelper.GetRayGenSectionSize();
		desc.RayGenerationShaderRecord.StartAddress = m_sbtStorage->GetGPUVirtualAddress();
		desc.RayGenerationShaderRecord.SizeInBytes = rayGenSectionSize;

		uint32_t missSectionSize = m_sbtHelper.GetMissSectionSize();
		desc.MissShaderTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenSectionSize;
		desc.MissShaderTable.SizeInBytes = missSectionSize;
		desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();

		uint32_t hitGroupSectionSize = m_sbtHelper.GetHitGroupSectionSize();
		desc.HitGroupTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() + rayGenSectionSize + missSectionSize;
		desc.HitGroupTable.SizeInBytes = hitGroupSectionSize;
		desc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();

		desc.Width = m_viewport.Width;
		desc.Height = m_viewport.Height;
		desc.Depth = 1;

		m_commandList->SetPipelineState1(m_rtStateObject.Get());
		m_commandList->DispatchRays(&desc);

		// Change RT output texture state to copy source
		transition = CD3DX12_RESOURCE_BARRIER::Transition(
			m_outputResource.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_SOURCE);

		m_commandList->ResourceBarrier(1, &transition);

		// Change render target state to cody destination
		transition = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_COPY_DEST);

		m_commandList->ResourceBarrier(1, &transition);

		// Copy RT output to render target
		m_commandList->CopyResource(
			m_renderTargets[m_frameIndex].Get(),
			m_outputResource.Get());

		// Change render target state back
		transition = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex].Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_RENDER_TARGET);

		m_commandList->ResourceBarrier(1, &transition);
	}

	// Indicates that the back buffer will be used to present
	barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);

	m_commandList->ResourceBarrier(1, &barrier);

	ThrowIfFailed(m_commandList->Close());
}

void DX12HelloTriangle::WaitForPreviousFrame()
{
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
	// sample illustrates how to use fences for efficient resource usage and to
	// maximize GPU utilization.

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence)
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}

void DX12HelloTriangle::CheckRaytracingSupport()
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	ThrowIfFailed(m_device->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS5,
		&options5, sizeof(options5)));

	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
		throw std::runtime_error("Raytracing is not supported on this device");
}

void DX12HelloTriangle::CreateCameraBuffer()
{
	uint32_t numMatrices = 4; // view, perspective, viewInv, perspectiveInv
	m_cameraBufferSize = numMatrices * sizeof(XMMATRIX);

	// Create constant buffer for all matrices
	m_cameraBuffer = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), m_cameraBufferSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	// Create descriptor heap that will be used for the rasterization
	m_constHeap = nv_helpers_dx12::CreateDescriptorHeap(
		m_device.Get(), 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	// Describe and create the constant buffer view
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = m_cameraBufferSize;

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
		m_constHeap->GetCPUDescriptorHandleForHeapStart();

	m_device->CreateConstantBufferView(&cbvDesc, srvHandle);
}

void DX12HelloTriangle::UpdateCameraBuffer()
{
	std::vector<XMMATRIX> matrices(4);

	XMVECTOR Eye = XMVectorSet(m_cameraEye.x, m_cameraEye.y, m_cameraEye.z, 0.0f);
	XMVECTOR At = XMVectorSet(m_cameraDir.x, m_cameraDir.y, m_cameraDir.z, 0.0f);
	XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	matrices[0] = XMMatrixLookAtRH(Eye, Eye + At, Up);

	float fovAngleY = 45.0f * XM_PI / 180.0f;
	matrices[1] =
		XMMatrixPerspectiveFovRH(fovAngleY, m_aspectRatio, 0.1f, 1000.0f);

	XMVECTOR det;
	matrices[2] = XMMatrixInverse(&det, matrices[0]);
	matrices[3] = XMMatrixInverse(&det, matrices[1]);

	uint8_t *pData;
	ThrowIfFailed(m_cameraBuffer->Map(0, nullptr, (void **)&pData));
	memcpy(pData, matrices.data(), m_cameraBufferSize);
	m_cameraBuffer->Unmap(0, nullptr);
}

void DX12HelloTriangle::OnKeyUp(uint8_t key)
{
	if (key == VK_SPACE)
		m_raster = !m_raster;
}

void DX12HelloTriangle::OnKeyDown(uint8_t key)
{
	glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
	if (key == 0x57) // w
		m_cameraEye += m_cameraDir * 0.1f;
	if (key == 0x53) // s
		m_cameraEye -= m_cameraDir * 0.1f;
	if (key == 0x44) // d
		m_cameraEye += glm::normalize(glm::cross(m_cameraDir, cameraUp)) * 0.1f;
	if (key == 0x41) // a
		m_cameraEye -= glm::normalize(glm::cross(m_cameraDir, cameraUp)) * 0.1f;
}

void DX12HelloTriangle::OnMouseMove(uint8_t wParam, uint32_t lParam)
{
	float xoffset = GET_X_LPARAM(lParam) - m_mousePosX;
	float yoffset = m_mousePosY - GET_Y_LPARAM(lParam);
	m_mousePosX = GET_X_LPARAM(lParam);
	m_mousePosY = GET_Y_LPARAM(lParam);

	const float sensitivity = 0.1f;
	xoffset *= sensitivity;
	yoffset *= sensitivity;

	m_cameraYaw += xoffset;
	m_cameraPitch += yoffset;

	if (m_cameraPitch > 89.0f)
		m_cameraPitch = 89.0f;
	if (m_cameraPitch < -89.0f)
		m_cameraPitch = -89.0f;

	m_cameraDir.x = cos(glm::radians(m_cameraYaw)) * cos(glm::radians(m_cameraPitch));
	m_cameraDir.y = sin(glm::radians(m_cameraPitch));
	m_cameraDir.z = sin(glm::radians(m_cameraYaw)) * cos(glm::radians(m_cameraPitch));
	m_cameraDir = glm::normalize(m_cameraDir);
}

AccelerationStructureBuffers DX12HelloTriangle::CreateBottomLevelAS(std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers)
{
	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;

	for (const auto &buffer : vVertexBuffers)
	{
		bottomLevelAS.AddVertexBuffer(
			buffer.first.Get(), 0,
			buffer.second,
			sizeof(Vertex), 0, 0);
	}

	uint64_t scratchSizeInBytes = 0;
	uint64_t resultSizeInBytes = 0;

	bottomLevelAS.ComputeASBufferSizes(m_device.Get(), false, &scratchSizeInBytes, &resultSizeInBytes);

	AccelerationStructureBuffers buffers;
	buffers.pScratch = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), scratchSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COMMON,
		nv_helpers_dx12::kDefaultHeapProps);

	buffers.pResult = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), resultSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nv_helpers_dx12::kDefaultHeapProps);

	bottomLevelAS.Generate(
		m_commandList.Get(),
		buffers.pScratch.Get(),
		buffers.pResult.Get(),
		false, nullptr);

	return buffers;
}

void DX12HelloTriangle::CreateTopLevelAS(const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> &instances)
{
	for (int i = 0; i < instances.size(); i++)
	{
		m_topLevelASGenerator.AddInstance(
			instances[i].first.Get(),
			instances[i].second,
			static_cast<uint32_t>(i),
			static_cast<uint32_t>(0));
	}

	uint64_t scratchSize, resultSize, instanceDescsSize = {0};

	m_topLevelASGenerator.ComputeASBufferSizes(m_device.Get(), true, &scratchSize, &resultSize, &instanceDescsSize);

	m_topLevelASBuffers.pScratch = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), scratchSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nv_helpers_dx12::kDefaultHeapProps);

	m_topLevelASBuffers.pResult = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), resultSize,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nv_helpers_dx12::kDefaultHeapProps);

	m_topLevelASBuffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), instanceDescsSize,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nv_helpers_dx12::kUploadHeapProps);

	m_topLevelASGenerator.Generate(
		m_commandList.Get(),
		m_topLevelASBuffers.pScratch.Get(),
		m_topLevelASBuffers.pResult.Get(),
		m_topLevelASBuffers.pInstanceDesc.Get());
}

void DX12HelloTriangle::CreateAccelerationStructures()
{
	AccelerationStructureBuffers blasBuffers = CreateBottomLevelAS({{m_vertexBuffer.Get(), 3}});

	m_instances = {{blasBuffers.pResult, XMMatrixIdentity()}};
	CreateTopLevelAS(m_instances);

	// Flush the command list and wait for it to finish
	m_commandList->Close();
	ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
	m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
	m_fenceValue++;
	m_commandQueue->Signal(m_fence.Get(), m_fenceValue);

	m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
	WaitForSingleObject(m_fenceEvent, INFINITE);

	// Once the command list is finished executing, reset it to be reused for rendering
	ThrowIfFailed(
		m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

	// Store AS buffers
	m_bottomLevelAS = blasBuffers.pResult;
}

ComPtr<ID3D12RootSignature> DX12HelloTriangle::CreateGenSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	rsc.AddHeapRangesParameter({{
									0,								 // u0
									1,								 // 1 descriptor
									0,								 // use implicit register space 0
									D3D12_DESCRIPTOR_RANGE_TYPE_UAV, // UAV representing the output buffer
									0								 // heap slot where the UAV defined
								},
								{
									0,								 // t0
									1,								 // 1 descriptor
									0,								 // use implicit register space 0
									D3D12_DESCRIPTOR_RANGE_TYPE_SRV, // Top-level acceleration structure
									1								 // heap slot
								},
								{
									0,								 // b0
									1,								 // 1 descriptor
									0,								 // use implicit register space 0
									D3D12_DESCRIPTOR_RANGE_TYPE_CBV, // Camera constant buffer view
									2								 // heap slot
								}});

	return rsc.Generate(m_device.Get(), true);
}

// -----------------------------------------------
// Root signature of the miss shader is empty since
// this shader only communicates through the ray payload
//
ComPtr<ID3D12RootSignature> DX12HelloTriangle::CreateMissSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	return rsc.Generate(m_device.Get(), true);
}

ComPtr<ID3D12RootSignature> DX12HelloTriangle::CreateHitSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV);
	return rsc.Generate(m_device.Get(), true);
}

void DX12HelloTriangle::CreateRaytracingPipeline()
{
	nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());

	// Compile and add libraries
	m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"assets/shaders/RayGen.hlsl");
	m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"assets/shaders/Miss.hlsl");
	m_hitLibrary = nv_helpers_dx12::CompileShaderLibrary(L"assets/shaders/Hit.hlsl");

	// Semantic is given in HLSL
	pipeline.AddLibrary(m_rayGenLibrary.Get(), {L"RayGen"});
	pipeline.AddLibrary(m_missLibrary.Get(), {L"Miss"});
	pipeline.AddLibrary(m_hitLibrary.Get(), {L"ClosestHit"});

	// Create root signatures, to define shader external inputs
	m_rayGenSignature = CreateGenSignature();
	m_missSignature = CreateMissSignature();
	m_hitSignature = CreateHitSignature();

	// 3 different shaders can be invoked to obtain intersection:
	// Intersection - called when hitting bb of non-triangylar geometry,
	// Any-hit shader - called on potential intersections. This shader can,
	// for example, perform alpha-testing and discard some intersections.
	// Closest-hit shader - is invoked on the intersection point closest to
	// the ray origin. Those 3 shaders are bound together into a hit group.

	// For triangular geometry the intersection shader is built-in. An
	// empty any-hit shader is also defined by default, so for now
	// hit group contains only the closest hit shader. Shaders are
	// referred to by name.

	// Hit group for the triangles, with a shader simply interpolating vertex
	// colors
	pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");

	// The following section associates the root signature to each shader.
	// Some shaders share the same root signature (eg. Miss and ShadowMiss).
	// The hit shaders are now only referred to as hit groups, meaning that
	// the underlying intersection, any-hit and closest-hit shaders share the
	// same root signature.
	pipeline.AddRootSignatureAssociation(m_rayGenSignature.Get(), {L"RayGen"});
	pipeline.AddRootSignatureAssociation(m_missSignature.Get(), {L"Miss"});
	pipeline.AddRootSignatureAssociation(m_hitSignature.Get(), {L"HitGroup"});

	// The payload size defines the maximum size of the data carried by the rays,
	// e.g. the data exchanged between the shaders (HitInfo).
	pipeline.SetMaxPayloadSize(4 * sizeof(float)); // RGB + distance

	// The attribute size defines the max size of the hit shader attributes
	pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coords

	// Set requcursion depth - for now only trace primary rays
	pipeline.SetMaxRecursionDepth(1);

	m_rtStateObject = pipeline.Generate();

	ThrowIfFailed(m_rtStateObject->QueryInterface(IID_PPV_ARGS(&m_rtStateObjectProps)));
}

void DX12HelloTriangle::CreateRaytracingOutputBuffer()
{
	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	// For accuracy we should convert to sRGB in the shader
	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Width = m_viewport.Width;
	resDesc.Height = m_viewport.Height;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;

	ThrowIfFailed(m_device->CreateCommittedResource(
		&nv_helpers_dx12::kDefaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		nullptr,
		IID_PPV_ARGS(&m_outputResource)));
}

void DX12HelloTriangle::CreateShaderResourceHeap()
{
	// Create a SRV/UAV/CBV descriptor heap. We need 2 entries - 1 UAV for the
	// raytracing output and 1 SRV for the TLAS
	m_srvUavHeap = nv_helpers_dx12::CreateDescriptorHeap(
		m_device.Get(), 3, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
		m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	m_device->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc,
										srvHandle);

	srvHandle.ptr += m_device->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.RaytracingAccelerationStructure.Location = m_topLevelASBuffers.pResult->GetGPUVirtualAddress();

	m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);

	// Add constant buffer for the camera
	srvHandle.ptr += m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_cameraBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = m_cameraBufferSize;
	m_device->CreateConstantBufferView(&cbvDesc, srvHandle);
}

void DX12HelloTriangle::CreateShaderBindingTable()
{
	m_sbtHelper.Reset();

	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle =
		m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();

	auto heapPointer = reinterpret_cast<void *>(srvUavHeapHandle.ptr);

	m_sbtHelper.AddRayGenerationProgram(L"RayGen", std::vector<void *>{heapPointer});
	m_sbtHelper.AddMissProgram(L"Miss", {});

	auto vertexBufferPointer = reinterpret_cast<void *>(m_vertexBuffer->GetGPUVirtualAddress());

	m_sbtHelper.AddHitGroup(L"HitGroup", std::vector<void *>{vertexBufferPointer});

	uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();

	m_sbtStorage = nv_helpers_dx12::CreateBuffer(
		m_device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

	if (!m_sbtStorage)
		throw std::logic_error("Could not allocate shader binding table");

	m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());
}
