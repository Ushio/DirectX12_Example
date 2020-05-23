#include "EzDx.hpp"
#include "pr.hpp"
#include "lwHoudiniLoader.hpp"
#include "WinPixEventRuntime/pix3.h"
#include "bvh.h"

struct Arguments
{
	int cb_width;
	int cb_height;
	int cb_pad0;
	int cb_pad1;
	float cb_inverseVP[16];
};

inline uint32_t as_uint32(float f) {
	return *reinterpret_cast<uint32_t*>(&f);
}
inline float as_float(uint32_t u) {
	return *reinterpret_cast<float*>(&u);
}
inline int32_t to_ordered(float f) {
	uint32_t b = as_uint32(f);
	uint32_t s = b & 0x80000000; // sign bit
	int32_t  x = b & 0x7FFFFFFF; // expornent and significand
	return s ? -x : x;
}
inline float from_ordered(int32_t ordered) {
	if (ordered < 0) {
		uint32_t x = -ordered;
		return as_float(x | 0x80000000);
	}
	return as_float(ordered);
}

inline uint32_t prefixScanIterationCount(uint32_t n)
{
	if (n <= 1) { return 0; }

	unsigned long index;
	_BitScanReverse(&index, n - 1);
	return index + 1;
}
inline uint32_t prefixScanIterationCountNaive(uint32_t n)
{
	int iteration = 0;
	uint32_t offset = 1;
	while (offset < n)
	{
		offset *= 2;
		iteration++;
	}
	return iteration;
}
//Check
//for (int i = 1; i < 32; ++i)
//{
//	printf("%d, %d[%d]\n", i, prefixScanIterationCount(i), prefixScanIterationCountNaive(i));
//}

struct BvhScanArgument
{
	int nElement;
	int offset;
};
struct BinningArgument
{
	int consumeTaskCount;
};

uint32_t ringBufferCount(uint32_t beg, uint32_t end, uint32_t n)
{
	if (beg <= end) {
		return end - beg;
	}
	return n - beg + end;
}

