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
inline D3D12_GPU_DESCRIPTOR_HANDLE add( D3D12_GPU_DESCRIPTOR_HANDLE h, int offset )
{
	h.ptr += offset;
	return h;
}
inline int64_t dispatchsize( int64_t n, int64_t threads )
{
	return ( n + threads - 1 ) / threads;
}
inline uint64_t alignPointer( uint64_t p, uint64_t alignment )
{
	uint64_t m = p % alignment;
	if ( m == 0 )
	{
		return p;
	}
	return p + alignment - m;
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

	CommandObject( ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type )
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

	void setName( std::wstring name )
	{
		_list->SetName( name.c_str() );
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
		ID3D12CommandList* const command[] = { commandObject->list() };
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
	DeviceObject( const DeviceObject& ) = delete;
	void operator=( const DeviceObject& ) = delete;

	DeviceObject( IDXGIAdapter* adapter )
	{
		HRESULT hr;

		DXGI_ADAPTER_DESC d;
		hr = adapter->GetDesc( &d );
		DX_ASSERT( hr == S_OK, "" );

		_deviceName = d.Description;

		struct DeviceIID
		{
			IID iid;
			const char* type;
		};
#define DEVICE_VER( type )      \
	{                           \
		__uuidof( type ), #type \
	}
		const DeviceIID deviceIIDs[] = {
			DEVICE_VER( ID3D12Device8 ),
			DEVICE_VER( ID3D12Device7 ),
			DEVICE_VER( ID3D12Device6 ),
			DEVICE_VER( ID3D12Device5 ),
			DEVICE_VER( ID3D12Device4 ),
			DEVICE_VER( ID3D12Device3 ),
			DEVICE_VER( ID3D12Device2 ),
			DEVICE_VER( ID3D12Device1 ),
		};
#undef DEVICE_VER

		for ( auto deviceIID : deviceIIDs )
		{
			hr = D3D12CreateDevice( adapter, D3D_FEATURE_LEVEL_12_0, deviceIID.iid, (void**)_device.getAddressOf() );
			if ( hr == S_OK )
			{
				_deviceIIDType = deviceIID.type;
				break;
			}
		}
		DX_ASSERT( hr == S_OK, "" );

		D3D12_FEATURE_DATA_SHADER_MODEL shaderModelFeature = {};
		shaderModelFeature.HighestShaderModel = D3D_SHADER_MODEL_6_5;
		hr = _device->CheckFeatureSupport( D3D12_FEATURE_SHADER_MODEL, &shaderModelFeature, sizeof( shaderModelFeature ) );
		DX_ASSERT( hr == S_OK, "" );

		std::map<D3D_SHADER_MODEL, std::string> sm_to_s =
			{
				{ D3D_SHADER_MODEL_5_1, "D3D_SHADER_MODEL_5_1" },
				{ D3D_SHADER_MODEL_6_0, "D3D_SHADER_MODEL_6_0" },
				{ D3D_SHADER_MODEL_6_1, "D3D_SHADER_MODEL_6_1" },
				{ D3D_SHADER_MODEL_6_2, "D3D_SHADER_MODEL_6_2" },
				{ D3D_SHADER_MODEL_6_3, "D3D_SHADER_MODEL_6_3" },
				{ D3D_SHADER_MODEL_6_4, "D3D_SHADER_MODEL_6_4" },
				{ D3D_SHADER_MODEL_6_5, "D3D_SHADER_MODEL_6_5" },
			};
		_highestShaderModel = sm_to_s[shaderModelFeature.HighestShaderModel];

		D3D12_FEATURE_DATA_D3D12_OPTIONS1 option1 = {};
		hr = _device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS1, &option1, sizeof( option1 ) );
		DX_ASSERT( hr == S_OK, "" );
		_waveLaneCount = option1.WaveLaneCountMin;
		_totalLaneCount = option1.TotalLaneCount;

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
	std::wstring deviceName() const
	{
		return _deviceName;
	}
	int waveLaneCount() const
	{
		return _waveLaneCount;
	}
	int totalLaneCount() const
	{
		return _totalLaneCount;
	}

private:
	std::string _deviceIIDType;
	std::wstring _deviceName;
	std::string _highestShaderModel;
	int _waveLaneCount = 0;
	int _totalLaneCount = 0;
	DxPtr<ID3D12Device> _device;
	std::shared_ptr<QueueObject> _queue;
	DxPtr<IDXGISwapChain1> _swapchain;
};

class IResourceAcceptor
{
public:
	virtual ~IResourceAcceptor() {}
	virtual void accept( DxPtr<ID3D12Resource> resource ) = 0;
};

class BatchHeapAllocator
{
public:
	void requestDefaultUAV( IResourceAcceptor* acceptor, uint64_t bytes, D3D12_RESOURCE_STATES initialState )
	{
		DX_ASSERT( _allocated == false, "" );

		_bytesDefault = alignPointer( _bytesDefault, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT );

		Allocation allocation = {};
		allocation.heapOffset = _bytesDefault;
		allocation.bytes = bytes;
		allocation.initialState = initialState;
		allocation.resourceDesc = CD3DX12_RESOURCE_DESC::Buffer( bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
		allocation.acceptor = acceptor;
		_allocationDefault.push_back( allocation );

		_bytesDefault += bytes;
	}
	void allocate( ID3D12Device* device )
	{
		DX_ASSERT( _allocated == false, "" );
		_allocated = true;

		HRESULT hr;
		hr = device->CreateHeap( &CD3DX12_HEAP_DESC( _bytesDefault, D3D12_HEAP_TYPE_DEFAULT ), IID_PPV_ARGS( _heapDefault.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );

		for ( int i = 0; i < _allocationDefault.size(); ++i )
		{
			DxPtr<ID3D12Resource> resource;
			hr = device->CreatePlacedResource(
				_heapDefault.get(),
				_allocationDefault[i].heapOffset,
				&_allocationDefault[i].resourceDesc,
				_allocationDefault[i].initialState,
				nullptr,
				IID_PPV_ARGS( resource.getAddressOf() ) );
			DX_ASSERT( hr == S_OK, "" );
			_allocationDefault[i].acceptor->accept( resource );
			_resources.push_back( resource );
		}
	}

	void activateResources( ID3D12GraphicsCommandList* commandList )
	{
		DX_ASSERT( _allocated, "" );
		std::vector<D3D12_RESOURCE_BARRIER> barriers;
		for ( auto r : _resources )
		{
			barriers.push_back( CD3DX12_RESOURCE_BARRIER::Aliasing( nullptr, r.get() ) );
		}
		resourceBarrier( commandList, barriers );
	}

private:
	bool _allocated = false;

	struct Allocation
	{
		uint64_t heapOffset;
		uint64_t bytes;
		D3D12_RESOURCE_STATES initialState;
		D3D12_RESOURCE_DESC resourceDesc;
		IResourceAcceptor* acceptor;
	};
	std::vector<Allocation> _allocationDefault;
	uint64_t _bytesDefault = 0;

	DxPtr<ID3D12Heap> _heapDefault;

	std::vector<DxPtr<ID3D12Resource>> _resources;
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
		D3D12_RANGE writerange = { 0, _bytes };

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
	void setName( std::wstring name )
	{
		_resource->SetName( name.c_str() );
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

	void map( std::function<void( const void* p )> f )
	{
		D3D12_RANGE readrange = { 0, _bytes };
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
	void setName( std::wstring name )
	{
		_resource->SetName( name.c_str() );
	}

private:
	int64_t _bytes;
	DxPtr<ID3D12Resource> _resource;
};

class BufferObjectUAV : public IResourceAcceptor
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
	BufferObjectUAV( BatchHeapAllocator* batchHeapAllocator, int64_t bytes, int64_t structureByteStride, D3D12_RESOURCE_STATES initialState, bool hasCounter = false, D3D12_RESOURCE_STATES counterInitialState = D3D12_RESOURCE_STATE_COPY_DEST )
		: _bytes( std::max( bytes, 1LL ) ), _structureByteStride( structureByteStride )
	{
		HRESULT hr;

		batchHeapAllocator->requestDefaultUAV( this, _bytes, initialState );
	}
	int64_t bytes() const
	{
		return _bytes;
	}
	int64_t itemCount() const
	{
		return _bytes / _structureByteStride;
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
	void copyFrom( ID3D12GraphicsCommandList* commandList, ID3D12Resource* src, uint64_t dstOffset, uint64_t srcOffset, uint64_t bytes )
	{
		DX_ASSERT( bytes <= _bytes, "overflow" );
		commandList->CopyBufferRegion(
			_resource.get(), dstOffset,
			src, srcOffset,
			bytes );
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
	D3D12_UNORDERED_ACCESS_VIEW_DESC CounterUAVDescription() const
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC d = {};
		d.Format = DXGI_FORMAT_UNKNOWN;
		d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		d.Buffer.FirstElement = 0;
		d.Buffer.NumElements = 1;
		d.Buffer.StructureByteStride = sizeof( uint32_t );
		d.Buffer.CounterOffsetInBytes = 0;
		return d;
	}
	void setName( std::wstring name )
	{
		_resource->SetName( name.c_str() );
	}

	void accept( DxPtr<ID3D12Resource> resource ) override
	{
		_resource = resource;
	}

	// It's just for debug
	template <class T>
	std::vector<T> synchronizedDownload( ID3D12Device* device, QueueObject* queue )
	{
		DX_ASSERT( _structureByteStride == sizeof( T ), "type T isn't suitable for this UAV" );

		CommandObject command( device, D3D12_COMMAND_LIST_TYPE_DIRECT );
		DownloaderObject downloader( device, _bytes );

		command.storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
			resourceBarrier( commandList, { CD3DX12_RESOURCE_BARRIER::Transition( _resource.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE ) } );
			commandList->CopyBufferRegion(
				downloader.resource(), 0,
				_resource.get(), 0,
				_bytes );
			resourceBarrier( commandList, { CD3DX12_RESOURCE_BARRIER::Transition( _resource.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON ) } );
		} );
		queue->execute( &command );

		int64_t numberOfElement = _bytes / _structureByteStride;
		std::vector<T> output( numberOfElement );

		std::shared_ptr<FenceObject> fence = queue->fence( device );
		fence->wait();

		downloader.map( [&]( const void* p ) {
			memcpy( output.data(), p, _bytes );
		} );

		return output;
	}

private:
	int64_t _bytes;
	int64_t _structureByteStride;
	DxPtr<ID3D12Resource> _resource;
};

class ConstantBufferObject
{
public:
	ConstantBufferObject( const ConstantBufferObject& ) = delete;
	void operator=( const ConstantBufferObject& ) = delete;

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
		_uploader->setName( L"uploader-ConstantBufferObject" );
	}
	D3D12_RESOURCE_BARRIER resourceBarrierTransition( D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to )
	{
		return CD3DX12_RESOURCE_BARRIER::Transition( _resource.get(), from, to );
	}

	// You must manage state by resourceBarrier
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
	void setName( std::wstring name )
	{
		_resource->SetName( name.c_str() );
	}

private:
	int64_t _bytes = 0;
	DxPtr<ID3D12Resource> _resource;
	std::unique_ptr<UploaderObject> _uploader;
};

class ConstantBufferArrayObject
{
public:
	ConstantBufferArrayObject( const ConstantBufferArrayObject& ) = delete;
	void operator=( const ConstantBufferArrayObject& ) = delete;

	ConstantBufferArrayObject( ID3D12Device* device, int64_t bytesPerElement, int64_t elementCount, D3D12_RESOURCE_STATES initialState )
		: _bytes( constantBufferSize( std::max( bytesPerElement, 1LL ) ) * elementCount ), _bytesStride( constantBufferSize( std::max( bytesPerElement, 1LL ) ) ), _elementCount( elementCount )
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
		_uploader->setName( L"uploader-ConstantBufferArrayObject" );
	}
	D3D12_RESOURCE_BARRIER resourceBarrierTransition( D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to )
	{
		return CD3DX12_RESOURCE_BARRIER::Transition( _resource.get(), from, to );
	}

	// You must manage state by resourceBarrier
	template <class T>
	void upload( ID3D12GraphicsCommandList* commandList, const std::vector<T>& value )
	{
		static_assert( std::is_trivial<T>::value == true, "T should trivial type" );
		DX_ASSERT( sizeof( T ) <= _bytesStride, "bad size" );
		DX_ASSERT( value.size() == _elementCount, "bad size" );

		_uploader->map( [&]( void* p ) {
			for ( int i = 0; i < _elementCount; ++i )
			{
				memcpy( (uint8_t*)p + _bytesStride * i, value.data() + i, sizeof( T ) );
			}
		} );
		commandList->CopyBufferRegion(
			_resource.get(), 0,
			_uploader->resource(), 0,
			_bytes );
	}
	ID3D12Resource* resource()
	{
		return _resource.get();
	}
	void setName( std::wstring name )
	{
		_resource->SetName( name.c_str() );
	}
	int64_t bytesStride() const
	{
		return _bytesStride;
	}
	int64_t bytesOffset( int i ) const
	{
		return _bytesStride * i;
	}

private:
	int64_t _bytes = 0;
	int64_t _bytesStride = 0;
	int64_t _elementCount = 0;
	DxPtr<ID3D12Resource> _resource;
	std::unique_ptr<UploaderObject> _uploader;
};

struct DescriptorEntity
{
	char type = 0;
	int registerIndex = 0;
	int descriptorHeapIndex = 0;
};
struct RootConstant32Entity
{
	int registerIndex = 0;
	int constantCount = 0;
	int constantIndex = 0;
};

struct DescriptorMap
{
	DescriptorMap()
	{
	}
	// these are describe a descriptor heap representation.
	// u and b will be encoded into a single descriptor heap. but s will be encoded the other descriptor heap. descriptorHeapIndex is the index.
	void u( int iRegister )
	{
		DescriptorEntity entity;
		entity.type = 'u';
		entity.registerIndex = iRegister;
		entity.descriptorHeapIndex = (int)_bufferDescriptorEntities.size();
		_bufferDescriptorEntities.push_back( entity );
	}
	void uRange( int begIndexRegister, int endIndexRegister )
	{
		for ( int i = begIndexRegister; i < endIndexRegister; ++i )
		{
			u( i );
		}
	}
	void b( int iRegister )
	{
		DescriptorEntity entity;
		entity.type = 'b';
		entity.registerIndex = iRegister;
		entity.descriptorHeapIndex = (int)_bufferDescriptorEntities.size();
		_bufferDescriptorEntities.push_back( entity );
	}
	void bRootConstant32( int iRegister, int constantCount )
	{
		RootConstant32Entity entity;
		entity.registerIndex = iRegister;
		entity.constantIndex = (int)_rootConstantEntities.size();
		entity.constantCount = constantCount;
		_rootConstantEntities.push_back( entity );
	}

	DxPtr<ID3D12RootSignature> createSignature( ID3D12Device* device ) const
	{
		std::vector<D3D12_DESCRIPTOR_RANGE> bufferDescriptorRanges;
		for ( int i = 0; i < _bufferDescriptorEntities.size(); ++i )
		{
			DescriptorEntity entity = _bufferDescriptorEntities[i];

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
		std::vector<D3D12_ROOT_PARAMETER> rootParameters;

		{
			D3D12_ROOT_PARAMETER rootParameter = {};
			rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParameter.DescriptorTable.NumDescriptorRanges = _bufferDescriptorEntities.size();
			rootParameter.DescriptorTable.pDescriptorRanges = bufferDescriptorRanges.data();
			rootParameters.push_back(rootParameter);
		}

		// Constant root parameters are started from 1.
		for ( int i = 0; i < _rootConstantEntities.size(); ++i )
		{
			RootConstant32Entity entity = _rootConstantEntities[i];
			D3D12_ROOT_PARAMETER rootParameter = {};
			rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParameter.Constants.RegisterSpace = 0;
			rootParameter.Constants.ShaderRegister = entity.registerIndex;
			rootParameter.Constants.Num32BitValues = entity.constantCount;
			rootParameters.push_back( rootParameter );
		}

		// Signature
		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = CD3DX12_ROOT_SIGNATURE_DESC( rootParameters.size(), rootParameters.data() );
		DxPtr<ID3DBlob> pSignature;
		HRESULT hr;
		hr = D3D12SerializeRootSignature( &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, pSignature.getAddressOf(), nullptr );
		DX_ASSERT( hr == S_OK, "" );

		DxPtr<ID3D12RootSignature> signature;
		hr = device->CreateRootSignature( 0, pSignature->GetBufferPointer(), pSignature->GetBufferSize(), IID_PPV_ARGS( signature.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
		return signature;
	}

	uint32_t bytesForBufferDescriptor( int incrementSizeCbvSrvUav ) const
	{
		int maxIndex = 0;
		for ( DescriptorEntity e : _bufferDescriptorEntities )
		{
			maxIndex = std::max( maxIndex, e.descriptorHeapIndex );
		}
		return incrementSizeCbvSrvUav * ( maxIndex + 1 );
	}

	uint32_t addressOfBufferDescriptor( char type, int iRegister, ID3D12Device* device ) const
	{
		uint32_t incrementUAV = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		for ( DescriptorEntity e : _bufferDescriptorEntities )
		{
			if ( e.type == type && e.registerIndex == iRegister )
			{
				return incrementUAV * e.descriptorHeapIndex;
			}
		}
		DX_ASSERT( false, "" );
		return -1;
	}
	uint32_t rootParameterIndexOfConstant( int iRegister ) const
	{
		for ( RootConstant32Entity e : _rootConstantEntities )
		{
			if ( e.registerIndex == iRegister )
			{
				return e.constantIndex + 1 /* buffer descriptor heap */;
			}
		}
		DX_ASSERT( false, "" );
		return -1;
	}
	std::vector<DescriptorEntity> _bufferDescriptorEntities;
	std::vector<RootConstant32Entity> _rootConstantEntities;
};

//
class StackDescriptorHeapObject
{
public:
	StackDescriptorHeapObject( ID3D12Device* device, int bufferHeapMaxCount )
	{
		HRESULT hr;
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = bufferHeapMaxCount;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		hr = device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( _bufferHeap.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );

		_incrementSizeCbvSrvUav = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		_bufferHeapCapacityBytes = bufferHeapMaxCount * _incrementSizeCbvSrvUav;
	}
	void clear()
	{
		_bufferHeapHeadBytes = 0;
		_currentBufferHeapBytes = 0;
	}
	void startNextHeapAndAssign( ID3D12GraphicsCommandList* commandList, DescriptorMap descriptorMap )
	{
		_descriptorMap = descriptorMap;
		_bufferHeapHeadBytes += _currentBufferHeapBytes;
		_currentBufferHeapBytes = _descriptorMap.bytesForBufferDescriptor( _incrementSizeCbvSrvUav );

		DX_ASSERT( _bufferHeapHeadBytes + _currentBufferHeapBytes < _bufferHeapCapacityBytes, "oveflow descriptor heap" );

		// Assign
		ID3D12DescriptorHeap* const heaps[] = { _bufferHeap.get() };
		commandList->SetDescriptorHeaps( 1, heaps );
		commandList->SetComputeRootDescriptorTable( 0, add( _bufferHeap->GetGPUDescriptorHandleForHeapStart(), _bufferHeapHeadBytes ) );
	}

	void u( ID3D12Device* device, int iRegister, ID3D12Resource* resource, D3D12_UNORDERED_ACCESS_VIEW_DESC uavdescription, ID3D12Resource* counterResource = nullptr )
	{
		int address = _descriptorMap.addressOfBufferDescriptor( 'u', iRegister, device );
		device->CreateUnorderedAccessView( resource, counterResource, &uavdescription, add( _bufferHeap->GetCPUDescriptorHandleForHeapStart(), _bufferHeapHeadBytes + address ) );
	}
	void b( ID3D12Device* device, int iRegister, ID3D12Resource* resource )
	{
		int address = _descriptorMap.addressOfBufferDescriptor( 'b', iRegister, device );
		D3D12_CONSTANT_BUFFER_VIEW_DESC d = {};
		d.BufferLocation = resource->GetGPUVirtualAddress();
		d.SizeInBytes = resource->GetDesc().Width;
		device->CreateConstantBufferView( &d, add( _bufferHeap->GetCPUDescriptorHandleForHeapStart(), _bufferHeapHeadBytes + address ) );
	}
	void bRootConstant32( ID3D12GraphicsCommandList* commandList, int iRegister, int num32BitValuesToSet, void *pSrc )
	{
		commandList->SetComputeRoot32BitConstants( _descriptorMap.rootParameterIndexOfConstant(iRegister), num32BitValuesToSet, pSrc, 0 );
	}
private:
	int _incrementSizeCbvSrvUav = 0;
	int _bufferHeapCapacityBytes = 0;
	int _bufferHeapHeadBytes = 0;
	int _currentBufferHeapBytes = 0;

	DxPtr<ID3D12DescriptorHeap> _bufferHeap;
	DescriptorMap _descriptorMap;
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
	// these are describe a descriptor heap representation.
	// u and b will be encoded into a single descriptor heap. but s will be encoded the other descriptor heap. descriptorHeapIndex is the index.
	void u( int iRegister )
	{
		DX_ASSERT( !_signature, "" );
		DX_ASSERT( !_pipelineState, "" );
		_descriptorMap.u( iRegister );
	}
	void uRange( int begIndexRegister, int endIndexRegister )
	{
		_descriptorMap.uRange( begIndexRegister, endIndexRegister );
	}
	void b( int iRegister )
	{
		DX_ASSERT( !_signature, "" );
		DX_ASSERT( !_pipelineState, "" );
		_descriptorMap.b( iRegister );
	}
	void bRootConstant32( int iRegister, int constantCount )
	{
		DX_ASSERT( !_signature, "" );
		DX_ASSERT( !_pipelineState, "" );
		_descriptorMap.bRootConstant32( iRegister, constantCount );
	}

	// compile shader and build. it also create the root signature.
	// you have to finish up calling u() b() before using this method.
	void loadShaderAndBuild( ID3D12Device* device, const char* shaderFile, const wchar_t* name = L"" )
	{
		HRESULT hr;

		_signature = _descriptorMap.createSignature( device );

		FileBlob shaderblob( shaderFile );
		// DxPtr<ID3DBlob> cso;
		// hr = D3DReadFileToBlob( shaderFile.c_str(), cso.getAddressOf() );
		// DX_ASSERT( hr == S_OK, "" );
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

			_pipelineState->SetName( name );
		}
	}

	DescriptorMap descriptorMap() const
	{
		return _descriptorMap;
	}

	void setPipelineState( ID3D12GraphicsCommandList* commandList )
	{
		commandList->SetPipelineState( _pipelineState.get() );
	}
	void setComputeRootSignature( ID3D12GraphicsCommandList* commandList )
	{
		commandList->SetComputeRootSignature( _signature.get() );
	}

	void dispatch( ID3D12GraphicsCommandList* commandList, int64_t x, int64_t y, int64_t z )
	{
		commandList->Dispatch( x, y, z );
	}

private:
	DescriptorMap _descriptorMap;

	DxPtr<ID3D12RootSignature> _signature;
	DxPtr<ID3D12PipelineState> _pipelineState;
};

class IndirectDispatcher
{
public:
	IndirectDispatcher( ID3D12Device* device )
	{
		D3D12_INDIRECT_ARGUMENT_DESC indirectArgDesc = {};
		indirectArgDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
		D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
		commandSignatureDesc.NodeMask = 0;
		commandSignatureDesc.pArgumentDescs = &indirectArgDesc;
		commandSignatureDesc.ByteStride = sizeof( D3D12_DISPATCH_ARGUMENTS );
		commandSignatureDesc.NumArgumentDescs = 1;
		HRESULT hr = device->CreateCommandSignature( &commandSignatureDesc, nullptr, IID_PPV_ARGS( _commandSignature.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
	}
	void dispatch( ID3D12GraphicsCommandList* commandList, ID3D12Resource* argumentBuffer )
	{
		commandList->ExecuteIndirect( _commandSignature.get(), 1, argumentBuffer, 0, nullptr, 0 );
	}

private:
	DxPtr<ID3D12CommandSignature> _commandSignature;
};

struct TimestampSpan
{
	std::string label;
	double durationS = 0;  // Seconds
	double durationMS = 0; // MilliSeconds
	double durationUS = 0; // MicroSeconds
};
class TimestampObject
{
public:
	TimestampObject( ID3D12Device* device, int capacity )
		: _queryCapacity( capacity )
	{
		D3D12_QUERY_HEAP_DESC desc = {};
		desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		desc.Count = _queryCapacity;
		device->CreateQueryHeap( &desc, IID_PPV_ARGS( _qHeap.getAddressOf() ) );

		_downloader = std::unique_ptr<DownloaderObject>( new DownloaderObject( device, sizeof( uint64_t ) * _queryCapacity ) );
	}

	void stamp( ID3D12GraphicsCommandList* command, const char* label = nullptr )
	{
		DX_ASSERT( _queryHeadIndex < _queryCapacity, "capacity overflow" );
		command->EndQuery( _qHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, _queryHeadIndex++ );

		if ( label )
		{
			_labels.push_back( label );
		}
		else
		{
			char buffer[64];
			sprintf( buffer, "index [%d]", _queryHeadIndex - 1 );
			_labels.push_back( buffer );
		}

		_ignoreNextLabel.push_back( 0 );
	}
	void stampBeg( ID3D12GraphicsCommandList* command, const char* label )
	{
		DX_ASSERT( _queryHeadIndex < _queryCapacity, "capacity overflow" );
		command->EndQuery( _qHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, _queryHeadIndex++ );

		_labels.push_back( label );
		_ignoreNextLabel.push_back( 0 );
	}
	void stampEnd( ID3D12GraphicsCommandList* command )
	{
		DX_ASSERT( _queryHeadIndex < _queryCapacity, "capacity overflow" );
		command->EndQuery( _qHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, _queryHeadIndex++ );

		_labels.push_back( "" );
		_ignoreNextLabel.push_back( 1 );
	}

	void clear()
	{
		_queryHeadIndex = 0;
		_labels.clear();
	}
	void resolve( ID3D12GraphicsCommandList* command )
	{
		command->ResolveQueryData( _qHeap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, _queryHeadIndex, _downloader->resource(), 0 );
	}

	std::vector<TimestampSpan> download( ID3D12CommandQueue* queue )
	{
		std::vector<TimestampSpan> r;

		uint64_t freq = 1;
		HRESULT hr;
		hr = queue->GetTimestampFrequency( &freq );
		DX_ASSERT( hr == S_OK, "" );

		_downloader->map( [&]( const void* p ) {
			const uint64_t* stamp = (const uint64_t*)p;

			for ( int i = 1; i < _queryHeadIndex; ++i )
			{
				if ( _ignoreNextLabel[i - 1] )
				{
					continue;
				}
				uint64_t deltaTime = stamp[i] - stamp[i - 1];
				double durationS = (double)deltaTime / (double)freq;

				TimestampSpan span;
				span.durationS = durationS;
				span.durationMS = durationS * 1000.0;
				span.durationUS = durationS * 1000.0 * 1000.0;
				if ( _labels[i].empty() )
				{
					span.label = _labels[i - 1];
				}
				else
				{
					span.label = _labels[i - 1] + " - " + _labels[i];
				}
				r.push_back( span );
			}
		} );

		return r;
	}

private:
	int _queryHeadIndex = 0;
	int _queryCapacity = 0;
	std::vector<std::string> _labels;
	std::vector<int> _ignoreNextLabel;

	DxPtr<ID3D12QueryHeap> _qHeap;
	std::unique_ptr<DownloaderObject> _downloader;
};