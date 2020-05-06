include "libs/PrLib"

workspace "DirectX12_Example"
    location "build"
    configurations { "Debug", "Release" }
    startproject "main"

architecture "x86_64"

externalproject "prlib"
	location "libs/PrLib/build" 
    kind "StaticLib"
    language "C++"

function dx()
    links { "dxgi" }
    links { "d3d12" }
    -- links { "d3dcompiler" }
end

function prlib()
    -- prlib
    -- setup command
    -- git submodule add https://github.com/Ushio/prlib libs/prlib
    -- premake5 vs2017
    dependson { "prlib" }
    includedirs { "libs/prlib/src" }
    libdirs { "libs/prlib/bin" }
    filter {"Debug"}
        links { "prlib_d" }
    filter {"Release"}
        links { "prlib" }
    filter{}
end

-- WinPixEventRuntime
-- https://devblogs.microsoft.com/pix/winpixeventruntime/
function pix( enabled )
    if enabled then
        defines { "USE_PIX" }
    end

    pixdir = "libs/winpixeventruntime.1.0.200127001"
    includedirs { "%{pixdir}/Include" }
    libdirs { "%{pixdir}/bin/x64/" }
    links { "WinPixEventRuntime" }
    postbuildcommands { "{COPY} %{prj.location}../%{pixdir}/bin/x64/WinPixEventRuntime.dll %{cfg.targetdir}" }
end

project "Gaussian"
    kind "ConsoleApp"
    language "C++"
    targetdir "bin/"
    systemversion "latest"
    flags { "MultiProcessorCompile", "NoPCH" }

    -- Src
    files { "main_gaussian.cpp", "EzDx.hpp" }

    -- directx
    dx()

    -- Helper
    files { "libs/dxhelper/*.h" }
    includedirs { "libs/dxhelper/" }

    -- prlib
    prlib()

    -- pix
    pix( true )

    symbols "On"

    filter {"Debug"}
        runtime "Debug"
        targetname ("Gaussian_Debug")
        optimize "Off"
    filter {"Release"}
        runtime "Release"
        targetname ("Gaussian")
        optimize "Full"
    filter{}

project "Simple"
    kind "ConsoleApp"
    language "C++"
    targetdir "bin/"
    systemversion "latest"
    flags { "MultiProcessorCompile", "NoPCH" }

    -- Src
    files { "main_simple.cpp", "EzDx.hpp" }

    -- directx
    dx()

    -- Helper
    files { "libs/dxhelper/*.h" }
    includedirs { "libs/dxhelper/" }

    -- prlib
    prlib()

    -- pix
    pix( true )

    symbols "On"

    filter {"Debug"}
        runtime "Debug"
        targetname ("Simple_Debug")
        optimize "Off"
    filter {"Release"}
        runtime "Release"
        targetname ("Simple")
        optimize "Full"
    filter{}

project "RadixSort"
    kind "ConsoleApp"
    language "C++"
    targetdir "bin/"
    systemversion "latest"
    flags { "MultiProcessorCompile", "NoPCH" }

    -- Src
    files { "main_radixsort.cpp", "EzDx.hpp" }

    -- directx
    dx()

    -- Helper
    files { "libs/dxhelper/*.h" }
    includedirs { "libs/dxhelper/" }

    -- prlib
    prlib()

    -- pix
    pix( true )
    
    symbols "On"

    filter {"Debug"}
        runtime "Debug"
        targetname ("RadixSort_Debug")
        optimize "Off"
    filter {"Release"}
        runtime "Release"
        targetname ("RadixSort")
        optimize "Full"
    filter{}

project "LinearRayCaster"
    kind "ConsoleApp"
    language "C++"
    targetdir "bin/"
    systemversion "latest"
    flags { "MultiProcessorCompile", "NoPCH" }

    -- Src
    files { "main_rt.cpp", "EzDx.hpp", "lwHoudiniLoader.hpp" }

    -- directx
    dx()

    -- Helper
    files { "libs/dxhelper/*.h" }
    includedirs { "libs/dxhelper/" }

    -- rapidjson
    includedirs { "libs/rapidjson/include" }
    files { "libs/rapidjson/include/**.h" }

    -- prlib
    prlib()

    -- pix
    pix( true )
    
    symbols "On"

    filter {"Debug"}
        runtime "Debug"
        targetname ("LinearRayCaster_Debug")
        optimize "Off"
    filter {"Release"}
        runtime "Release"
        targetname ("LinearRayCaster")
        optimize "Full"
    filter{}

project "BvhRayCaster"
    kind "ConsoleApp"
    language "C++"
    targetdir "bin/"
    systemversion "latest"
    flags { "MultiProcessorCompile", "NoPCH" }

    -- Src
    files { "main_rt_bvh.cpp", "EzDx.hpp", "lwHoudiniLoader.hpp" }

    -- directx
    dx()

    -- Helper
    files { "libs/dxhelper/*.h" }
    includedirs { "libs/dxhelper/" }

    -- rapidjson
    includedirs { "libs/rapidjson/include" }
    files { "libs/rapidjson/include/**.h" }

    -- prlib
    prlib()

    -- pix
    pix( true )
    
    symbols "On"

    filter {"Debug"}
        runtime "Debug"
        targetname ("BvhRayCaster_Debug")
        optimize "Off"
    filter {"Release"}
        runtime "Release"
        targetname ("BvhRayCaster")
        optimize "Full"
    filter{}