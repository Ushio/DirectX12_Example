#include "EzDx.hpp"
#include "pr.hpp"
#include "lwHoudiniLoader.hpp"

struct Arguments
{
	int cb_width;
	int cb_height;
	int cb_pad0;
	int cb_pad1;
	float cb_inverseVP[16];
};

class Rt
{
public:
	Rt( DeviceObject* deviceObject, const lwh::Polygon* polygon, int width, int height ) : _width( width ), _height( height ), _deviceObject( deviceObject )
	{
		computeCommandList = std::unique_ptr<CommandObject>( new CommandObject( deviceObject->device(), D3D12_COMMAND_LIST_TYPE_DIRECT ) );
		computeCommandList->setName( L"Compute" );
		heap = std::unique_ptr<StackDescriptorHeapObject>( new StackDescriptorHeapObject( deviceObject->device(), 32 ) );

		colorRGBX8Buffer = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), _width * _height * sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );
		downloader = std::unique_ptr<DownloaderObject>( new DownloaderObject( deviceObject->device(), _width * _height * sizeof( uint32_t ) ) );

		compute = std::unique_ptr<ComputeObject>( new ComputeObject() );
		compute->u( 0 );
		compute->b( 0 );
		compute->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "linear_rt.cso" ).c_str() );

		_argument = std::unique_ptr<ConstantBufferObject>( new ConstantBufferObject( deviceObject->device(), sizeof( Arguments ), D3D12_RESOURCE_STATE_COMMON ) );

		texture = std::unique_ptr<pr::ITexture>( pr::CreateTexture() );
	}
	int width() const { return _width; }
	int height() const { return _height; }

	void step()
	{
		DeviceObject* deviceObject = _deviceObject;

		heap->clear();

		computeCommandList->storeCommand( [&]( ID3D12GraphicsCommandList* commandList ) {
			// Update Argument
			resourceBarrier( commandList, {_argument->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE )} );
			Arguments arg = {};
			arg.cb_width = _width;
			arg.cb_height = _height;
			memcpy( arg.cb_inverseVP, glm::value_ptr( glm::transpose( _inverseVP ) ), sizeof( _inverseVP ) );

			_argument->upload( commandList, arg );

			resourceBarrier( commandList, {
											  _argument->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ),
										  } );

			// Execute
			compute->setPipelineState( commandList );
			compute->setComputeRootSignature( commandList );
			heap->startNextHeapAndAssign( commandList, compute->descriptorEnties() );
			heap->u( deviceObject->device(), 0, colorRGBX8Buffer->resource(), colorRGBX8Buffer->UAVDescription() );
			heap->b( deviceObject->device(), 0, _argument->resource() );
			compute->dispatch( commandList, dispatchsize( _width * _height, 64 ), 1, 1 );

			resourceBarrier( commandList, {colorRGBX8Buffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE )} );
			colorRGBX8Buffer->copyTo( commandList, downloader.get() );
			resourceBarrier( commandList, {colorRGBX8Buffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON )} );
		} );

		deviceObject->queueObject()->execute( computeCommandList.get() );

		// wait for CPU read.
		{
			std::shared_ptr<FenceObject> fence = deviceObject->queueObject()->fence( deviceObject->device() );
			fence->wait();
		}

		downloader->map( [&]( const void* p ) {
			texture->uploadAsRGBA8( (const uint8_t*)p, _width, _height );
		} );

		deviceObject->present();
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
	}

private:
	int _width = 0, _height = 0;
	glm::mat4 _inverseVP;
	DeviceObject* _deviceObject;

	std::unique_ptr<CommandObject> computeCommandList;
	std::unique_ptr<StackDescriptorHeapObject> heap;
	std::unique_ptr<ComputeObject> compute;

	std::unique_ptr<BufferObjectUAV> accumulationBuffer;
	std::unique_ptr<BufferObjectUAV> colorRGBX8Buffer;
	std::unique_ptr<DownloaderObject> downloader;
	std::unique_ptr<pr::ITexture> texture;

	std::unique_ptr<ConstantBufferObject> _argument;
};

void run( DeviceObject* deviceObject )
{
	using namespace pr;

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

	std::shared_ptr<Rt> rt = std::shared_ptr<Rt>( new Rt( devices[0].get(), lwhPolygon.polygon, GetScreenWidth(), GetScreenHeight() ) );

	Camera3D camera;
	camera.origin = {4, 4, 4};
	camera.lookat = {0, 0, 0};
	camera.zNear = 0.1f;
	camera.zFar = 100.0f;
	// camera.perspective = 0.1f;
	camera.zUp = false;

	double e = GetElapsedTime();

	while ( pr::NextFrame() == false )
	{
		if ( IsImGuiUsingMouse() == false )
		{
			UpdateCameraBlenderLike( &camera );
		}

		// ClearBackground( 0.1f, 0.1f, 0.1f, 1 );
		glm::mat4 proj, view;
		GetCameraMatrix( camera, &proj, &view );
		rt->setMatrixProjViewMatrix( proj, view );
		rt->step();
		CameraRayGenerator gg( view, proj, GetScreenWidth(), GetScreenHeight() );
		glm::vec3 ro, rd;
		gg.shoot( &ro, &rd, 0, 0 );

		ClearBackground( rt->getTexture() );

		BeginCamera( camera );

		PushGraphicState();

		DrawGrid( GridAxis::XZ, 1.0f, 10, {128, 128, 128} );
		DrawXYZAxis( 1.0f );

		BeginCameraWithObjectTransform( camera, lwhPolygon.polygon->xform );
		PrimBegin( PrimitiveMode::Lines );
		for ( int i = 0; i < lwhPolygon.polygon->indices.size(); i += 3 )
		{
			int a = lwhPolygon.polygon->indices[i];
			int b = lwhPolygon.polygon->indices[i + 1];
			int c = lwhPolygon.polygon->indices[i + 2];
			glm::u8vec3 color = {
				255,
				255,
				255};
			PrimVertex( lwhPolygon.polygon->P[a], color );
			PrimVertex( lwhPolygon.polygon->P[b], color );
			PrimVertex( lwhPolygon.polygon->P[b], color );
			PrimVertex( lwhPolygon.polygon->P[c], color );
			PrimVertex( lwhPolygon.polygon->P[c], color );
			PrimVertex( lwhPolygon.polygon->P[a], color );
		}
		PrimEnd();
		EndCamera();

		PopGraphicState();
		EndCamera();

		BeginImGui();

		ImGui::SetNextWindowSize( {500, 800}, ImGuiCond_Once );
		ImGui::Begin( "Panel" );
		ImGui::Text( "fps = %f", GetFrameRate() );

		rt->OnImGUI();

		ImGui::End();

		EndImGui();
	}

	pr::CleanUp();
}