struct GPUBvhBuilder
{
	GPUBvhBuilder( DeviceObject* deviceObject, const lwh::Polygon* polygon )
	{
		pr::Stopwatch sw;

		auto compute_bvh_firstTask = std::unique_ptr<ComputeObject>(new ComputeObject());
		compute_bvh_firstTask->uRange(0, 3);
		compute_bvh_firstTask->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "bvh_firstTask.cso" ).c_str() );

		auto compute_bvh_element = std::unique_ptr<ComputeObject>( new ComputeObject() );
		compute_bvh_element->uRange(0, 3);
		compute_bvh_element->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "bvh_element.cso" ).c_str() );

		auto compute_bvh_executionCount = std::unique_ptr<ComputeObject>( new ComputeObject() );
		compute_bvh_executionCount->u( 0 );
		compute_bvh_executionCount->u( 1 );
		compute_bvh_executionCount->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "bvh_executionCount.cso" ).c_str() );

		auto compute_bvh_scan = std::unique_ptr<ComputeObject>( new ComputeObject() );
		compute_bvh_scan->uRange(0, 2);
		compute_bvh_scan->b( 0 );
		compute_bvh_scan->b( 1 );
		compute_bvh_scan->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "bvh_scan.cso" ).c_str() );

		auto compute_bvh_clearBin = std::unique_ptr<ComputeObject>( new ComputeObject() );
		compute_bvh_clearBin->b( 0 );
		compute_bvh_clearBin->uRange(0, 3);
		compute_bvh_clearBin->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "bvh_clearBin.cso" ).c_str() );

		auto compute_bvh_consumeTask = std::unique_ptr<ComputeObject>( new ComputeObject() );
		compute_bvh_consumeTask->b( 0 );
		compute_bvh_consumeTask->u( 0 );
		compute_bvh_consumeTask->u( 1 );
		compute_bvh_consumeTask->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "bvh_consumeTask.cso" ).c_str() );

		auto compute_bvh_binning = std::unique_ptr<ComputeObject>( new ComputeObject() );
		compute_bvh_binning->b( 0 );
		compute_bvh_binning->uRange(0, 6);
		compute_bvh_binning->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "bvh_binning.cso" ).c_str() );

		auto compute_bvh_selectBin = std::unique_ptr<ComputeObject>( new ComputeObject() );
		compute_bvh_selectBin->b( 0 );
		compute_bvh_selectBin->uRange(0, 5);
		compute_bvh_selectBin->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "bvh_selectBin.cso" ).c_str() );

		auto compute_bvh_reorder = std::unique_ptr<ComputeObject>( new ComputeObject() );
		compute_bvh_reorder->b( 0 );
		compute_bvh_reorder->uRange( 0, 7 );
		compute_bvh_reorder->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "bvh_reorder.cso" ).c_str() );

		printf("setup shaders %.3f ms\n", 1000.0 * sw.elapsed() );

		uint32_t vBytes = polygon->P.size() * sizeof( glm::vec3 );
		uint32_t iBytes = polygon->indices.size() * sizeof( uint32_t );
		vertexBuffer = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), vBytes, sizeof( glm::vec3 ), D3D12_RESOURCE_STATE_COPY_DEST ) );
		indexBuffer = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), iBytes, sizeof( uint32_t ), D3D12_RESOURCE_STATE_COPY_DEST ) );
		UploaderObject v_uploader( deviceObject->device(), vBytes );
		UploaderObject i_uploader( deviceObject->device(), iBytes );
		v_uploader.map( [&]( void* p ) {
			memcpy( p, polygon->P.data(), vBytes );
		} );
		i_uploader.map( [&]( void* p ) {
			memcpy( p, polygon->indices.data(), iBytes );
		} );

		auto computeCommandList = std::unique_ptr<CommandObject>( new CommandObject( deviceObject->device(), D3D12_COMMAND_LIST_TYPE_DIRECT ) );
		computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
			vertexBuffer->copyFrom( commandList, &v_uploader );
			indexBuffer->copyFrom( commandList, &i_uploader );

			resourceBarrier( commandList, {
											  vertexBuffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
											  indexBuffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
										  } );
		} );
		deviceObject->queueObject()->execute( computeCommandList.get() );

		int nProcessBlocks = 1024 * 64;

		UploaderObject zeroU32( deviceObject->device(), sizeof( uint32_t ) );
		zeroU32.map( []( void* p ) { memset( p, 0, sizeof( uint32_t ) ); } );

		std::unique_ptr<UploaderObject> firstTaskUploader( new UploaderObject( deviceObject->device(), sizeof( BuildTask ) ) );
		firstTaskUploader->map( [&]( void* p ) {
			BuildTask task;
			task.geomBeg = 0;
			task.geomEnd = polygon->primitiveCount;
			for ( int i = 0; i < 3; ++i )
			{
				task.lower[i] = to_ordered( +FLT_MAX );
				task.upper[i] = to_ordered( -FLT_MAX );
			}
			task.parentNode = -1;
			memcpy( p, &task, sizeof( BuildTask ) );
		} );

		uint32_t taskBufferCount = std::max( (uint32_t)2, polygon->primitiveCount );
		std::unique_ptr<BufferObjectUAV> bvhElementBuffer( new BufferObjectUAV( deviceObject->device(), polygon->primitiveCount * sizeof( BvhElement ), sizeof( BvhElement ), D3D12_RESOURCE_STATE_COMMON ) );
		std::unique_ptr<BufferObjectUAV> bvhBuildTaskBuffer( new BufferObjectUAV( deviceObject->device(), taskBufferCount * sizeof( BuildTask ), sizeof( BuildTask ), D3D12_RESOURCE_STATE_COPY_DEST ) );

		std::unique_ptr<BufferObjectUAV> bvhBuildTaskRingRanges[2];
		bvhBuildTaskRingRanges[0] = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), 2 * sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COPY_DEST ) );
		bvhBuildTaskRingRanges[1] = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), 2 * sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COPY_DEST ) );
		UploaderObject ringBufferRangeDefault( deviceObject->device(), 4 * sizeof( uint32_t ) );
		ringBufferRangeDefault.map( [&]( void* p ) {
			uint32_t values[] = {0, 1, 1, 1};
			memcpy( p, values, sizeof( uint32_t ) * 4 );
		} );

		bvhElementIndicesBuffers[0] = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), polygon->primitiveCount * sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );
		bvhElementIndicesBuffers[1] = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), polygon->primitiveCount * sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );

		std::unique_ptr<BufferObjectUAV> executionCountBuffer( new BufferObjectUAV( deviceObject->device(), nProcessBlocks * sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );
		std::unique_ptr<BufferObjectUAV> executionTableBuffers[2];
		executionTableBuffers[0] = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), nProcessBlocks * sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COPY_DEST ) );
		executionTableBuffers[1] = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), nProcessBlocks * sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );

		std::unique_ptr<BufferObjectUAV> binningBuffer( new BufferObjectUAV( deviceObject->device(), nProcessBlocks * sizeof( BinningBuffer ), sizeof( BinningBuffer ), D3D12_RESOURCE_STATE_COMMON ) );
		std::unique_ptr<BufferObjectUAV> executionIterator( new BufferObjectUAV( deviceObject->device(), sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );

		// NodeBuffer geombeg, geomend are stored to indexL, indexR
		int maxNodes = std::max( (int)polygon->primitiveCount - 1, 1 );
		bvhNodeBuffer = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), maxNodes * sizeof( BvhNode ), sizeof( BvhNode ), D3D12_RESOURCE_STATE_COMMON ) );
		std::unique_ptr<BufferObjectUAV> bvhNodeCounterBuffer = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COPY_DEST ) );

		auto heap = std::unique_ptr<StackDescriptorHeapObject>( new StackDescriptorHeapObject( deviceObject->device(), 128 ) );

		computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
			// Calculate AABB for each element
			compute_bvh_element->setPipelineState( commandList );
			compute_bvh_element->setComputeRootSignature( commandList );
			heap->startNextHeapAndAssign( commandList, compute_bvh_element->descriptorEnties() );
			heap->u( deviceObject->device(), 0, vertexBuffer->resource(), vertexBuffer->UAVDescription() );
			heap->u( deviceObject->device(), 1, indexBuffer->resource(), indexBuffer->UAVDescription() );
			heap->u( deviceObject->device(), 2, bvhElementBuffer->resource(), bvhElementBuffer->UAVDescription() );
			compute_bvh_element->dispatch( commandList, dispatchsize( polygon->primitiveCount, 64 ), 1, 1 );

			// Task Counter Initialize
			bvhBuildTaskRingRanges[0]->copyFrom( commandList, ringBufferRangeDefault.resource(), 0, 0, sizeof( uint32_t ) * 2 );
			bvhBuildTaskRingRanges[1]->copyFrom( commandList, ringBufferRangeDefault.resource(), 0, sizeof( uint32_t ) * 2, sizeof( uint32_t ) * 2 );

			// Task Initialize
			bvhBuildTaskBuffer->copyFrom( commandList, firstTaskUploader->resource(), 0, 0, sizeof( BuildTask ) );

			// BVH Node Counter Clear
			bvhNodeCounterBuffer->copyFrom( commandList, &zeroU32 );

			resourceBarrier( commandList, {
											  bvhElementBuffer->resourceBarrierUAV(),
											  bvhBuildTaskBuffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
											  bvhBuildTaskRingRanges[0]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
											  bvhBuildTaskRingRanges[1]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
											  bvhNodeCounterBuffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
										  } );

			// FirstTask
			compute_bvh_firstTask->setPipelineState( commandList );
			compute_bvh_firstTask->setComputeRootSignature( commandList );
			heap->startNextHeapAndAssign( commandList, compute_bvh_firstTask->descriptorEnties() );
			heap->u( deviceObject->device(), 0, bvhBuildTaskBuffer->resource(), bvhBuildTaskBuffer->UAVDescription() );
			heap->u( deviceObject->device(), 1, bvhElementBuffer->resource(), bvhElementBuffer->UAVDescription() );
			heap->u( deviceObject->device(), 2, bvhElementIndicesBuffers[0]->resource(), bvhElementIndicesBuffers[0]->UAVDescription() );
			compute_bvh_firstTask->dispatch( commandList, dispatchsize( polygon->primitiveCount, 64 ), 1, 1 );

			resourceBarrier( commandList, {
											  bvhBuildTaskBuffer->resourceBarrierUAV(),
											  bvhElementIndicesBuffers[0]->resourceBarrierUAV(),
										  } );
		} );
		deviceObject->queueObject()->execute( computeCommandList.get() );

		ConstantBufferObject binningArgument( deviceObject->device(), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON );
		DownloaderObject ringRangesDownloader( deviceObject->device(), sizeof( uint32_t ) * 4 );

		// Setup Scan Args
		int scanArgCount = prefixScanIterationCount( nProcessBlocks );
		std::unique_ptr<ConstantBufferArrayObject> scanArgs( new ConstantBufferArrayObject( deviceObject->device(), sizeof( int32_t ), scanArgCount, D3D12_RESOURCE_STATE_COPY_DEST ) );

		computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
			std::vector<int32_t> offsets;
			for ( int i = 0; i < scanArgCount; ++i )
			{
				offsets.push_back( 1 << i );
			}
			scanArgs->upload( commandList, offsets );
			resourceBarrier( commandList, {scanArgs->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON )} );
		} );
		deviceObject->queueObject()->execute( computeCommandList.get() );

		int taskCount = 1;
		for ( int itr = 0; true; ++itr )
		{
			int consumeTaskCount = std::min( taskCount, nProcessBlocks );

			// Scan
			int nScanIteration = prefixScanIterationCount( consumeTaskCount );

			computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
				// Binning argument
				resourceBarrier( commandList, {binningArgument.resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST )} );
				binningArgument.upload( commandList, (uint32_t)consumeTaskCount );
				resourceBarrier( commandList, {binningArgument.resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON )} );

				// clear bin
				compute_bvh_clearBin->setPipelineState( commandList );
				compute_bvh_clearBin->setComputeRootSignature( commandList );
				heap->startNextHeapAndAssign( commandList, compute_bvh_clearBin->descriptorEnties() );
				heap->b( deviceObject->device(), 0, binningArgument.resource() );
				heap->u( deviceObject->device(), 0, binningBuffer->resource(), binningBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 1, bvhBuildTaskBuffer->resource(), bvhBuildTaskBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 2, bvhBuildTaskRingRanges[0]->resource(), bvhBuildTaskRingRanges[0]->UAVDescription() );
				compute_bvh_clearBin->dispatch( commandList, dispatchsize( consumeTaskCount, 64 ), 1, 1 );

				resourceBarrier( commandList, {bvhBuildTaskRingRanges[0]->resourceBarrierUAV()} );

				compute_bvh_consumeTask->setPipelineState( commandList );
				compute_bvh_consumeTask->setComputeRootSignature( commandList );
				heap->startNextHeapAndAssign( commandList, compute_bvh_consumeTask->descriptorEnties() );
				heap->b( deviceObject->device(), 0, binningArgument.resource() );
				heap->u( deviceObject->device(), 0, bvhBuildTaskBuffer->resource(), bvhBuildTaskBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 1, bvhBuildTaskRingRanges[0]->resource(), bvhBuildTaskRingRanges[0]->UAVDescription() );
				compute_bvh_consumeTask->dispatch( commandList, 1, 1, 1 );

				resourceBarrier( commandList, {bvhBuildTaskRingRanges[0]->resourceBarrierUAV()} );

				// Execution Count
				compute_bvh_executionCount->setPipelineState( commandList );
				compute_bvh_executionCount->setComputeRootSignature( commandList );
				heap->startNextHeapAndAssign( commandList, compute_bvh_executionCount->descriptorEnties() );
				heap->u( deviceObject->device(), 0, binningBuffer->resource(), binningBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 1, executionCountBuffer->resource(), executionCountBuffer->UAVDescription() );
				compute_bvh_executionCount->dispatch( commandList, dispatchsize( consumeTaskCount, 64 ), 1, 1 );

				resourceBarrier( commandList, {
												  executionCountBuffer->resourceBarrierUAV(),
												  executionCountBuffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE ),
											  } );

				// Scan Preapre for exclusive scan
				executionTableBuffers[0]->copyFrom( commandList, zeroU32.resource(), 0, 0, sizeof( uint32_t ) );
				executionTableBuffers[0]->copyFrom( commandList, executionCountBuffer->resource(), sizeof( uint32_t ), 0, sizeof( uint32_t ) * ( consumeTaskCount - 1 ) );

				resourceBarrier( commandList, {executionCountBuffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON )} );

				PIXBeginEvent( commandList, PIX_COLOR_DEFAULT, "Scan" );

				for ( int i = 0; i < nScanIteration; ++i )
				{
					compute_bvh_scan->setPipelineState( commandList );
					compute_bvh_scan->setComputeRootSignature( commandList );
					heap->startNextHeapAndAssign( commandList, compute_bvh_scan->descriptorEnties() );
					heap->b( deviceObject->device(), 0, binningArgument.resource() );
					heap->b( deviceObject->device(), 1, scanArgs->resource(), scanArgs->bytesStride(), scanArgs->bytesOffset( i ) );
					heap->u( deviceObject->device(), 0, executionTableBuffers[0]->resource(), executionTableBuffers[0]->UAVDescription() );
					heap->u( deviceObject->device(), 1, executionTableBuffers[1]->resource(), executionTableBuffers[1]->UAVDescription() );
					compute_bvh_scan->dispatch( commandList, dispatchsize( consumeTaskCount, 64 ), 1, 1 );

					std::swap( executionTableBuffers[0], executionTableBuffers[1] );
					resourceBarrier( commandList, {executionTableBuffers[0]->resourceBarrierUAV()} );
				}

				PIXEndEvent( commandList );

				// clear counter
				resourceBarrier( commandList, {executionIterator->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST )} );
				executionIterator->copyFrom( commandList, &zeroU32 );
				resourceBarrier( commandList, {executionIterator->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON )} );

				// Binning
				PIXBeginEvent( commandList, PIX_COLOR_DEFAULT, "Binning" );
				compute_bvh_binning->setPipelineState( commandList );
				compute_bvh_binning->setComputeRootSignature( commandList );
				heap->startNextHeapAndAssign( commandList, compute_bvh_binning->descriptorEnties() );
				heap->b( deviceObject->device(), 0, binningArgument.resource() );
				heap->u( deviceObject->device(), 0, executionCountBuffer->resource(), executionCountBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 1, executionTableBuffers[0]->resource(), executionTableBuffers[0]->UAVDescription() );
				heap->u( deviceObject->device(), 2, executionIterator->resource(), executionIterator->UAVDescription() );
				heap->u( deviceObject->device(), 3, binningBuffer->resource(), binningBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 4, bvhElementIndicesBuffers[0]->resource(), bvhElementIndicesBuffers[0]->UAVDescription() );
				heap->u( deviceObject->device(), 5, bvhElementBuffer->resource(), bvhElementBuffer->UAVDescription() );
				compute_bvh_binning->dispatch( commandList, deviceObject->totalLaneCount(), 1, 1 );
				PIXEndEvent( commandList );

				resourceBarrier( commandList, {
												  binningBuffer->resourceBarrierUAV(),
												  executionIterator->resourceBarrierUAV(),
											  } );

				// Select bin
				PIXBeginEvent( commandList, PIX_COLOR_DEFAULT, "Select bin" );
				compute_bvh_selectBin->setPipelineState( commandList );
				compute_bvh_selectBin->setComputeRootSignature( commandList );
				heap->startNextHeapAndAssign( commandList, compute_bvh_selectBin->descriptorEnties() );
				heap->b( deviceObject->device(), 0, binningArgument.resource() );
				heap->u( deviceObject->device(), 0, bvhBuildTaskBuffer->resource(), bvhBuildTaskBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 1, bvhBuildTaskRingRanges[1]->resource(), bvhBuildTaskRingRanges[1]->UAVDescription() );
				heap->u( deviceObject->device(), 2, binningBuffer->resource(), binningBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 3, bvhNodeBuffer->resource(), bvhNodeBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 4, bvhNodeCounterBuffer->resource(), bvhNodeCounterBuffer->UAVDescription() );
				compute_bvh_selectBin->dispatch( commandList, dispatchsize( consumeTaskCount, 64 ), 1, 1 );
				PIXEndEvent( commandList );

				resourceBarrier( commandList, {
												  bvhBuildTaskBuffer->resourceBarrierUAV(),
												  bvhBuildTaskRingRanges[1]->resourceBarrierUAV(),
												  binningBuffer->resourceBarrierUAV(),
												  bvhNodeBuffer->resourceBarrierUAV(),
												  bvhNodeCounterBuffer->resourceBarrierUAV(),
											  } );

				// clear counter
				resourceBarrier( commandList, {executionIterator->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST )} );
				executionIterator->copyFrom( commandList, &zeroU32 );
				resourceBarrier( commandList, {executionIterator->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON )} );

				// Reorder
				PIXBeginEvent( commandList, PIX_COLOR_DEFAULT, "Reorder" );
				compute_bvh_reorder->setPipelineState( commandList );
				compute_bvh_reorder->setComputeRootSignature( commandList );
				heap->startNextHeapAndAssign( commandList, compute_bvh_reorder->descriptorEnties() );
				heap->b( deviceObject->device(), 0, binningArgument.resource() );
				heap->u( deviceObject->device(), 0, executionCountBuffer->resource(), executionCountBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 1, executionTableBuffers[0]->resource(), executionTableBuffers[0]->UAVDescription() );
				heap->u( deviceObject->device(), 2, executionIterator->resource(), executionIterator->UAVDescription() );
				heap->u( deviceObject->device(), 3, binningBuffer->resource(), binningBuffer->UAVDescription() );
				heap->u( deviceObject->device(), 4, bvhElementIndicesBuffers[0]->resource(), bvhElementIndicesBuffers[0]->UAVDescription() );
				heap->u( deviceObject->device(), 5, bvhElementIndicesBuffers[1]->resource(), bvhElementIndicesBuffers[1]->UAVDescription() );
				heap->u( deviceObject->device(), 6, bvhElementBuffer->resource(), bvhElementBuffer->UAVDescription() );
				compute_bvh_reorder->dispatch( commandList, deviceObject->totalLaneCount(), 1, 1 );
				PIXEndEvent( commandList );

				resourceBarrier( commandList, {bvhElementIndicesBuffers[1]->resourceBarrierUAV()} );

				// reading task count
				resourceBarrier( commandList, {
												  bvhBuildTaskRingRanges[0]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE ),
												  bvhBuildTaskRingRanges[1]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE ),
											  } );
				commandList->CopyBufferRegion(
					ringRangesDownloader.resource(), 0,
					bvhBuildTaskRingRanges[0]->resource(), 0,
					sizeof( uint32_t ) * 2 );
				commandList->CopyBufferRegion(
					ringRangesDownloader.resource(), sizeof( uint32_t ) * 2,
					bvhBuildTaskRingRanges[1]->resource(), 0,
					sizeof( uint32_t ) * 2 );
				resourceBarrier( commandList, {
												  bvhBuildTaskRingRanges[0]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON ),
												  bvhBuildTaskRingRanges[1]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON ),
											  } );
			} );

			deviceObject->queueObject()->execute( computeCommandList.get() );

			// wait for CPU read.
			std::shared_ptr<FenceObject> fence = deviceObject->queueObject()->fence( deviceObject->device() );
			fence->wait();

			uint32_t ranges[4] = {};
			ringRangesDownloader.map( [&]( const void* p ) {
				memcpy( ranges, p, sizeof( uint32_t ) * 4 );
			} );

			// auto RingRanges = bvhBuildTaskRingRanges[0]->synchronizedDownload<uint32_t>(deviceObject->device(), deviceObject->queueObject());
			//auto executionCount = executionCountBuffer->synchronizedDownload<uint32_t>(deviceObject->device(), deviceObject->queueObject());
			//auto executionTable = executionTableBuffers[0]->synchronizedDownload<uint32_t>(deviceObject->device(), deviceObject->queueObject());
			//auto binningBufferValue = binningBuffer->synchronizedDownload<BinningBuffer>(deviceObject->device(), deviceObject->queueObject());
			//auto taks = bvhBuildTaskBuffer->synchronizedDownload<BuildTask>(deviceObject->device(), deviceObject->queueObject());
			//auto idx = bvhElementIndicesBuffers[1]->synchronizedDownload<uint32_t>(deviceObject->device(), deviceObject->queueObject());
			//printf("");
			//nodes = bvhNodeBuffer->synchronizedDownload<BvhNode>(deviceObject->device(), deviceObject->queueObject());
			//auto nodeCounter = bvhNodeCounterBuffer->synchronizedDownload<uint32_t>(deviceObject->device(), deviceObject->queueObject());
			//auto debugValue = debugBuffer->synchronizedDownload<uint32_t>(deviceObject->device(), deviceObject->queueObject());

			bool aPartDone = ranges[0] == ranges[1];
			bool bPartDone = ranges[2] == ranges[3];
			if ( aPartDone && bPartDone )
			{
				taskCount = 0;
			}
			else if ( aPartDone )
			{
				taskCount = ringBufferCount( ranges[2], ranges[3], bvhBuildTaskBuffer->itemCount() );
				// swap input and output
				// swap(ranges[0], ranges[2]);
				// swap(ranges[1], ranges[3]);
				// ranges[0] = ranges[1];
				// ranges[1] = ranges[1];
				std::swap( bvhBuildTaskRingRanges[0], bvhBuildTaskRingRanges[1] );
				computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
					resourceBarrier( commandList, {
													  bvhBuildTaskRingRanges[0]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE ),
													  bvhBuildTaskRingRanges[1]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST ),
												  } );
					commandList->CopyBufferRegion(
						bvhBuildTaskRingRanges[1]->resource(), 0,				   // dst
						bvhBuildTaskRingRanges[0]->resource(), sizeof( uint32_t ), // src
						sizeof( uint32_t ) );
					commandList->CopyBufferRegion(
						bvhBuildTaskRingRanges[1]->resource(), sizeof( uint32_t ),
						bvhBuildTaskRingRanges[0]->resource(), sizeof( uint32_t ),
						sizeof( uint32_t ) );
					resourceBarrier( commandList, {
													  bvhBuildTaskRingRanges[0]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON ),
													  bvhBuildTaskRingRanges[1]->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
												  } );
				} );
				deviceObject->queueObject()->execute( computeCommandList.get() );

				// also need to swap indices input and output
				std::swap( bvhElementIndicesBuffers[0], bvhElementIndicesBuffers[1] );
			}
			else
			{
				taskCount = ringBufferCount( ranges[0], ranges[1], bvhBuildTaskBuffer->itemCount() );
			}

			heap->clear();

			if ( taskCount == 0 )
			{
				break;
			}
		}
		bvhElementIndicesBuffers[1] = std::unique_ptr<BufferObjectUAV>();

		printf("bvh done %.3f ms\n", 1000.0 * sw.elapsed());
	}

	std::unique_ptr<BufferObjectUAV> vertexBuffer;
	std::unique_ptr<BufferObjectUAV> indexBuffer;
	std::unique_ptr<BufferObjectUAV> bvhNodeBuffer;
	std::unique_ptr<BufferObjectUAV> bvhElementIndicesBuffers[2];
};

