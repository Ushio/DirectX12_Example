#include "EzDx.hpp"
#include "pr.hpp"

struct BvhNode
{
	// AABBs
	float lowerL[3];
	float upperL[3];
	float lowerR[3];
	float upperR[3];

	// childs or geoms
	uint32_t indexL[2];
	uint32_t indexR[2];
};

void run( DeviceObject* deviceObject )
{
	using namespace pr;

	//for ( int i = 0; i < 33; i++ )
	//{
	//	uint64_t bytes = 1llu << (uint64_t)i;

	//	Stopwatch sw;

	//	DxPtr<ID3D12Heap> heap;
	//	HRESULT hr = deviceObject->device()->CreateHeap( &CD3DX12_HEAP_DESC( bytes, D3D12_HEAP_TYPE_DEFAULT ), IID_PPV_ARGS( heap.getAddressOf() ) );

	//	printf( "%lld %.3f\n", bytes, 1000.0 * sw.elapsed() );
	//}

	int align = 32;
	for (int i = 0; i < 100; ++i)
	{
		printf("[%d] %d aligned\n", i, alignPointer(i, align));
	}

	{
		uint64_t bytes = 1024llu * 1024 * 100;
		Stopwatch sw;

		HRESULT hr;
		DxPtr<ID3D12Heap> heap;
		hr = deviceObject->device()->CreateHeap(&CD3DX12_HEAP_DESC(bytes, D3D12_HEAP_TYPE_DEFAULT), IID_PPV_ARGS(heap.getAddressOf()));

		// deviceObject->device()->CreatePlacedResource()

		printf("%lld %.3f\n", bytes, 1000.0 * sw.elapsed());
	}

	//for (int i = 0; i < 30; i++)
	//{
	//	int bytes = 1 << i;
	//	Stopwatch sw;
	//	std::unique_ptr<BufferObjectUAV> valueBuffer0(new BufferObjectUAV(deviceObject->device(), bytes, 1, D3D12_RESOURCE_STATE_COMMON));
	//	printf("%d %.3f\n", valueBuffer0->bytes(), 1000.0 * sw.elapsed());
	//}

	{
		int maxNodes = 2612928;
		Stopwatch sw;
		std::unique_ptr<BufferObjectUAV> valueBuffer0(new BufferObjectUAV(deviceObject->device(), 1024 * 1024 * 100, 1, D3D12_RESOURCE_STATE_COMMON));
		printf("allocate %d byte %.3f ms\n", valueBuffer0->bytes(), 1000.0 * sw.elapsed());
	}
	{
		int maxNodes = 2612928;
		Stopwatch sw;
		std::unique_ptr<BufferObjectUAV> valueBuffer0(new BufferObjectUAV(deviceObject->device(), 1024 * 1024 * 100, 1, D3D12_RESOURCE_STATE_COMMON));
		printf("allocate %d byte %.3f ms\n", valueBuffer0->bytes(), 1000.0 * sw.elapsed());
	}

	std::shared_ptr<CommandObject> computeCommandList( new CommandObject( deviceObject->device(), D3D12_COMMAND_LIST_TYPE_DIRECT ) );
	computeCommandList->setName( L"Compute" );

	std::vector<float> input( 100000 );
	for ( int i = 0; i < input.size(); ++i )
	{
		input[i] = i / 10.0f;
	}

	uint64_t numberOfElement = input.size();
	uint64_t ioDataBytes = sizeof( float ) * input.size();

	std::unique_ptr<BufferObjectUAV> valueBuffer0( new BufferObjectUAV( deviceObject->device(), ioDataBytes, sizeof( float ), D3D12_RESOURCE_STATE_COPY_DEST ) );
	std::unique_ptr<BufferObjectUAV> valueBuffer1( new BufferObjectUAV( deviceObject->device(), ioDataBytes, sizeof( float ), D3D12_RESOURCE_STATE_COMMON ) );
	valueBuffer0->setName( L"valueBuffer0" );
	valueBuffer1->setName( L"valueBuffer1" );

	std::unique_ptr<UploaderObject> uploader( new UploaderObject( deviceObject->device(), ioDataBytes ) );
	uploader->setName( L"uploader" );
	uploader->map( [&]( void* p ) {
		memcpy( p, input.data(), ioDataBytes );
	} );

	std::unique_ptr<ComputeObject> compute( new ComputeObject() );
	compute->u( 0 );
	compute->u( 1 );
	compute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "simple.cso" ).c_str() );
	std::shared_ptr<StackDescriptorHeapObject> heap( new StackDescriptorHeapObject( deviceObject->device(), 32 ) );

	computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
		// upload
		valueBuffer0->copyFrom( commandList, uploader.get() );

		// upload memory barrier
		resourceBarrier( commandList, {valueBuffer0->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON )} );

		// Execute
		compute->setPipelineState( commandList );
		compute->setComputeRootSignature( commandList );
		heap->startNextHeapAndAssign( commandList, compute->descriptorEnties() );
		heap->u( deviceObject->device(), 0, valueBuffer0->resource(), valueBuffer0->UAVDescription() );
		heap->u( deviceObject->device(), 1, valueBuffer1->resource(), valueBuffer1->UAVDescription() );
		compute->dispatch( commandList, dispatchsize( numberOfElement, 64 ), 1, 1 );
	} );

	deviceObject->queueObject()->execute( computeCommandList.get() );

	std::vector<float> output = valueBuffer1->synchronizedDownload<float>( deviceObject->device(), deviceObject->queueObject() );
	{
		FILE* fp = fopen( GetDataPath( "output.txt" ).c_str(), "w" );
		for ( int i = 0; i < output.size(); ++i )
		{
			fprintf( fp, "%f\n", output[i] );
		}
		fclose( fp );
	}

	// for debugger tools.
	deviceObject->present();
}

int main()
{
	using namespace pr;
	SetDataDir( ExecutableDir() );

	// Activate Debug Layer
	enableDebugLayer();

	std::vector<DxPtr<IDXGIAdapter>> adapters = getAllAdapters();

	HRESULT hr;
	std::vector<std::shared_ptr<DeviceObject>> devices;
	for ( DxPtr<IDXGIAdapter> adapter : adapters )
	{
		DXGI_ADAPTER_DESC d;
		hr = adapter->GetDesc( &d );
		DX_ASSERT( hr == S_OK, "" );

		// The number of bytes of dedicated video memory that are not shared with the CPU.
		if ( d.DedicatedVideoMemory == 0 )
		{
			continue;
		}
		printf( "%s\n", wstring_to_string( d.Description ).c_str() );

		devices.push_back( std::shared_ptr<DeviceObject>( new DeviceObject( adapter.get() ) ) );
	}

	for ( int i = 0;; ++i )
	{
		for ( auto d : devices )
		{
			printf( "run : %s\n", wstring_to_string( d->deviceName() ).c_str() );
			run( d.get() );
		}
	}
}
