#include "stdafx.h"
#include "DXPipeline.h"

using namespace Microsoft::WRL;

DXPipeline::DXPipeline(uint32_t viewportWidth, uint32_t viewportHeight, std::wstring name) : m_width(viewportWidth),
																							 m_height(viewportHeight),
																							 m_title(name),
																							 m_useWarpDevice(false)
{
	WCHAR assetsPath[512];
	GetAssetsPath(assetsPath, _countof(assetsPath));
	m_assetsPath = assetsPath;
	m_aspectRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
}

DXPipeline::~DXPipeline() {}

void DXPipeline::OnKeyDown(uint8_t key)
{
}

void DXPipeline::OnKeyUp(uint8_t key)
{
}

void DXPipeline::OnMouseMove(uint8_t wParam, uint32_t lParam)
{
}

// Helper fuction for resolving the full path of assets.
std::wstring DXPipeline::GetAssetFullPath(LPCWSTR assetName)
{
	return m_assetsPath + assetName;
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_ void DXPipeline::GetHardwareAdapter(IDXGIFactory2 *pFactory, IDXGIAdapter1 **ppAdapter)
{
	ComPtr<IDXGIAdapter1> adapter;
	*ppAdapter = nullptr;

	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Don't select the Basic Render Driver adapter.
			// If you want a software adapter, pass in "/warp" on the command line.
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the
		// actual device yet.
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}

	*ppAdapter = adapter.Detach();
}

// Helper function for setting the window's title text.
void DXPipeline::SetCustomWindowText(LPCWSTR text)
{
	std::wstring windowText = m_title + L": " + text;
	SetWindowText(Win32Application::GetHwnd(), windowText.c_str());
}

// Helper function for parsing any supplied command line args.
_Use_decl_annotations_ void DXPipeline::ParseCommandLineArgs(WCHAR *argv[], int argc)
{
	for (int i = 1; i < argc; ++i)
	{
		if (_wcsnicmp(argv[i], L"-warp", wcslen(argv[i])) == 0 ||
			_wcsnicmp(argv[i], L"/warp", wcslen(argv[i])) == 0)
		{
			// m_useWarpDevice = true;
			m_title = m_title + L" (WARP)";
		}
	}
}
