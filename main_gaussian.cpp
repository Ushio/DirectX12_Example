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

	Image2DRGBA8 image;
	image.load( "../image/cat.png" );

	std::shared_ptr<CommandObject> computeCommandList( new CommandObject( deviceObject->device(), D3D12_COMMAND_LIST_TYPE_DIRECT ) );
	computeCommandList->setName( L"Compute" );

	typedef glm::u8vec4 IOImagePixelType;
	typedef glm::vec4 WorkingPixelType;

	uint64_t numberOfElement = image.width() * image.height();
	uint64_t ioImageBytes = sizeof( IOImagePixelType ) * numberOfElement;
	uint64_t workImageBytes = sizeof( WorkingPixelType ) * numberOfElement;

	std::unique_ptr<ConstantBufferObject> arguments_H( new ConstantBufferObject( deviceObject->device(), sizeof( Arguments ), D3D12_RESOURCE_STATE_COPY_DEST ) );
	std::unique_ptr<ConstantBufferObject> arguments_V( new ConstantBufferObject( deviceObject->device(), sizeof( Arguments ), D3D12_RESOURCE_STATE_COPY_DEST ) );
	arguments_H->setName( L"arguments_H" );
	arguments_V->setName( L"arguments_V" );

	std::unique_ptr<BufferObjectUAV> ioImageBuffer( new BufferObjectUAV( deviceObject->device(), ioImageBytes, sizeof( IOImagePixelType ), D3D12_RESOURCE_STATE_COPY_DEST ) );
	std::unique_ptr<BufferObjectUAV> valueBuffer0( new BufferObjectUAV( deviceObject->device(), workImageBytes, sizeof( WorkingPixelType ), D3D12_RESOURCE_STATE_COMMON ) );
	std::unique_ptr<BufferObjectUAV> valueBuffer1( new BufferObjectUAV( deviceObject->device(), workImageBytes, sizeof( WorkingPixelType ), D3D12_RESOURCE_STATE_COMMON ) );
	ioImageBuffer->setName( L"ioImageBuffer" );
	valueBuffer0->setName( L"valueBuffer0" );
	valueBuffer1->setName( L"valueBuffer1" );

	std::unique_ptr<UploaderObject> imageUploader( new UploaderObject( deviceObject->device(), ioImageBytes ) );
	imageUploader->map( [&]( void* p ) {
		memcpy( p, image.data(), ioImageBytes );
	} );
	std::unique_ptr<DownloaderObject> imageDownloader( new DownloaderObject( deviceObject->device(), ioImageBytes ) );
	imageUploader->setName( L"imageUploader" );
	imageDownloader->setName( L"imageDownloader" );

	float sigma = 20.0f;

	float sum = 0.0f;
	std::vector<float> kernelstore;
	for ( int i = 0;; ++i )
	{
		float g = std::exp( -i * i / ( 2.0f * sigma * sigma ) );
		sum += g;
		kernelstore.push_back( g );
		if ( ( i % 2 == 1 ) && g < ( 1.0f / 512.0f ) )
		{
			break;
		}
	}

	// -3, -2, -1, 0, 1, 2, 3
	// ^^^^^^^^^^ are missing and sum x 2 conains overwaped kernelstore[0] == 1.0 so remove 1.0
	sum = sum * 2.0f - 1.0f;
	for ( float& w : kernelstore )
	{
		w /= sum;
	}

	std::unique_ptr<BufferObjectUAV> kernel( new BufferObjectUAV( deviceObject->device(), sizeof( float ) * kernelstore.size(), sizeof( float ), D3D12_RESOURCE_STATE_COPY_DEST ) );
	std::unique_ptr<UploaderObject> kernelUploader( new UploaderObject( deviceObject->device(), sizeof( float ) * kernelstore.size() ) );
	kernelUploader->map( [&]( void* p ) {
		memcpy( p, kernelstore.data(), sizeof( float ) * kernelstore.size() );
	} );

	std::unique_ptr<StackDescriptorHeapObject> heap( new StackDescriptorHeapObject( deviceObject->device(), 32 ) );

	std::unique_ptr<ComputeObject> degammaCompute( new ComputeObject() );
	degammaCompute->u( 0 );
	degammaCompute->u( 1 );
	degammaCompute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "gaussian_degamma.cso" ).c_str() );

	std::unique_ptr<ComputeObject> gaussianCompute( new ComputeObject() );
	gaussianCompute->u( 0 );
	gaussianCompute->u( 1 );
	gaussianCompute->u( 2 );
	gaussianCompute->b( 0 );
	gaussianCompute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "gaussian.cso" ).c_str() );

	std::unique_ptr<ComputeObject> gammaCompute( new ComputeObject() );
	gammaCompute->u( 0 );
	gammaCompute->u( 1 );
	gammaCompute->loadShaderAndBuild( deviceObject->device(), GetDataPath( "gaussian_gamma.cso" ).c_str() );

	std::unique_ptr<TimestampObject> stumper( new TimestampObject( deviceObject->device(), 128 ) );

	computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
		stumper->stamp( commandList, "upload" );

		// upload
		arguments_H->upload<Arguments>( commandList, {image.width(), image.height(), 1, 0} );
		arguments_V->upload<Arguments>( commandList, {image.width(), image.height(), 0, 1} );
		kernel->copyFrom( commandList, kernelUploader.get() );
		ioImageBuffer->copyFrom( commandList, imageUploader.get() );

		// upload memory barrier
		resourceBarrier( commandList, {
										  arguments_H->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
										  arguments_V->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
										  kernel->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
										  ioImageBuffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
									  } );

		stumper->stamp( commandList, "degamma" );

		// Degamma
		degammaCompute->setPipelineState( commandList );
		degammaCompute->setComputeRootSignature( commandList );
		heap->startNextHeapAndAssign( commandList, degammaCompute->descriptorEnties() );
		heap->u( deviceObject->device(), 0, ioImageBuffer->resource(), ioImageBuffer->UAVDescription() );
		heap->u( deviceObject->device(), 1, valueBuffer0->resource(), valueBuffer0->UAVDescription() );

		degammaCompute->dispatch( commandList, dispatchsize( numberOfElement, 64 ), 1, 1 );

		resourceBarrier( commandList, {valueBuffer0->resourceBarrierUAV()} );

		stumper->stamp( commandList, "gaussian H" );

		// Gaussian Pipeline
		gaussianCompute->setPipelineState( commandList );
		gaussianCompute->setComputeRootSignature( commandList );

		// Horizontal
		heap->startNextHeapAndAssign( commandList, gaussianCompute->descriptorEnties() );
		heap->u( deviceObject->device(), 0, valueBuffer0->resource(), valueBuffer0->UAVDescription() );
		heap->u( deviceObject->device(), 1, valueBuffer1->resource(), valueBuffer1->UAVDescription() );
		heap->u( deviceObject->device(), 2, kernel->resource(), kernel->UAVDescription() );
		heap->b( deviceObject->device(), 0, arguments_H->resource() );
		gaussianCompute->dispatch( commandList, dispatchsize( numberOfElement, 64 ), 1, 1 );

		// Just valueBuffer1 will be modified.
		resourceBarrier( commandList, {valueBuffer1->resourceBarrierUAV()} );

		stumper->stamp( commandList, "gaussian V" );

		// Vertical
		heap->startNextHeapAndAssign( commandList, gaussianCompute->descriptorEnties() );
		heap->u( deviceObject->device(), 0, valueBuffer1->resource(), valueBuffer1->UAVDescription() );
		heap->u( deviceObject->device(), 1, valueBuffer0->resource(), valueBuffer0->UAVDescription() );
		heap->u( deviceObject->device(), 2, kernel->resource(), kernel->UAVDescription() );
		heap->b( deviceObject->device(), 0, arguments_V->resource() );
		gaussianCompute->dispatch( commandList, dispatchsize( numberOfElement, 64 ), 1, 1 );

		// Just valueBuffer0 will be modified.
		resourceBarrier( commandList, {valueBuffer0->resourceBarrierUAV()} );

		stumper->stamp( commandList, "gamma" );

		// Gamma
		gammaCompute->setPipelineState( commandList );
		gammaCompute->setComputeRootSignature( commandList );
		heap->startNextHeapAndAssign( commandList, gammaCompute->descriptorEnties() );
		heap->u( deviceObject->device(), 0, valueBuffer0->resource(), valueBuffer0->UAVDescription() );
		heap->u( deviceObject->device(), 1, ioImageBuffer->resource(), ioImageBuffer->UAVDescription() );
		gammaCompute->dispatch( commandList, dispatchsize( numberOfElement, 64 ), 1, 1 );

		resourceBarrier( commandList, {ioImageBuffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE )} );

		stumper->stamp( commandList, "download" );

		// download
		ioImageBuffer->copyTo( commandList, imageDownloader.get() );
		resourceBarrier( commandList, {ioImageBuffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON )} );

		stumper->stamp( commandList, "end" );
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
	imageDownloader->map( [&]( const void* p ) {
		memcpy( image.data(), p, ioImageBytes );
		image.save( "out.png" );
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
