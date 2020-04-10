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

project "Gaussian"
    kind "ConsoleApp"
    language "C++"
    targetdir "bin/"
    systemversion "latest"
    flags { "MultiProcessorCompile", "NoPCH" }

    -- Src
    files { "main_gaussian.cpp", "EzDx.hpp" }

    links { "dxgi" }
    links { "d3d12" }
    -- links { "d3dcompiler" }

    -- Helper
    files { "libs/dxhelper/*.h" }
    includedirs { "libs/dxhelper/" }

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

    links { "dxgi" }
    links { "d3d12" }
    -- links { "d3dcompiler" }

    -- Helper
    files { "libs/dxhelper/*.h" }
    includedirs { "libs/dxhelper/" }

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