#include "EzDx.hpp"
#include "pr.hpp"

struct Arguments
{
	int width;
	int height;
	int sample_dx;
	int sample_dy;
};

void run( DeviceObject* deviceObject )
{
	using namespace pr;

	std::shared_ptr<CommandObject> computeCommandList( new CommandObject( deviceObject->device(), D3D12_COMMAND_LIST_TYPE_DIRECT, "Compute" ) );

	std::vector<float> input( 1000 );
	for ( int i = 0; i < input.size(); ++i )
	{
		input[i] = i / 10.0f;
	}

	uint64_t numberOfElement = input.size();
	uint64_t ioDataBytes = sizeof( float ) * input.size();

	std::unique_ptr<BufferObjectUAV> valueBuffer0( new BufferObjectUAV( deviceObject->device(), ioDataBytes, sizeof( float ), D3D12_RESOURCE_STATE_COPY_DEST ) );
	std::unique_ptr<BufferObjectUAV> valueBuffer1( new BufferObjectUAV( deviceObject->device(), ioDataBytes, sizeof( float ), D3D12_RESOURCE_STATE_COMMON ) );

	std::unique_ptr<UploaderObject> uploader( new UploaderObject( deviceObject->device(), ioDataBytes ) );
	uploader->map( [&]( void* p ) {
		memcpy( p, input.data(), ioDataBytes );
	} );
	std::unique_ptr<DownloaderObject> downloader( new DownloaderObject( deviceObject->device(), ioDataBytes ) );

	std::unique_ptr<ComputeObject> compute( new ComputeObject() );
	compute->u( 0 );
	compute->u( 1 );
	compute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "simple.cso" ).c_str() );
	std::shared_ptr<DescriptorHeapObject> heap = compute->createDescriptorHeap( deviceObject->device() );

	computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
		// upload
		valueBuffer0->copyFrom( commandList, uploader.get() );

		// upload memory barrier
		resourceBarrier( commandList, {valueBuffer0->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON )} );

		// Degamma
		compute->setPipelineState( commandList );
		compute->setComputeRootSignature( commandList );
		compute->assignDescriptorHeap( commandList, heap.get() );
		heap->u( deviceObject->device(), 0, valueBuffer0->resource(), valueBuffer0->UAVDescription() );
		heap->u( deviceObject->device(), 1, valueBuffer1->resource(), valueBuffer1->UAVDescription() );
		compute->dispatch( commandList, dispatchsize( numberOfElement, 64 ), 1, 1 );

		// download
		resourceBarrier( commandList, {valueBuffer1->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE )} );
		valueBuffer1->copyTo( commandList, downloader.get() );
		resourceBarrier( commandList, {valueBuffer1->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON )} );
	} );

	deviceObject->queueObject()->execute( computeCommandList.get() );

	// wait for CPU read.
	{
		std::shared_ptr<FenceObject> fence = deviceObject->queueObject()->fence( deviceObject->device() );
		fence->wait();
	}

	downloader->map( [&]( void* p ) {
		std::vector<float> output( numberOfElement );
		memcpy( output.data(), p, ioDataBytes );

		FILE* fp = fopen(GetDataPath("output.txt").c_str(), "w");
		for (int i = 0; i < output.size(); ++i)
		{
			fprintf( fp, "%f\n", output[i] );
		}
		fclose(fp);
	} );

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