class Rt
{
public:
	Rt( DeviceObject* deviceObject, const lwh::Polygon* polygon, int width, int height ) 
		: _width( width ), _height( height ), _deviceObject( deviceObject ), _polygon(polygon)
	{
		pr::Stopwatch sw;

		_timestamp = std::unique_ptr<TimestampObject>(new TimestampObject(deviceObject->device(), 128));

		compute_bvh_traverse = std::unique_ptr<ComputeObject>(new ComputeObject());
		compute_bvh_traverse->u(0);
		compute_bvh_traverse->u(1);
		compute_bvh_traverse->u(2);
		compute_bvh_traverse->u(3);
		compute_bvh_traverse->u(4);
		compute_bvh_traverse->b(0);
		compute_bvh_traverse->loadShaderAndBuild(deviceObject->device(), pr::GetDataPath("bvh_traverse.cso").c_str());

		computeCommandList = std::unique_ptr<CommandObject>( new CommandObject( deviceObject->device(), D3D12_COMMAND_LIST_TYPE_DIRECT ) );
		computeCommandList->setName( L"Compute" );
		heap = std::unique_ptr<StackDescriptorHeapObject>( new StackDescriptorHeapObject( deviceObject->device(), 128 ) );

		colorRGBX8Buffer = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), _width * _height * sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );
		downloader = std::unique_ptr<DownloaderObject>( new DownloaderObject( deviceObject->device(), _width * _height * sizeof( uint32_t ) ) );

		_argument = std::unique_ptr<ConstantBufferObject>(new ConstantBufferObject(deviceObject->device(), sizeof(Arguments), D3D12_RESOURCE_STATE_COMMON));

		texture = std::unique_ptr<pr::ITexture>(pr::CreateTexture());

		builder = std::unique_ptr<GPUBvhBuilder>(new GPUBvhBuilder( deviceObject, polygon ));

		//printf("setup vertex and indices %.3f ( %lld bytes, %lld bytes )ms\n", 1000.0 * sw.elapsed(), vertexBuffer->bytes(), indexBuffer->bytes() );
		//printf("");
	}
	int width() const { return _width; }
	int height() const { return _height; }

	void rebuild()
	{
		builder = std::unique_ptr<GPUBvhBuilder>();
		builder = std::unique_ptr<GPUBvhBuilder>(new GPUBvhBuilder(_deviceObject, _polygon));
	}

	void step()
	{
		// nodes = bvhNodeBuffer->synchronizedDownload<BvhNode>(deviceObject->device(), deviceObject->queueObject());
		_timestamp->clear();

		computeCommandList->storeCommand([&](ID3D12GraphicsCommandList* commandList) {
			// Update Argument
			resourceBarrier( commandList, {_argument->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST )} );
			Arguments arg = {};
			arg.cb_width = _width;
			arg.cb_height = _height;
			memcpy( arg.cb_inverseVP, glm::value_ptr( glm::transpose( _inverseVP ) ), sizeof( _inverseVP ) );

			_argument->upload( commandList, arg );

			resourceBarrier( commandList, {
											  _argument->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
										  } );
			_timestamp->stampBeg( commandList, "BVH Traverse" );

			// Execute
			compute_bvh_traverse->setPipelineState( commandList );
			compute_bvh_traverse->setComputeRootSignature( commandList );
			heap->startNextHeapAndAssign( commandList, compute_bvh_traverse->descriptorEnties() );
			heap->u( _deviceObject->device(), 0, colorRGBX8Buffer->resource(), colorRGBX8Buffer->UAVDescription() );
			heap->u( _deviceObject->device(), 1, builder->vertexBuffer->resource(), builder->vertexBuffer->UAVDescription() );
			heap->u( _deviceObject->device(), 2, builder->indexBuffer->resource(), builder->indexBuffer->UAVDescription() );
			heap->u( _deviceObject->device(), 3, builder->bvhNodeBuffer->resource(), builder->bvhNodeBuffer->UAVDescription() );
			heap->u( _deviceObject->device(), 4, builder->bvhElementIndicesBuffers[0]->resource(), builder->bvhElementIndicesBuffers[0]->UAVDescription() );
			
			heap->b(_deviceObject->device(), 0, _argument->resource() );
			compute_bvh_traverse->dispatch( commandList, dispatchsize( _width * _height, 64 ), 1, 1 );

			_timestamp->stampEnd( commandList );

			resourceBarrier( commandList, {colorRGBX8Buffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE )} );
			colorRGBX8Buffer->copyTo( commandList, downloader.get() );
			resourceBarrier( commandList, {colorRGBX8Buffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON )} );

			_timestamp->resolve( commandList );
		});
		_deviceObject->queueObject()->execute(computeCommandList.get());

		// wait for CPU read.
		{
			std::shared_ptr<FenceObject> fence = _deviceObject->queueObject()->fence( _deviceObject->device() );
			fence->wait();
		}
		heap->clear();

		downloader->map( [&]( const void* p ) {
			texture->uploadAsRGBA8( (const uint8_t*)p, _width, _height );
		} );
		timestampSpans = _timestamp->download(_deviceObject->queueObject()->queue() );

		_deviceObject->present();
	}
	pr::ITexture* getTexture()
	{
		return texture.get();
	}
	void setMatrixProjViewMatrix( glm::mat4 proj, glm::mat4 view )
	{
		_inverseVP = glm::inverse( proj * view );
	}
	void OnImGUI()
	{
		for (int i = 0; i < timestampSpans.size(); ++i)
		{
			ImGui::Text("%s, %.3f ms", timestampSpans[i].label.c_str(), timestampSpans[i].durationMS);
		}
	}

	std::vector<BvhNode> nodes;
