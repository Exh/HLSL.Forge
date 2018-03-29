#include "pch.h"

#include "XAML\D3DRenderPanel.h"

#include "Utils\DXTools.h"
#include <iostream>
#include <DirectXMath.h> 
#include <DirectXColors.h> 
#include <math.h> 
#include <ppltasks.h> 
#include <windows.ui.xaml.media.dxinterop.h> 

#include <d3dcompiler.h>
//#pragma comment(lib, "d3dcompiler.lib")

using namespace Microsoft::WRL;
using namespace Platform;
using namespace Windows::ApplicationModel;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Display;
using namespace Windows::System::Threading;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input::Inking;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Interop;
using namespace Concurrency;
using namespace DirectX;
using namespace D2D1;
using namespace DXControls;
using namespace DX;

namespace DXControls 
{
	// �����������
	D3DRenderPanel::D3DRenderPanel() {
		critical_section::scoped_lock lock(m_criticalSection);
		CreateDeviceIndependentResources();
		CreateDeviceResources();
		CreateSizeDependentResources();
	}

	void D3DRenderPanel::CreateSizeDependentResources()
	{
		
		m_renderTargetView = nullptr;
		m_depthStencilView = nullptr;

		DXPanelBase::CreateSizeDependentResources();
		
		// Create a render target view of the swap chain back buffer. 
		ComPtr<ID3D11Texture2D> backBuffer;
		ThrowIfFailed(
			m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))
		);

		// Create render target view. 
		ThrowIfFailed(
			m_d3dDevice->CreateRenderTargetView(
				backBuffer.Get(),
				nullptr,
				&m_renderTargetView)
		);

		// Create and set viewport. 
		D3D11_VIEWPORT viewport = CD3D11_VIEWPORT(
			0.0f,
			0.0f,
			m_renderTargetWidth,
			m_renderTargetHeight
		);

		m_d3dContext->RSSetViewports(1, &viewport);

		// Create depth/stencil buffer descriptor. 
		CD3D11_TEXTURE2D_DESC depthStencilDesc(
			DXGI_FORMAT_D24_UNORM_S8_UINT,
			static_cast<UINT>(m_renderTargetWidth),
			static_cast<UINT>(m_renderTargetHeight),
			1,
			1,
			D3D11_BIND_DEPTH_STENCIL
		);

		// Allocate a 2-D surface as the depth/stencil buffer. 
		ComPtr<ID3D11Texture2D> depthStencil;
		ThrowIfFailed(
			m_d3dDevice->CreateTexture2D(&depthStencilDesc, nullptr, &depthStencil)
		);

		// Create depth/stencil view based on depth/stencil buffer. 
		const CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = CD3D11_DEPTH_STENCIL_VIEW_DESC(D3D11_DSV_DIMENSION_TEXTURE2D);
		ThrowIfFailed(
			m_d3dDevice->CreateDepthStencilView(
				depthStencil.Get(),
				&depthStencilViewDesc,
				&m_depthStencilView
			)
		);
		
	}

	void D3DRenderPanel::CreateDeviceResources() {
		DXPanelBase::CreateDeviceResources();

		ComPtr<IDXGIFactory1> dxgiFactory;
		ThrowIfFailed(
			CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory))
		);

		ComPtr<IDXGIAdapter> dxgiAdapter;
		ThrowIfFailed(
			dxgiFactory->EnumAdapters(0, &dxgiAdapter)
		);

		ThrowIfFailed(
			dxgiAdapter->EnumOutputs(0, &m_dxgiOutput)
		);
		
		// �������� ���������� �������
		auto loadVSTask = DX::ReadDataAsync(L"DXControls\\VertexShader.cso");
		auto createVSTask = loadVSTask.then([this](const std::vector<byte>& fileData) {
			ThrowIfFailed(
				m_d3dDevice->CreateVertexShader(
					&fileData[0],
					fileData.size(),
					nullptr,
					&m_vertexShader
				)
			);

			static const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};

			ThrowIfFailed(
				m_d3dDevice->CreateInputLayout(
					vertexDesc,
					ARRAYSIZE(vertexDesc),
					&fileData[0],
					fileData.size(),
					&m_inputLayout
				)
			);

			float aspectRatio = m_renderTargetWidth / m_renderTargetHeight;

			float fovAngleY = 70.0f * XM_PI / 180.0f;
			if (aspectRatio < 1.0f)
			{
				fovAngleY /= aspectRatio;
			}

			XMMATRIX perspectiveMatrix = XMMatrixPerspectiveFovRH(
				fovAngleY,
				aspectRatio,
				0.01f,
				100.0f
			);

			XMFLOAT4X4 orientation =
				XMFLOAT4X4(
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					0.0f, 0.0f, 0.0f, 1.0f
				);

			XMMATRIX orientationMatrix = XMLoadFloat4x4(&orientation);

			XMStoreFloat4x4(
				&m_constantBufferData.projection,
				XMMatrixTranspose(perspectiveMatrix * orientationMatrix)
			);
		});

		// Asynchronously load vertex shader and create constant buffer. 
		auto loadPSTask = DX::ReadDataAsync(L"DXControls\\PixelShader.cso");
		auto createPSTask = loadPSTask.then([this](const std::vector<byte>& fileData) {
			ThrowIfFailed(
				m_d3dDevice->CreatePixelShader(
					&fileData[0],
					fileData.size(),
					nullptr,
					&m_pixelShader
				)
			);

			CD3D11_BUFFER_DESC constantBufferDesc(sizeof(ModelViewProjectionConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
			ThrowIfFailed(
				m_d3dDevice->CreateBuffer(
					&constantBufferDesc,
					nullptr,
					&m_constantBuffer
				)
			);
		});

		// Once both shaders are loaded, create the mesh. 
		auto createCubeTask = (createPSTask && createVSTask).then([this]() {

			// Load mesh vertices. Each vertex has a position and a color. 
			static const VertexFull cubeVertices[] =
			{
				// �������� �����
				{ XMFLOAT3( 0.5f,  0.5f,  0.5f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
				{ XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
				{ XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
				{ XMFLOAT3( 0.5f, -0.5f,  0.5f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) },
				// ������ �����
				{ XMFLOAT3( 0.5f,  0.5f, -0.5f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) },
				{ XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
				{ XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
				{ XMFLOAT3( 0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },

				// ������ �����
				{ XMFLOAT3( 0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
				{ XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
				{ XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
				{ XMFLOAT3( 0.5f, -0.5f,  0.5f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) },
				// ������� �����
				{ XMFLOAT3( 0.5f,  0.5f, -0.5f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) },
				{ XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
				{ XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
				{ XMFLOAT3( 0.5f,  0.5f,  0.5f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) },	
				// ����� �����
				{ XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) },
				{ XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
				{ XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
				{ XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
				// ������� �����
				{ XMFLOAT3( 0.5f,  0.5f, -0.5f), XMFLOAT3( 1.0f,  0.0f,  0.0f), XMFLOAT3(1.0f, 0.0f, 1.0f) },
				{ XMFLOAT3( 0.5f,  0.5f,  0.5f), XMFLOAT3( 1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
				{ XMFLOAT3( 0.5f, -0.5f,  0.5f), XMFLOAT3( 1.0f,  0.0f,  0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
				{ XMFLOAT3( 0.5f, -0.5f, -0.5f), XMFLOAT3( 1.0f,  0.0f,  0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
			};

			D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
			vertexBufferData.pSysMem = cubeVertices;
			vertexBufferData.SysMemPitch = 0;
			vertexBufferData.SysMemSlicePitch = 0;
			CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(cubeVertices), D3D11_BIND_VERTEX_BUFFER);
			ThrowIfFailed(
				m_d3dDevice->CreateBuffer(
					&vertexBufferDesc,
					&vertexBufferData,
					&m_vertexBuffer
				)
			);

			// Load mesh indices. Each triple of indices represents 
			// a triangle to be rendered on the screen. 
			// For example, 0,2,1 means that the vertices with indexes 
			// 0, 2 and 1 from the vertex buffer compose the  
			// first triangle of this mesh. 
			static const unsigned short cubeIndices[] =
			{
				 0,  2,  1, // +z
				 0,  3,  2,
				 4,  5,  6, // -z 
				 4,  6,  7,
				 8,  9, 10, // -y 
				 8, 10, 11,
				12, 14, 13, // -y 
				12, 15, 14,
				16, 17, 18, // -x
				16, 18, 19,
				20, 22, 21, // -x
				20, 23, 22,
			};

			m_indexCount = ARRAYSIZE(cubeIndices);

			D3D11_SUBRESOURCE_DATA indexBufferData = { 0 };
			indexBufferData.pSysMem = cubeIndices;
			indexBufferData.SysMemPitch = 0;
			indexBufferData.SysMemSlicePitch = 0;
			CD3D11_BUFFER_DESC indexBufferDesc(sizeof(cubeIndices), D3D11_BIND_INDEX_BUFFER);
			ThrowIfFailed(
				m_d3dDevice->CreateBuffer(
					&indexBufferDesc,
					&indexBufferData,
					&m_indexBuffer
				)
			);
		});

		// Once the cube is loaded, the object is ready to be rendered. 
		createCubeTask.then([this]() {
			m_loadingComplete = true;
		});
		
	}

	void D3DRenderPanel::StartRenderLoop() {		
		// ���� ������ �� ��������� ��� �������, �� �������� ����. 
		if (m_renderLoopWorker != nullptr && m_renderLoopWorker->Status == AsyncStatus::Started)
		{
			return;
		}
		
		// ������� ������ �� ���������
		auto workItemHandler = ref new WorkItemHandler([this](IAsyncAction ^ action)
		{
			while (action->Status == AsyncStatus::Started)
			{
				m_timer.Tick([&]()
				{
					critical_section::scoped_lock lock(m_criticalSection);
					Render();
				});
				m_dxgiOutput->WaitForVBlank();
			}			
		});

		m_renderLoopWorker = ThreadPool::RunAsync(workItemHandler, WorkItemPriority::High, WorkItemOptions::TimeSliced);
		
	}

	void D3DRenderPanel::StopRenderLoop() {
		// Cancel the asynchronous task and let the render thread exit. 
		m_renderLoopWorker->Cancel();
	}

	void D3DRenderPanel::Render() {		
		if (!m_loadingComplete || m_timer.GetFrameCount() == 0)
		{
			return;
		}

		// Eye is at (0,0.7,1.5), looking at point (0,-0.1,0) with the up-vector along the y-axis. 
		static const XMVECTORF32 eye = { 0.0f, 0.7f, 1.5f, 0.0f };
		static const XMVECTORF32 at = { 0.0f, -0.1f, 0.0f, 0.0f };
		static const XMVECTORF32 up = { 0.0f, 1.0f, 0.0f, 0.0f };

		// Convert degrees to radians, then convert seconds to rotation angle 
		float radiansPerSecond = XMConvertToRadians(m_degreesPerSecond);
		double totalRotation = m_timer.GetTotalSeconds() * radiansPerSecond;
		float animRadians = (float)fmod(totalRotation, XM_2PI);

		// Prepare to pass the view matrix, and updated model matrix, to the shader 
		XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixLookAtRH(eye, at, up)));
		XMStoreFloat4x4(&m_constantBufferData.model, XMMatrixTranspose(XMMatrixRotationY(animRadians)));

		// Set render targets to the screen. 
		ID3D11RenderTargetView *const targets[1] = { m_renderTargetView.Get() };
		m_d3dContext->OMSetRenderTargets(1, targets, m_depthStencilView.Get());

		// Clear the back buffer and depth stencil view. 
		m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), DirectX::Colors::CornflowerBlue);
		m_d3dContext->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		// Prepare the constant buffer to send it to the Graphics device. 
		m_d3dContext->UpdateSubresource(
			m_constantBuffer.Get(),
			0,
			NULL,
			&m_constantBufferData,
			0,
			0
		);

		// Each vertex is one instance of the VertexPositionColor struct. 
		UINT stride = sizeof(VertexFull);
		UINT offset = 0;
		m_d3dContext->IASetVertexBuffers(
			0,
			1,
			m_vertexBuffer.GetAddressOf(),
			&stride,
			&offset
		);

		m_d3dContext->IASetIndexBuffer(
			m_indexBuffer.Get(),
			DXGI_FORMAT_R16_UINT, // Each index is one 16-bit unsigned integer (short). 
			0
		);

		m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		m_d3dContext->IASetInputLayout(m_inputLayout.Get());

		// Attach our vertex shader. 
		m_d3dContext->VSSetShader(
			m_vertexShader.Get(),
			nullptr,
			0
		);

		// Send the constant buffer to the Graphics device. 
		m_d3dContext->VSSetConstantBuffers(
			0,
			1,
			m_constantBuffer.GetAddressOf()
		);

		// Attach our pixel shader. 
		m_d3dContext->PSSetShader(
			m_pixelShader.Get(),
			nullptr,
			0
		);

		// Draw the objects. 
		m_d3dContext->DrawIndexed(
			m_indexCount,
			0,
			0
		);

		Present();		
	}

	void D3DRenderPanel::CompilePixelShader(Platform::String ^data)
	{
		const std::string shaderSource = std::string(data->Begin(), data->End());
		
		ComPtr<ID3DBlob> pShaderCode;
		ComPtr<ID3DBlob> errorBlob;

		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_VALIDATION | D3DCOMPILE_SKIP_OPTIMIZATION;

		HRESULT hr = D3DCompile(
			shaderSource.c_str(), 
			shaderSource.length(),
			nullptr,
			nullptr, 
			nullptr,
			"main", 
			"ps_4_0", 
			compileFlags,
			0, 
			&pShaderCode, 
			&errorBlob);

		if (FAILED(hr))
		{
			auto msg = ref new Platform::String((const wchar_t*)errorBlob->GetBufferPointer());
			throw Platform::Exception::CreateException(hr, msg);
		}
		
		if (hr == S_OK) {
			ComPtr<ID3D11PixelShader> pixelShader;
			ThrowIfFailed(m_d3dDevice->CreatePixelShader(
				pShaderCode->GetBufferPointer(),
				pShaderCode->GetBufferSize(),
				nullptr,
				&m_pixelShader));
		}		
	}		
	
	// ����������
	D3DRenderPanel::~D3DRenderPanel() {

	}
}