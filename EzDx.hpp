#pragma once

#include <algorithm>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdlib.h>
#include <vector>

#include "d3dx12.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#define DX_ASSERT( status, message )                                                             \
	if ( ( status ) == 0 )                                                                       \
	{                                                                                            \
		char buffer[512];                                                                        \
		snprintf( buffer, sizeof( buffer ), "%s, %s (%d line)\n", message, __FILE__, __LINE__ ); \
		__debugbreak();                                                                          \
	}

template <class T>
class DxPtr
{
public:
	DxPtr() {}
	DxPtr( T* ptr ) : _ptr( ptr )
	{
	}
	DxPtr( const DxPtr<T>& rhs ) : _ptr( rhs._ptr )
	{
		if ( _ptr )
		{
			_ptr->AddRef();
		}
	}
	DxPtr<T>& operator=( const DxPtr<T>& rhs )
	{
		auto p = _ptr;

		if ( rhs._ptr )
		{
			rhs._ptr->AddRef();
		}
		_ptr = rhs._ptr;

		if ( p )
		{
			p->Release();
		}
		return *this;
	}
	~DxPtr()
	{
		if ( _ptr )
		{
			_ptr->Release();
		}
	}
	T* get()
	{
		return _ptr;
	}
	const T* get() const
	{
		return _ptr;
	}
	T* operator->()
	{
		return _ptr;
	}
	T** getAddressOf()
	{
		return &_ptr;
	}
	operator bool()
	{
		return _ptr != nullptr;
	}

private:
	T* _ptr = nullptr;
};

inline void enableDebugLayer()
{
	DxPtr<ID3D12Debug> debugController;
	if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( debugController.getAddressOf() ) ) ) )
	{
		debugController->EnableDebugLayer();
	}
}
inline std::vector<DxPtr<IDXGIAdapter>> getAllAdapters()
{
	HRESULT hr;

	DxPtr<IDXGIFactory7> factory;
	UINT flagsDXGI = DXGI_CREATE_FACTORY_DEBUG;
	hr = CreateDXGIFactory2( flagsDXGI, IID_PPV_ARGS( factory.getAddressOf() ) );
	DX_ASSERT( hr == S_OK, "" );

	std::vector<DxPtr<IDXGIAdapter>> adapters;

	int adapterIndex = 0;
	for ( ;; )
	{
		DxPtr<IDXGIAdapter> adapter;
		hr = factory->EnumAdapters( adapterIndex++, adapter.getAddressOf() );
		if ( hr == S_OK )
		{
			adapters.push_back( adapter );
			continue;
		}
		DX_ASSERT( hr == DXGI_ERROR_NOT_FOUND, "" );
		break;
	};

	return adapters;
}

static void resourceBarrier( ID3D12GraphicsCommandList* commandList, std::vector<D3D12_RESOURCE_BARRIER> barrier )
{
	commandList->ResourceBarrier( barrier.size(), barrier.data() );
}

/*
example)
(0   + 255) & 0xFFFFFF00 = 0
(1   + 255) & 0xFFFFFF00 = 256
(255 + 255) & 0xFFFFFF00 = 256
(256 + 255) & 0xFFFFFF00 = 256
(257 + 255) & 0xFFFFFF00 = 512
(258 + 255) & 0xFFFFFF00 = 512
*/
inline uint32_t constantBufferSize( uint32_t bytes )
{
	return ( bytes + 255 ) & 0xFFFFFF00;
}

inline D3D12_CPU_DESCRIPTOR_HANDLE add( D3D12_CPU_DESCRIPTOR_HANDLE h, int offset )
{
	h.ptr += offset;
	return h;
}
inline int64_t dispatchsize( int64_t n, int64_t threads )
{
	return ( n + threads - 1 ) / threads;
}

class FenceObject
{
public:
	FenceObject( const FenceObject& ) = delete;
	void operator=( const FenceObject& ) = delete;