private:
	int _width = 0, _height = 0;
	glm::mat4 _inverseVP;
	DeviceObject* _deviceObject;

	std::unique_ptr<GPUBvhBuilder> builder;

	std::unique_ptr<CommandObject> computeCommandList;
	std::unique_ptr<StackDescriptorHeapObject> heap;
	
	std::unique_ptr<ComputeObject> compute_bvh_traverse;

	std::unique_ptr<BufferObjectUAV> accumulationBuffer;
	std::unique_ptr<BufferObjectUAV> colorRGBX8Buffer;
	std::unique_ptr<DownloaderObject> downloader;
	std::unique_ptr<pr::ITexture> texture;

	std::unique_ptr<ConstantBufferObject> _argument;

	std::unique_ptr<TimestampObject> _timestamp;
	std::vector<TimestampSpan> timestampSpans;

	const lwh::Polygon* _polygon;
};
void drawNode( const std::vector<BvhNode>& nodes, int node, int depth = 0 )
{
	if ( nodes.empty() )
	{
		return;
	}
	if ( 5 < depth )
	{
		return;
	}
	static std::vector<glm::u8vec3> colors = {
		{255, 0, 0},
		{0, 255, 0},
		{0, 0, 255},
		{0, 255, 255},
		{255, 0, 255},
		{255, 255, 0},
	};
	auto c = colors[depth % colors.size()];
	glm::vec3 lowerL( nodes[node].lowerL[0], nodes[node].lowerL[1], nodes[node].lowerL[2] );
	glm::vec3 upperL( nodes[node].upperL[0], nodes[node].upperL[1], nodes[node].upperL[2] );
	pr::DrawAABB( lowerL, upperL, c );

	glm::vec3 lowerR( nodes[node].lowerR[0], nodes[node].lowerR[1], nodes[node].lowerR[2] );
	glm::vec3 upperR( nodes[node].upperR[0], nodes[node].upperR[1], nodes[node].upperR[2] );
	pr::DrawAABB( lowerR, upperR, c );

	if ( ( nodes[node].indexL[0] & 0x80000000 ) == 0 )
	{
		drawNode( nodes, nodes[node].indexL[0], depth + 1 );
	}
	if ( ( nodes[node].indexR[0] & 0x80000000 ) == 0 )
	{
		drawNode( nodes, nodes[node].indexR[0], depth + 1 );
	}
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

	//for ( int i = 0;; ++i )
	//{
	//	for ( auto d : devices )
	//	{
	//		printf( "run : %s\n", wstring_to_string( d->deviceName() ).c_str() );
	//		run( d.get() );
	//	}
	//}

	BinaryLoader loader;
	loader.load( "../prim/out/box.json" );
	loader.push_back( '\0' );

	Stopwatch sw;
	rapidjson::Document d;
	d.ParseInsitu( (char*)loader.data() );
	PR_ASSERT( d.HasParseError() == false );

	auto lwhPolygon = lwh::load( d );

	Config config;
	config.ScreenWidth = 1280;
	config.ScreenHeight = 720;
	config.SwapInterval = 1;
	Initialize( config );

	std::shared_ptr<Rt> rt;
	//  = std::shared_ptr<Rt>(new Rt(devices[0].get(), lwhPolygon.polygon, GetScreenWidth(), GetScreenHeight()));

	Camera3D camera;
	camera.origin = {4, 4, 4};
	camera.lookat = {0, 0, 0};
	camera.zNear = 0.1f;
	camera.zFar = 100.0f;
	// camera.perspective = 0.1f;
	camera.zUp = false;

	double e = GetElapsedTime();

	bool showWire = false;
	bool reBuild = false;

	while ( pr::NextFrame() == false )
	{
		if ( IsImGuiUsingMouse() == false )
		{
			UpdateCameraBlenderLike( &camera );
		}

		// ClearBackground( 0.1f, 0.1f, 0.1f, 1 );


		if (rt == nullptr || rt->width() != GetScreenWidth() || rt->height() != GetScreenHeight())
		{
			rt = std::shared_ptr<Rt>();
			rt = std::shared_ptr<Rt>(new Rt(devices[0].get(), lwhPolygon.polygon, GetScreenWidth(), GetScreenHeight()));
		}
		if (reBuild)
		{
			rt->rebuild();
		}
		glm::mat4 proj, view;
		GetCameraMatrix( camera, &proj, &view );
		rt->setMatrixProjViewMatrix( proj, view );
		rt->step();

		ClearBackground( rt->getTexture() );

		BeginCamera( camera );

		PushGraphicState();

		DrawGrid( GridAxis::XZ, 1.0f, 10, {128, 128, 128} );
		DrawXYZAxis( 1.0f );

		if (showWire)
		{
			BeginCameraWithObjectTransform(camera, lwhPolygon.polygon->xform);
			PrimBegin(PrimitiveMode::Lines);
			for (int i = 0; i < lwhPolygon.polygon->indices.size(); i += 3)
			{
				int a = lwhPolygon.polygon->indices[i];
				int b = lwhPolygon.polygon->indices[i + 1];
				int c = lwhPolygon.polygon->indices[i + 2];
				glm::u8vec3 color = {
					255,
					255,
					255 };
				PrimVertex(lwhPolygon.polygon->P[a], color);
				PrimVertex(lwhPolygon.polygon->P[b], color);
				PrimVertex(lwhPolygon.polygon->P[b], color);
				PrimVertex(lwhPolygon.polygon->P[c], color);
				PrimVertex(lwhPolygon.polygon->P[c], color);
				PrimVertex(lwhPolygon.polygon->P[a], color);
			}
			PrimEnd();

			drawNode(rt->nodes, 0);

			EndCamera();
		}

		PopGraphicState();
		EndCamera();

		BeginImGui();

		ImGui::SetNextWindowSize( {500, 800}, ImGuiCond_Once );
		ImGui::Begin( "Panel" );
		ImGui::Text( "fps = %f", GetFrameRate() );
		ImGui::Checkbox("showWire", &showWire);
		ImGui::Checkbox("reBuild", &reBuild);
		
		rt->OnImGUI();

		ImGui::End();

		EndImGui();
	}

	pr::CleanUp();
}
