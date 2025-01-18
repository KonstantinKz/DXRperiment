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
#include "DXPipelineHelper.h"
#include "Win32Application.h"

class DXPipeline
{
public:
	DXPipeline(uint32_t viewportWidth, uint32_t viewportHeight, std::wstring name);
	virtual ~DXPipeline();

	virtual void OnInit() = 0;
	virtual void OnUpdate() = 0;
	virtual void OnRender() = 0;
	virtual void OnDestroy() = 0;
	virtual void OnKeyDown(uint8_t key);
	virtual void OnKeyUp(uint8_t key);

	uint32_t GetViewportWidth() const { return m_width; }
	uint32_t GetViewportHeight() const { return m_height; }
	const WCHAR* GetTitle() const { return m_title.c_str(); }

	void ParseCommandLineArgs(_In_reads_(argc) WCHAR* argv[], int argc);

protected:
	std::wstring GetAssetFullPath(LPCWSTR assetName);
	void GetHardwareAdapter(_In_ IDXGIFactory2* pFactory, _Outptr_result_maybenull_ IDXGIAdapter1** ppAdapter);
	void SetCustomWindowText(LPCWSTR text);

	float m_aspectRatio;
	
	// Adapter info
	bool m_useWarpDevice;

	uint32_t m_width;
	uint32_t m_height;

private:
	std::wstring m_title;
	std::wstring m_assetsPath;
};