	FenceObject( ID3D12Device* device, ID3D12CommandQueue* queue )
	{
		HRESULT hr;
		hr = device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( _fence.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
		hr = queue->Signal( _fence.get(), 1 );
		DX_ASSERT( hr == S_OK, "" );
	}
	void wait()
	{
		HANDLE e = CreateEvent( nullptr, false, false, nullptr );
		_fence->SetEventOnCompletion( 1, e );
		WaitForSingleObject( e, INFINITE );
		CloseHandle( e );
	}

private:
	DxPtr<ID3D12Fence> _fence;
};
class CommandObject
{
public:
	CommandObject( const CommandObject& ) = delete;
	void operator=( const CommandObject& ) = delete;

	CommandObject( ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, const char* name )
	{
		HRESULT hr;
		hr = device->CreateCommandAllocator( type, IID_PPV_ARGS( _allocator.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );

		hr = device->CreateCommandList(
			0,
			type,
			_allocator.get(),
			nullptr, /* pipeline state */
			IID_PPV_ARGS( _list.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
	}
	ID3D12GraphicsCommandList* list()
	{
		return _list.get();
	}

	void storeCommand( std::function<void( ID3D12GraphicsCommandList* commandList )> f )
	{
		if ( _isClosed )
		{
			_list->Reset( _allocator.get(), nullptr );
		}
		f( _list.get() );
		_list->Close();
		_isClosed = true;
	}
	void storeCommand( std::function<void( ID3D12GraphicsCommandList* lhs, ID3D12GraphicsCommandList* rhs )> f, CommandObject* rhsCommand )
	{
		if ( _isClosed )
		{
			_list->Reset( _allocator.get(), nullptr );
		}
		rhsCommand->storeCommand( [&]( ID3D12GraphicsCommandList* rhs ) {
			f( _list.get(), rhs );
		} );
		_list->Close();
		_isClosed = true;
	}

private:
	bool _isClosed = false;
	DxPtr<ID3D12CommandAllocator> _allocator;
	DxPtr<ID3D12GraphicsCommandList> _list;
};

class QueueObject
{
public:
	QueueObject( const QueueObject& ) = delete;
	void operator=( const QueueObject& ) = delete;

	QueueObject( ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, const char* name )
	{
		HRESULT hr;

		D3D12_COMMAND_QUEUE_DESC desc_command_queue = {};
		desc_command_queue.Type = type;
		desc_command_queue.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
		desc_command_queue.Flags = D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;
		desc_command_queue.NodeMask = 0;
		hr = device->CreateCommandQueue( &desc_command_queue, IID_PPV_ARGS( _queue.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
	}
	ID3D12CommandQueue* queue()
	{
		return _queue.get();
	}
	void execute( CommandObject* commandObject )
	{
		ID3D12CommandList* const command[] = {commandObject->list()};
		_queue->ExecuteCommandLists( 1, command );
	}
	std::shared_ptr<FenceObject> fence( ID3D12Device* device )
	{
		return std::shared_ptr<FenceObject>( new FenceObject( device, _queue.get() ) );
	}

private:
	DxPtr<ID3D12CommandQueue> _queue;
};

class DeviceObject
{
public:
	DeviceObject(const DeviceObject&) = delete;
	void operator=(const DeviceObject&) = delete;

	DeviceObject( IDXGIAdapter* adapter )
	{
		HRESULT hr;

		DXGI_ADAPTER_DESC d;
		hr = adapter->GetDesc( &d );
		DX_ASSERT( hr == S_OK, "" );

		_deviceName = d.Description;

		hr = D3D12CreateDevice( adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS( _device.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );

		D3D12_FEATURE_DATA_SHADER_MODEL shaderModelFeature = {};
		shaderModelFeature.HighestShaderModel = D3D_SHADER_MODEL_6_5;
		hr = _device->CheckFeatureSupport( D3D12_FEATURE_SHADER_MODEL, &shaderModelFeature, sizeof( shaderModelFeature ) );
		DX_ASSERT( hr == S_OK, "" );

		std::map<D3D_SHADER_MODEL, std::string> sm_to_s =
			{
				{D3D_SHADER_MODEL_5_1, "D3D_SHADER_MODEL_5_1"},
				{D3D_SHADER_MODEL_6_0, "D3D_SHADER_MODEL_6_0"},
				{D3D_SHADER_MODEL_6_1, "D3D_SHADER_MODEL_6_1"},
				{D3D_SHADER_MODEL_6_2, "D3D_SHADER_MODEL_6_2"},
				{D3D_SHADER_MODEL_6_3, "D3D_SHADER_MODEL_6_3"},
				{D3D_SHADER_MODEL_6_4, "D3D_SHADER_MODEL_6_4"},
				{D3D_SHADER_MODEL_6_5, "D3D_SHADER_MODEL_6_5"},
			};
		_highestShaderModel = sm_to_s[shaderModelFeature.HighestShaderModel];

		_queue = std::shared_ptr<QueueObject>( new QueueObject( _device.get(), D3D12_COMMAND_LIST_TYPE_DIRECT, "Compute" ) );

		DxPtr<IDXGIFactory4> pDxgiFactory;
		hr = CreateDXGIFactory1( __uuidof( IDXGIFactory1 ), (void**)pDxgiFactory.getAddressOf() );
		DX_ASSERT( hr == S_OK, "" );

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Width = 64;
		swapChainDesc.Height = 64;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.SampleDesc.Count = 1;

		// Create the swap chain
		hr = pDxgiFactory->CreateSwapChainForComposition( _queue->queue(), &swapChainDesc, nullptr, _swapchain.getAddressOf() );
		DX_ASSERT( hr == S_OK, "" );
	}
	ID3D12Device* device()
	{
		return _device.get();
	}
	QueueObject* queueObject()
	{
		return _queue.get();
	}

	void present()
	{
		HRESULT hr;
		hr = _swapchain->Present( 1, 0 );
		DX_ASSERT( hr == S_OK, "" );

		std::shared_ptr<FenceObject> fence = _queue->fence( _device.get() );
		fence->wait();
	}
	std::wstring deviceName()
	{
		return _deviceName;
	}

private:
	std::wstring _deviceName;
	std::string _highestShaderModel;
	DxPtr<ID3D12Device> _device;
	std::shared_ptr<QueueObject> _queue;
	DxPtr<IDXGISwapChain1> _swapchain;
};

class UploaderObject
{
public:
	UploaderObject( const UploaderObject& ) = delete;
	void operator=( const UploaderObject& ) = delete;

	UploaderObject( ID3D12Device* device, int64_t bytes ) : _bytes( std::max( bytes, 1LL ) )
	{
		HRESULT hr;
		hr = device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer( _bytes ),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS( _resource.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
	}
	~UploaderObject()
	{
	}

	void map( std::function<void( void* p )> f )
	{
		D3D12_RANGE readrange = {};
		D3D12_RANGE writerange = {0, _bytes};

		void* p;
		HRESULT hr;
		hr = _resource->Map( 0, &readrange, &p );
		DX_ASSERT( hr == S_OK, "" );

		f( p );

		_resource->Unmap( 0, &writerange );
	}
	int64_t bytes()
	{
		return _bytes;
	}
	ID3D12Resource* resource()
	{
		return _resource.get();
	}

private:
	int64_t _bytes;
	DxPtr<ID3D12Resource> _resource;
};

class DownloaderObject
{
public:
	DownloaderObject( const DownloaderObject& ) = delete;
	void operator=( const DownloaderObject& ) = delete;

	DownloaderObject( ID3D12Device* device, int64_t bytes ) : _bytes( std::max( bytes, 1LL ) )
	{
		HRESULT hr;
		hr = device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_READBACK ),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer( bytes ),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS( _resource.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
	}
	~DownloaderObject()
	{
	}
	void map( std::function<void( void* p )> f )
	{
		D3D12_RANGE readrange = {0, _bytes};
		D3D12_RANGE writerange = {};

		void* p;
		HRESULT hr;
		hr = _resource->Map( 0, &readrange, &p );
		DX_ASSERT( hr == S_OK, "" );

		f( p );

		_resource->Unmap( 0, &writerange );
	}
	int64_t bytes()
	{
		return _bytes;
	}
	ID3D12Resource* resource()
	{
		return _resource.get();
	}

private:
	int64_t _bytes;
	DxPtr<ID3D12Resource> _resource;
};

class BufferObjectUAV
{
public:
	BufferObjectUAV( const BufferObjectUAV& ) = delete;
	void operator=( const BufferObjectUAV& ) = delete;

	BufferObjectUAV( ID3D12Device* device, int64_t bytes, int64_t structureByteStride, D3D12_RESOURCE_STATES initialState )
		: _bytes( std::max( bytes, 1LL ) ), _structureByteStride( structureByteStride )
	{
		HRESULT hr;
		hr = device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
			D3D12_HEAP_FLAG_NONE /* I don't know */,
			&CD3DX12_RESOURCE_DESC::Buffer( _bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ),
			initialState,
			nullptr,
			IID_PPV_ARGS( _resource.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
	}
	int64_t bytes() const
	{
		return _bytes;
	}
	D3D12_RESOURCE_BARRIER resourceBarrierUAV()
	{
		return CD3DX12_RESOURCE_BARRIER::UAV( _resource.get() );
	}
	D3D12_RESOURCE_BARRIER resourceBarrierTransition( D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to )
	{
		return CD3DX12_RESOURCE_BARRIER::Transition( _resource.get(), from, to );
	}
	ID3D12Resource* resource()
	{
		return _resource.get();
	}
	void copyFrom( ID3D12GraphicsCommandList* commandList, UploaderObject* uploader )
	{
		DX_ASSERT( _bytes == uploader->bytes(), "Required the same number of bytes" );
		commandList->CopyBufferRegion(
			_resource.get(), 0,
			uploader->resource(), 0,
			_bytes );
	}
	void copyTo( ID3D12GraphicsCommandList* commandList, DownloaderObject* downloader )
	{
		DX_ASSERT( _bytes == downloader->bytes(), "Required the same number of bytes" );
		commandList->CopyBufferRegion(
			downloader->resource(), 0,
			_resource.get(), 0,
			_bytes );
	}
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDescription() const
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC d = {};
		d.Format = DXGI_FORMAT_UNKNOWN;
		d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		d.Buffer.FirstElement = 0;
		d.Buffer.NumElements = _bytes / _structureByteStride;
		d.Buffer.StructureByteStride = _structureByteStride;
		d.Buffer.CounterOffsetInBytes = 0;
		return d;
	}

private:
	int64_t _bytes;
	int64_t _structureByteStride;
	DxPtr<ID3D12Resource> _resource;
};

class ConstantBufferObject
{
public:
	ConstantBufferObject(const ConstantBufferObject&) = delete;
	void operator=(const ConstantBufferObject&) = delete;

	ConstantBufferObject( ID3D12Device* device, int64_t bytes, D3D12_RESOURCE_STATES initialState )
		: _bytes( constantBufferSize( std::max( bytes, 1LL ) ) )
	{
		HRESULT hr;
		hr = device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
			D3D12_HEAP_FLAG_NONE /* I don't know */,
			&CD3DX12_RESOURCE_DESC::Buffer( _bytes ),
			initialState,
			nullptr,
			IID_PPV_ARGS( _resource.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
		_uploader = std::unique_ptr<UploaderObject>( new UploaderObject( device, _bytes ) );
	}
	D3D12_RESOURCE_BARRIER resourceBarrierTransition( D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to )
	{
		return CD3DX12_RESOURCE_BARRIER::Transition( _resource.get(), from, to );
	}

	template <class T>
	void upload( ID3D12GraphicsCommandList* commandList, T value )
	{
		static_assert( std::is_trivial<T>::value == true, "T should trivial type" );
		DX_ASSERT( sizeof( T ) <= _bytes, "bad size" );

		_uploader->map( [&]( void* p ) {
			memcpy( p, &value, sizeof( T ) );
		} );
		commandList->CopyBufferRegion(
			_resource.get(), 0,
			_uploader->resource(), 0,
			sizeof( T ) );
	}
	ID3D12Resource* resource()
	{
		return _resource.get();
	}

private:
	int64_t _bytes = 0;
	DxPtr<ID3D12Resource> _resource;
	std::unique_ptr<UploaderObject> _uploader;
};

struct DescriptorEntity
{
	char type = 0;
	int registerIndex = 0;
	int descriptorHeapIndex = 0;
};

class DescriptorHeapObject
{
public:
	DescriptorHeapObject( ID3D12Device* device, std::vector<DescriptorEntity> bufferDescriptorEntries )
		: _bufferDescriptorEntries( bufferDescriptorEntries )
	{
		HRESULT hr;
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = _bufferDescriptorEntries.size();
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		hr = device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( _bufferHeap.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
	}
	void u( ID3D12Device* device, int i, ID3D12Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC uavdescription )
	{
		bool found = false;
		for ( DescriptorEntity e : _bufferDescriptorEntries )
		{
			if ( e.type == 'u' && e.registerIndex == i )
			{
				found = true;

				int increment = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
				device->CreateUnorderedAccessView( resource, nullptr, &uavdescription, add( _bufferHeap->GetCPUDescriptorHandleForHeapStart(), increment * e.descriptorHeapIndex ) );
				break;
			}
		}
		DX_ASSERT( found, "" );
	}
	void b( ID3D12Device* device, int i, ID3D12Resource* resource )
	{
		bool found = false;
		for ( DescriptorEntity e : _bufferDescriptorEntries )
		{
			if ( e.type == 'b' && e.registerIndex == i )
			{
				found = true;

				int increment = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

				D3D12_CONSTANT_BUFFER_VIEW_DESC d = {};
				d.BufferLocation = resource->GetGPUVirtualAddress();
				d.SizeInBytes = resource->GetDesc().Width;
				device->CreateConstantBufferView( &d, add( _bufferHeap->GetCPUDescriptorHandleForHeapStart(), increment * e.descriptorHeapIndex ) );
				break;
			}
		}
		DX_ASSERT( found, "" );
	}
	ID3D12DescriptorHeap* bufferHeap()
	{
		return _bufferHeap.get();
	}

private:
	std::vector<DescriptorEntity> _bufferDescriptorEntries;
	DxPtr<ID3D12DescriptorHeap> _bufferHeap;
};

class FileBlob
{
public:
	FileBlob( const char* file )
	{
		FILE* fp = fopen( file, "rb" );
		DX_ASSERT( fp, "failed to load file." );

		fseek( fp, 0, SEEK_END );

		_data.resize( ftell( fp ) );

		fseek( fp, 0, SEEK_SET );

		size_t s = fread( _data.data(), 1, _data.size(), fp );
		DX_ASSERT( s == _data.size(), "failed to load file." );

		fclose( fp );
		fp = nullptr;
	}
	const uint8_t* data() const
	{
		return _data.data();
	}
	std::size_t size() const
	{
		return _data.size();
	}

private:
	std::vector<uint8_t> _data;
};

class ComputeObject
{
public:
	void u( int i )
	{
		DX_ASSERT( !_signature, "" );
		DX_ASSERT( !_pipelineState, "" );
		DescriptorEntity entity;
		entity.type = 'u';
		entity.registerIndex = i;
		entity.descriptorHeapIndex = (int)_bufferDescriptorEntries.size();
		_bufferDescriptorEntries.push_back( entity );
	}
	void b( int i )
	{
		DX_ASSERT( !_signature, "" );
		DX_ASSERT( !_pipelineState, "" );
		DescriptorEntity entity;
		entity.type = 'b';
		entity.registerIndex = i;
		entity.descriptorHeapIndex = (int)_bufferDescriptorEntries.size();
		_bufferDescriptorEntries.push_back( entity );
	}

	void loadShaderAndBuild( ID3D12Device* device, const char* shaderFile )
	{
		std::vector<D3D12_DESCRIPTOR_RANGE> bufferDescriptorRanges;
		for ( int i = 0; i < _bufferDescriptorEntries.size(); ++i )
		{
			DescriptorEntity entity = _bufferDescriptorEntries[i];

			D3D12_DESCRIPTOR_RANGE range = {};
			switch ( entity.type )
			{
			case 'u':
				range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
				break;
			case 'b':
				range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
				break;
			default:
				DX_ASSERT( 0, "" );
			}

			range.NumDescriptors = 1;
			range.BaseShaderRegister = entity.registerIndex;
			range.RegisterSpace = 0;
			range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
			bufferDescriptorRanges.push_back( range );
		}
		D3D12_ROOT_PARAMETER rootParameter = {};
		rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameter.DescriptorTable.NumDescriptorRanges = _bufferDescriptorEntries.size();
		rootParameter.DescriptorTable.pDescriptorRanges = bufferDescriptorRanges.data();

		// Signature
		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = CD3DX12_ROOT_SIGNATURE_DESC( 1, &rootParameter );
		DxPtr<ID3DBlob> pSignature;
		HRESULT hr;
		hr = D3D12SerializeRootSignature( &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, pSignature.getAddressOf(), nullptr );
		DX_ASSERT( hr == S_OK, "" );

		_signature = DxPtr<ID3D12RootSignature>();
		hr = device->CreateRootSignature( 0, pSignature->GetBufferPointer(), pSignature->GetBufferSize(), IID_PPV_ARGS( _signature.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );

		FileBlob shaderblob( shaderFile );
		// DxPtr<ID3DBlob> cso;
		// hr = D3DReadFileToBlob( shaderFile.c_str(), cso.getAddressOf() );
		DX_ASSERT( hr == S_OK, "" );
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
			desc.CS.pShaderBytecode = shaderblob.data();
			desc.CS.BytecodeLength = shaderblob.size();
			desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
			desc.NodeMask = 0;
			desc.pRootSignature = _signature.get();
			_pipelineState = DxPtr<ID3D12PipelineState>();
			hr = device->CreateComputePipelineState( &desc, IID_PPV_ARGS( _pipelineState.getAddressOf() ) );
			DX_ASSERT( hr == S_OK, "" );
		}
	}
	std::shared_ptr<DescriptorHeapObject> createDescriptorHeap( ID3D12Device* device )
	{
		return std::shared_ptr<DescriptorHeapObject>( new DescriptorHeapObject( device, _bufferDescriptorEntries ) );
	}

	void setPipelineState( ID3D12GraphicsCommandList* commandList )
	{
		commandList->SetPipelineState( _pipelineState.get() );
	}
	void setComputeRootSignature( ID3D12GraphicsCommandList* commandList )
	{
		commandList->SetComputeRootSignature( _signature.get() );
	}
	void assignDescriptorHeap( ID3D12GraphicsCommandList* commandList, DescriptorHeapObject* descriptorHeapObject )
	{
		ID3D12DescriptorHeap* const heaps[] = {descriptorHeapObject->bufferHeap()};
		commandList->SetDescriptorHeaps( 1, heaps );
		commandList->SetComputeRootDescriptorTable( 0, descriptorHeapObject->bufferHeap()->GetGPUDescriptorHandleForHeapStart() );
	}

	void dispatch( ID3D12GraphicsCommandList* commandList, int64_t x, int64_t y, int64_t z )
	{
		commandList->Dispatch( x, y, z );
	}

private:
	std::vector<DescriptorEntity> _bufferDescriptorEntries;

	DxPtr<ID3D12RootSignature> _signature;
	DxPtr<ID3D12PipelineState> _pipelineState;
};
