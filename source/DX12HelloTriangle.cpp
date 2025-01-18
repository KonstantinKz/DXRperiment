#include "stdafx.h"
#include "DX12HelloTriangle.h"
#include "DXPipelineHelper.h"

DX12HelloTriangle::DX12HelloTriangle(uint32_t viewportWidth, uint32_t viewportHeight, std::wstring name) :
	DXPipeline(viewportWidth, viewportHeight, name),
	m_frameIndex(0),
	m_viewport(0.f, 0.f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)),
	m_scissorRect(0.f, 0.f, static_cast<LONG>(viewportWidth), static_cast<LONG>(viewportHeight))
{
}

void DX12HelloTriangle::OnInit()
{
	InitPipelineObjects();
	LoadAssets();
}

void DX12HelloTriangle::OnUpdate()
{
}

void DX12HelloTriangle::OnRender()
{
	PopulateCommandList();

	// Execute command list
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
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

	// TODO: Enable debug layers

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);
		ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
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
		&swapchain
	));
	
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
	// Root signature
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDescription;
	rootSignatureDescription.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	
	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDescription, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
	ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

	// Pipeline state
	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

	// TODO: Enable debug flags
	uint32_t compileFlags = 0;

	ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vertexShader, &error));
	ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(), nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &pixelShader, &error));

	// Define the vertex input layout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// Describe and create the graphics pipeline state object (PSO)
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
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
		IID_PPV_ARGS(&m_commandList)
	));

	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	ThrowIfFailed(m_commandList->Close());

	// Create vertex buffer
	{
		Vertex triangleVertices[] =
		{
			{ { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
			{ { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
			{ { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
		};

		const uint32_t vertexBufferSize = sizeof(triangleVertices);


		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)));

		// Copy the triangle data to the vertex buffer.
		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
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
	// Technically should here fences should be used 
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
	m_commandList->ResourceBarrier(
		1, &CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex].Get(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
		m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), 
		m_frameIndex, 
		m_rtvDescriptorSize
	);

	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// Record commands
	const float clearCoat[] = { 0.f, 0.2f, 0.4f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, clearCoat, 0, nullptr);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	m_commandList->DrawInstanced(3, 1, 0, 0);

	// Indicates that the back buffer will be used to present
	m_commandList->ResourceBarrier(
		1, &CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_frameIndex].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		)
	);

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
