#include "EzDx.hpp"
#include "pr.hpp"
#include <intrin.h>

#define ELEMENTS_IN_BLOCK 512

// 4 stages, uint = [8 bit] [8 bit] [8 bit] [8 bit]
// so buckets wants 256 counters
#define COUNTERS_IN_BLOCK 256

#define RADIX_NUMBER_OF_ITERATION 4

struct CountAndReorderArgument
{
	uint32_t numberOfBlock;
	uint32_t elementsInBlock;
	uint32_t iteration;
};

struct ScanGlobalArgument
{
	int iteration;
	int offset;
};

static uint32_t prefixScanIterationCount( uint32_t n )
{
	DX_ASSERT( n, "n can't be 0" );
	unsigned long index = 0;
	_BitScanReverse( &index, n );
	return index + 1;
}

void run( DeviceObject* deviceObject )
{
	using namespace pr;

	std::shared_ptr<CommandObject> computeCommandList( new CommandObject( deviceObject->device(), D3D12_COMMAND_LIST_TYPE_DIRECT ) );
	computeCommandList->setName( L"Compute" );

	std::vector<uint32_t> input( 1000000 );
	for ( int i = 0; i < input.size(); ++i )
	{
		input[i] = rand();
		// input[i] = i & 0xFF;
	}

	std::shared_ptr<StackDescriptorHeapObject> heap( new StackDescriptorHeapObject( deviceObject->device(), 512 ) );

	uint64_t numberOfElement = input.size();
	uint64_t ioDataBytes = sizeof( uint32_t ) * input.size();

	uint64_t numberOfBlock = dispatchsize( numberOfElement, ELEMENTS_IN_BLOCK );
	uint64_t blockBytes = sizeof( uint32_t ) * numberOfBlock;

	uint64_t numberOfAllCoutner = numberOfBlock * COUNTERS_IN_BLOCK;
	uint64_t counterBytes = sizeof( uint32_t ) * numberOfAllCoutner;
	uint64_t globalScanBytes = sizeof( uint32_t ) * numberOfAllCoutner;

	std::unique_ptr<BufferObjectUAV> xs0( new BufferObjectUAV( deviceObject->device(), ioDataBytes, sizeof( uint32_t ), D3D12_RESOURCE_STATE_COPY_DEST ) );
	xs0->setName( L"xs0" );
	std::unique_ptr<BufferObjectUAV> xs1( new BufferObjectUAV( deviceObject->device(), ioDataBytes, sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );
	xs1->setName( L"xs1" );

	std::vector<std::unique_ptr<ConstantBufferObject>> countAndReorderArguments;
	for ( int i = 0; i < RADIX_NUMBER_OF_ITERATION; ++i )
	{
		countAndReorderArguments.push_back(
			std::unique_ptr<ConstantBufferObject>( new ConstantBufferObject( deviceObject->device(), sizeof( CountAndReorderArgument ), D3D12_RESOURCE_STATE_COPY_DEST ) ) );
	}

	/*
	column major store
	+------> counters (COUNTERS_IN_BLOCK)
	|(block 0, cnt=0), (block 0, cnt=1)
	|(block 1, cnt=0), (block 1, cnt=1)
	|(block 2, cnt=0), (block 2, cnt=1)
	v
	blocks ( numberOfBlock )
	*/
	std::unique_ptr<BufferObjectUAV> counter( new BufferObjectUAV( deviceObject->device(), counterBytes, sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );
	counter->setName( L"counter" );

	std::unique_ptr<BufferObjectUAV> globalScanTable0( new BufferObjectUAV( deviceObject->device(), globalScanBytes, sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );
	std::unique_ptr<BufferObjectUAV> globalScanTable1( new BufferObjectUAV( deviceObject->device(), globalScanBytes, sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );
	globalScanTable0->setName( L"globalScanTable0" );
	globalScanTable1->setName( L"globalScanTable1" );

	std::unique_ptr<UploaderObject> uploader( new UploaderObject( deviceObject->device(), ioDataBytes ) );
	uploader->setName( L"uploader" );
	uploader->map( [&]( void* p ) {
		memcpy( p, input.data(), ioDataBytes );
	} );

	std::unique_ptr<ComputeObject> countCompute( new ComputeObject() );
	countCompute->u( 0 );
	countCompute->u( 1 );
	countCompute->b( 0 );
	countCompute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "radixsort_count.cso" ).c_str() );

	// Scan Prepare
	std::unique_ptr<ComputeObject> scanPrepareCompute( new ComputeObject() );
	scanPrepareCompute->u( 0 );
	scanPrepareCompute->u( 1 );
	scanPrepareCompute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "radixsort_scanPrepare.cso" ).c_str() );

	// Global Scan
	std::unique_ptr<ComputeObject> scanglobalCompute( new ComputeObject() );
	scanglobalCompute->u( 0 );
	scanglobalCompute->u( 1 );
	scanglobalCompute->b( 0 );
	scanglobalCompute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "radixsort_scanglobal.cso" ).c_str() );

	int globalScanIteration = prefixScanIterationCount( numberOfAllCoutner );
	std::vector<std::unique_ptr<ConstantBufferObject>> scanglobalConstants;
	for ( int i = 0; i < globalScanIteration; ++i )
	{
		std::unique_ptr<ConstantBufferObject> constant( new ConstantBufferObject( deviceObject->device(), sizeof( ScanGlobalArgument ), D3D12_RESOURCE_STATE_COPY_DEST ) );
		scanglobalConstants.push_back( std::move( constant ) );
	}

	// Reorder
	std::unique_ptr<ComputeObject> reorderCompute( new ComputeObject() );
	reorderCompute->u( 0 );
	reorderCompute->u( 1 );
	reorderCompute->u( 2 );
	reorderCompute->u( 3 );
	reorderCompute->b( 0 );
	reorderCompute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "radixsort_reorder.cso" ).c_str() );

	std::unique_ptr<TimestampObject> stumper( new TimestampObject( deviceObject->device(), 128 ) );

	computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
		// upload
		std::vector<D3D12_RESOURCE_BARRIER> uploadBarriers;

		xs0->copyFrom( commandList, uploader.get() );
		uploadBarriers.push_back( xs0->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ) );

		//
		for ( int i = 0; i < RADIX_NUMBER_OF_ITERATION; ++i )
		{
			countAndReorderArguments[i]->upload<CountAndReorderArgument>( commandList, {(uint32_t)numberOfBlock, (uint32_t)ELEMENTS_IN_BLOCK, (uint32_t)i} );
			uploadBarriers.push_back( countAndReorderArguments[i]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ) );
		}

		// Scan Global Upload
		for ( int i = 0; i < globalScanIteration; ++i )
		{
			ScanGlobalArgument arg = {};
			arg.iteration = i;
			arg.offset = 1 << i;
			scanglobalConstants[i]->upload( commandList, arg );
			uploadBarriers.push_back( scanglobalConstants[i]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ) );
		}
		resourceBarrier( commandList, uploadBarriers );

		for ( int i = 0; i < RADIX_NUMBER_OF_ITERATION; ++i )
		{
			// clear
			stumper->stampBeg( commandList, "Count" );

			// count
			countCompute->setPipelineState( commandList );
			countCompute->setComputeRootSignature( commandList );
			heap->startNextHeapAndAssign( commandList, countCompute->descriptorEnties() );
			heap->u( deviceObject->device(), 0, xs0->resource(), xs0->UAVDescription() );
			heap->u( deviceObject->device(), 1, counter->resource(), counter->UAVDescription() );
			heap->b( deviceObject->device(), 0, countAndReorderArguments[i]->resource() );
			countCompute->dispatch(commandList, dispatchsize(numberOfElement, ELEMENTS_IN_BLOCK), 1, 1);
			
			resourceBarrier( commandList, {counter->resourceBarrierUAV()} );

			stumper->stampEnd( commandList );
			stumper->stampBeg( commandList, "Scan" );

			// Scan Prepare
			scanPrepareCompute->setPipelineState( commandList );
			scanPrepareCompute->setComputeRootSignature( commandList );
			heap->startNextHeapAndAssign( commandList, scanPrepareCompute->descriptorEnties() );
			heap->u( deviceObject->device(), 0, counter->resource(), counter->UAVDescription() );
			heap->u( deviceObject->device(), 1, globalScanTable0->resource(), globalScanTable0->UAVDescription() );
			scanPrepareCompute->dispatch( commandList, dispatchsize( numberOfAllCoutner, 128 ), 1, 1 );

			resourceBarrier( commandList, {globalScanTable0->resourceBarrierUAV()} );

			// globalScanTable0 is a always latest one.
			for ( int j = 0; j < globalScanIteration; ++j )
			{
				scanglobalCompute->setPipelineState( commandList );
				scanglobalCompute->setComputeRootSignature( commandList );
				heap->startNextHeapAndAssign( commandList, scanglobalCompute->descriptorEnties() );
				heap->b( deviceObject->device(), 0, scanglobalConstants[j]->resource() );
				heap->u( deviceObject->device(), 0, globalScanTable0->resource(), globalScanTable0->UAVDescription() );
				heap->u( deviceObject->device(), 1, globalScanTable1->resource(), globalScanTable1->UAVDescription() );

				scanglobalCompute->dispatch( commandList, dispatchsize( numberOfAllCoutner, 64 ), 1, 1 );

				std::swap( globalScanTable0, globalScanTable1 );
				resourceBarrier( commandList, {globalScanTable0->resourceBarrierUAV()} );
			}

			stumper->stampEnd( commandList );
			stumper->stampBeg( commandList, "Reorder" );

			// Reorder
			reorderCompute->setPipelineState( commandList );
			reorderCompute->setComputeRootSignature( commandList );
			heap->startNextHeapAndAssign( commandList, reorderCompute->descriptorEnties() );
			heap->u( deviceObject->device(), 0, xs0->resource(), xs0->UAVDescription() );
			heap->u( deviceObject->device(), 1, xs1->resource(), xs1->UAVDescription() );
			heap->u( deviceObject->device(), 2, globalScanTable0->resource(), globalScanTable0->UAVDescription() );
			heap->b( deviceObject->device(), 0, countAndReorderArguments[i]->resource() );
			// reorderCompute->dispatch( commandList, dispatchsize( numberOfBlock, 64 ), 1, 1 );

			reorderCompute->dispatch(commandList, numberOfBlock, 1, 1);

			std::swap( xs0, xs1 );

			resourceBarrier( commandList, {xs0->resourceBarrierUAV()} );

			stumper->stampEnd( commandList );
		}

		stumper->resolve( commandList );
	} );

	deviceObject->queueObject()->execute( computeCommandList.get() );

	// wait for CPU read.
	{
		std::shared_ptr<FenceObject> fence = deviceObject->queueObject()->fence( deviceObject->device() );
		fence->wait();
	}

	auto stumpdata = stumper->download( deviceObject->queueObject()->queue() );
	for ( auto s : stumpdata )
	{
		printf( "%s -- %.4f ms\n", s.label.c_str(), s.durationMS );
	}

	std::vector<uint32_t> counterValues = counter->synchronizedDownload<uint32_t>( deviceObject->device(), deviceObject->queueObject() );
	std::vector<uint32_t> globalScanTableValues = globalScanTable0->synchronizedDownload<uint32_t>( deviceObject->device(), deviceObject->queueObject() );
	std::vector<uint32_t> sortedValues = xs0->synchronizedDownload<uint32_t>( deviceObject->device(), deviceObject->queueObject() );

	// this check is RADIX_NUMBER_OF_ITERATION == 1 only
	//// Check Counter
	//{
	//	for ( int i = 0; i < numberOfElement; i += ELEMENTS_IN_BLOCK )
	//	{
	//		std::vector<uint32_t> counter( COUNTERS_IN_BLOCK );
	//		for ( int j = 0; j < ELEMENTS_IN_BLOCK; ++j )
	//		{
	//			int src = i + j;
	//			if ( numberOfElement <= src )
	//			{
	//				break;
	//			}
	//			uint32_t index = input[src] & 0xFF;
	//			counter[index]++;
	//		}

	//		int blockindex = i / ELEMENTS_IN_BLOCK;
	//		for ( int j = 0; j < COUNTERS_IN_BLOCK; ++j )
	//		{
	//			DX_ASSERT( counterValues[numberOfBlock * j + blockindex] == counter[j], "" );
	//		}
	//	}
	//}

	//// Check Prefix Sum
	//{
	//	int sum = 0;
	//	for ( int i = 0; i < counterValues.size(); i++ )
	//	{
	//		DX_ASSERT( globalScanTableValues[i] == sum, "" );
	//		sum += counterValues[i];
	//	}
	//}

	// Check Sort
	{
		std::sort( input.begin(), input.end() );

		for ( int i = 0; i < sortedValues.size(); ++i )
		{
			DX_ASSERT( sortedValues[i] == input[i], "" );
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
			d->device()->SetStablePowerState( true );
			run( d.get() );
		}
	}
}
