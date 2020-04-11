#include "EzDx.hpp"
#include "pr.hpp"

#define ELEMENTS_IN_BLOCK 1024

// 4 stages, uint = [8 bit] [8 bit] [8 bit] [8 bit]
// so buckets wants 256 counters
#define COUNTER_IN_BLOCK 256

void run( DeviceObject* deviceObject )
{
	using namespace pr;

	std::shared_ptr<CommandObject> computeCommandList( new CommandObject( deviceObject->device(), D3D12_COMMAND_LIST_TYPE_DIRECT ) );
	computeCommandList->setName( L"Compute" );

	std::vector<uint32_t> input( 100000 );
	for ( int i = 0; i < input.size(); ++i )
	{
		input[i] = rand() & 0xFF;
	}

	std::unique_ptr<ComputeObject> clearCompute( new ComputeObject() );
	clearCompute->u( 0 );
	clearCompute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "radixsort_clear.cso" ).c_str() );
	std::shared_ptr<DescriptorHeapObject> clearHeap = clearCompute->createDescriptorHeap( deviceObject->device() );
	clearHeap->setName( L"clearHeap" );

	uint64_t numberOfElement = input.size();
	uint64_t ioDataBytes = sizeof( uint32_t ) * input.size();

	uint64_t numberOfBlock = dispatchsize( numberOfElement, ELEMENTS_IN_BLOCK );
	uint64_t blockBytes = sizeof( uint32_t ) * numberOfBlock;

	uint64_t numberOfCoutner = numberOfBlock * COUNTER_IN_BLOCK;
	uint64_t counterBytes = sizeof( uint32_t ) * numberOfCoutner;

	std::unique_ptr<BufferObjectUAV> xs( new BufferObjectUAV( deviceObject->device(), ioDataBytes, sizeof( uint32_t ), D3D12_RESOURCE_STATE_COPY_DEST ) );
	xs->setName( L"xs" );
	std::unique_ptr<BufferObjectUAV> counter( new BufferObjectUAV( deviceObject->device(), counterBytes, sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );

	std::unique_ptr<UploaderObject> uploader( new UploaderObject( deviceObject->device(), ioDataBytes ) );
	uploader->setName( L"uploader" );
	uploader->map( [&]( void* p ) {
		memcpy( p, input.data(), ioDataBytes );
	} );

	std::unique_ptr<ComputeObject> countCompute( new ComputeObject() );
	countCompute->u( 0 );
	countCompute->u( 1 );
	countCompute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "radixsort_count.cso" ).c_str() );
	std::shared_ptr<DescriptorHeapObject> countheap = countCompute->createDescriptorHeap( deviceObject->device() );
	countheap->setName( L"countheap" );

	computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
		// upload
		xs->copyFrom( commandList, uploader.get() );

		// clear
		clearCompute->setPipelineState( commandList );
		clearCompute->setComputeRootSignature( commandList );
		clearCompute->assignDescriptorHeap( commandList, clearHeap.get() );
		clearHeap->u( deviceObject->device(), 0, counter->resource(), counter->UAVDescription() );
		clearCompute->dispatch( commandList, dispatchsize( numberOfCoutner, 64 ), 1, 1 );

		resourceBarrier( commandList, {counter->resourceBarrierUAV()} );

		// count
		countCompute->setPipelineState( commandList );
		countCompute->setComputeRootSignature( commandList );
		countCompute->assignDescriptorHeap( commandList, countheap.get() );
		countheap->u( deviceObject->device(), 0, xs->resource(), xs->UAVDescription() );
		countheap->u( deviceObject->device(), 1, counter->resource(), counter->UAVDescription() );
		countCompute->dispatch( commandList, dispatchsize( numberOfElement, ELEMENTS_IN_BLOCK ), 1, 1 );

		// upload memory barrier
		resourceBarrier( commandList, {xs->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON )} );
	} );

	deviceObject->queueObject()->execute( computeCommandList.get() );

	std::vector<uint32_t> counterValues = counter->synchronizedDownload<uint32_t>( deviceObject->device(), deviceObject->queueObject() );
	{
		for ( int i = 0; i < numberOfElement; i += ELEMENTS_IN_BLOCK )
		{
			std::vector<uint32_t> counter( COUNTER_IN_BLOCK );
			for ( int j = 0; j < ELEMENTS_IN_BLOCK; ++j )
			{
				int src = i + j;
				if ( numberOfElement <= src )
				{
					break;
				}
				uint32_t index = input[src] & 0xFF;
				counter[index]++;
			}

			int blockindex = i / ELEMENTS_IN_BLOCK;
			int bucketStart = blockindex * COUNTER_IN_BLOCK;
			for ( int j = 0; j < COUNTER_IN_BLOCK; ++j )
			{
				DX_ASSERT( counterValues[bucketStart + j] == counter[j], "" );
			}
			printf( "" );
		}
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
