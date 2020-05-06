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

struct BvhElement
{
	int lower[3];
	int upper[3];
	float centeroid[3];
};
struct BuildTask
{
	int lower[3];
	int upper[3];
	int geomBeg;
	int geomEnd;
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

class Rt
{
public:
	Rt( DeviceObject* deviceObject, const lwh::Polygon* polygon, int width, int height ) 
		: _width( width ), _height( height ), _deviceObject( deviceObject ), _polygon(polygon)
	{
		computeCommandList = std::unique_ptr<CommandObject>( new CommandObject( deviceObject->device(), D3D12_COMMAND_LIST_TYPE_DIRECT ) );
		computeCommandList->setName( L"Compute" );
		heap = std::unique_ptr<StackDescriptorHeapObject>( new StackDescriptorHeapObject( deviceObject->device(), 32 ) );

		colorRGBX8Buffer = std::unique_ptr<BufferObjectUAV>( new BufferObjectUAV( deviceObject->device(), _width * _height * sizeof( uint32_t ), sizeof( uint32_t ), D3D12_RESOURCE_STATE_COMMON ) );
		downloader = std::unique_ptr<DownloaderObject>( new DownloaderObject( deviceObject->device(), _width * _height * sizeof( uint32_t ) ) );

		compute = std::unique_ptr<ComputeObject>( new ComputeObject() );
		compute->u( 0 );
		compute->u( 1 );
		compute->u( 2 );
		compute->b( 0 );
		compute->loadShaderAndBuild( deviceObject->device(), pr::GetDataPath( "linear_rt.cso" ).c_str() );

		compute_bvh_element = std::unique_ptr<ComputeObject>(new ComputeObject());
		compute_bvh_element->u(0);
		compute_bvh_element->u(1);
		compute_bvh_element->u(2);
		compute_bvh_element->loadShaderAndBuild(deviceObject->device(), pr::GetDataPath("bvh_element.cso").c_str());

		compute_bvh_build = std::unique_ptr<ComputeObject>(new ComputeObject());
		compute_bvh_build->u(0);
		compute_bvh_build->u(1);
		compute_bvh_build->u(2);
		compute_bvh_build->u(3);
		compute_bvh_build->loadShaderAndBuild(deviceObject->device(), pr::GetDataPath("bvh_build.cso").c_str());

		bvhElementBuffer = std::unique_ptr<BufferObjectUAV>(new BufferObjectUAV(deviceObject->device(), _polygon->primitiveCount * sizeof(BvhElement), sizeof(BvhElement), D3D12_RESOURCE_STATE_COMMON));

		for(int i = 0 ; i < 2 ; ++i)
		{
			bvhBuildTaskBuffers[i] = std::unique_ptr<BufferObjectUAV>(new BufferObjectUAV(deviceObject->device(), _polygon->primitiveCount * sizeof(BuildTask), sizeof(BuildTask), D3D12_RESOURCE_STATE_COMMON, true, D3D12_RESOURCE_STATE_COPY_DEST ));
		}

		_argument = std::unique_ptr<ConstantBufferObject>( new ConstantBufferObject( deviceObject->device(), sizeof( Arguments ), D3D12_RESOURCE_STATE_COMMON ) );

		texture = std::unique_ptr<pr::ITexture>( pr::CreateTexture() );

		_timestamp = std::unique_ptr<TimestampObject>(new TimestampObject(deviceObject->device(), 128));

		uint32_t vBytes = polygon->P.size() * sizeof(glm::vec3);
		uint32_t iBytes = polygon->indices.size() * sizeof(uint32_t);
		vertexBuffer = std::unique_ptr<BufferObjectUAV>(new BufferObjectUAV(deviceObject->device(), vBytes, sizeof(glm::vec3), D3D12_RESOURCE_STATE_COPY_DEST));
		indexBuffer  = std::unique_ptr<BufferObjectUAV>(new BufferObjectUAV(deviceObject->device(), iBytes, sizeof(uint32_t), D3D12_RESOURCE_STATE_COPY_DEST));
		UploaderObject v_uploader(deviceObject->device(), vBytes);
		UploaderObject i_uploader(deviceObject->device(), iBytes);
		v_uploader.map([&](void* p) {
			memcpy(p, polygon->P.data(), vBytes);
		});
		i_uploader.map([&](void* p) {
			memcpy(p, polygon->indices.data(), iBytes);
		});
		computeCommandList->storeCommand([&](ID3D12GraphicsCommandList* commandList) {
			vertexBuffer->copyFrom(commandList, &v_uploader);
			indexBuffer->copyFrom(commandList, &i_uploader);

			resourceBarrier(commandList, {
				vertexBuffer->resourceBarrierTransition(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
				indexBuffer->resourceBarrierTransition(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
			});
		});

		deviceObject->queueObject()->execute(computeCommandList.get());

		// wait for copying
		std::shared_ptr<FenceObject> fence = deviceObject->queueObject()->fence(deviceObject->device());
		fence->wait();
	}
	int width() const { return _width; }
	int height() const { return _height; }

	void step()
	{
		DeviceObject* deviceObject = _deviceObject;

		heap->clear();
		_timestamp->clear();

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
			_timestamp->stampBeg(commandList, "bvh element");
			compute_bvh_element->setPipelineState(commandList);
			compute_bvh_element->setComputeRootSignature(commandList);
			heap->startNextHeapAndAssign(commandList, compute_bvh_element->descriptorEnties());
			heap->u(deviceObject->device(), 0, vertexBuffer->resource(), vertexBuffer->UAVDescription());
			heap->u(deviceObject->device(), 1, indexBuffer->resource(), indexBuffer->UAVDescription());
			heap->u(deviceObject->device(), 2, bvhElementBuffer->resource(), bvhElementBuffer->UAVDescription());
			compute_bvh_element->dispatch(commandList, dispatchsize(_polygon->primitiveCount, 64), 1, 1);
			_timestamp->stampEnd(commandList);

			bvhBuildTaskBuffers[0]->clearCounterValue( commandList );
			bvhBuildTaskBuffers[1]->clearCounterValue( commandList );

			_timestamp->stampBeg(commandList, "bvh build");
			compute_bvh_build->setPipelineState(commandList);
			compute_bvh_build->setComputeRootSignature(commandList);
			heap->startNextHeapAndAssign(commandList, compute_bvh_build->descriptorEnties());
			heap->u(deviceObject->device(), 0, bvhBuildTaskBuffers[0]->resource(), bvhBuildTaskBuffers[0]->UAVDescription());
			heap->u(deviceObject->device(), 1, bvhBuildTaskBuffers[0]->counterResource(), bvhBuildTaskBuffers[0]->CounterUAVDescription());
			heap->u(deviceObject->device(), 2, bvhBuildTaskBuffers[1]->resource(), bvhBuildTaskBuffers[1]->UAVDescription(), bvhBuildTaskBuffers[1]->counterResource());
			heap->u(deviceObject->device(), 3, bvhElementBuffer->resource(), bvhElementBuffer->UAVDescription());
			compute_bvh_build->dispatch(commandList, 1, 1, 1);
			_timestamp->stampEnd(commandList);
			// Execute
			//compute->setPipelineState( commandList );
			//compute->setComputeRootSignature( commandList );
			//heap->startNextHeapAndAssign( commandList, compute->descriptorEnties() );
			//heap->u( deviceObject->device(), 0, colorRGBX8Buffer->resource(), colorRGBX8Buffer->UAVDescription() );
			//heap->u( deviceObject->device(), 1, vertexBuffer->resource(), vertexBuffer->UAVDescription() );
			//heap->u( deviceObject->device(), 2, indexBuffer->resource(), indexBuffer->UAVDescription() );
			//
			//heap->b( deviceObject->device(), 0, _argument->resource() );
			//compute->dispatch( commandList, dispatchsize( _width * _height, 64 ), 1, 1 );

			

			resourceBarrier( commandList, {colorRGBX8Buffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE )} );
			colorRGBX8Buffer->copyTo( commandList, downloader.get() );
			resourceBarrier( commandList, {colorRGBX8Buffer->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON )} );
		
			_timestamp->resolve( commandList );
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
		timestampSpans = _timestamp->download( deviceObject->queueObject()->queue() );

		deviceObject->present();

		auto eleme = bvhElementBuffer->synchronizedDownload<BvhElement>(deviceObject->device(), deviceObject->queueObject());
		auto task = bvhBuildTaskBuffers[1]->synchronizedDownload<BuildTask>(deviceObject->device(), deviceObject->queueObject());
		printf("");
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

private:
	int _width = 0, _height = 0;
	glm::mat4 _inverseVP;
	DeviceObject* _deviceObject;

	std::unique_ptr<CommandObject> computeCommandList;
	std::unique_ptr<StackDescriptorHeapObject> heap;
	std::unique_ptr<ComputeObject> compute;
	std::unique_ptr<ComputeObject> compute_bvh_element;
	std::unique_ptr<ComputeObject> compute_bvh_build;

	std::unique_ptr<BufferObjectUAV> vertexBuffer;
	std::unique_ptr<BufferObjectUAV> indexBuffer;
	std::unique_ptr<BufferObjectUAV> bvhElementBuffer;
	std::unique_ptr<BufferObjectUAV> bvhBuildTaskBuffers[2];

	std::unique_ptr<BufferObjectUAV> accumulationBuffer;
	std::unique_ptr<BufferObjectUAV> colorRGBX8Buffer;
	std::unique_ptr<DownloaderObject> downloader;
	std::unique_ptr<pr::ITexture> texture;

	std::unique_ptr<ConstantBufferObject> _argument;

	std::unique_ptr<TimestampObject> _timestamp;
	std::vector<TimestampSpan> timestampSpans;

	const lwh::Polygon* _polygon;
};

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
			EndCamera();
		}

		PopGraphicState();
		EndCamera();

		BeginImGui();

		ImGui::SetNextWindowSize( {500, 800}, ImGuiCond_Once );
		ImGui::Begin( "Panel" );
		ImGui::Text( "fps = %f", GetFrameRate() );
		ImGui::Checkbox("showWire", &showWire);

		rt->OnImGUI();

		ImGui::End();

		EndImGui();
	}

	pr::CleanUp();
}
