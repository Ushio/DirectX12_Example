require 'shellwords'

# USAGE: dxc.exe [options] <inputs>
#   -nologo            Suppress copyright message
#   -Zi                     Enable debug information
#   -Od                     Disable optimizations
#   -T <profile>            Set target profile.
#   <profile>: ps_6_0, ps_6_1, ps_6_2, ps_6_3, ps_6_4,
#            vs_6_0, vs_6_1, vs_6_2, vs_6_3, vs_6_4,
#            cs_6_0, cs_6_1, cs_6_2, cs_6_3, cs_6_4,
#            gs_6_0, gs_6_1, gs_6_2, gs_6_3, gs_6_4,
#            ds_6_0, ds_6_1, ds_6_2, ds_6_3, ds_6_4,
#            hs_6_0, hs_6_1, hs_6_2, hs_6_3, hs_6_4,
#            lib_6_3, lib_6_4
#    -Fo <file>              Output object file
# system("dxc -help")

DebugMode = true

Dir.glob("kernels/*.hlsl") do |hlsl|
    output = "bin/#{File.basename(hlsl, '.*')}.cso"
    
    if DebugMode then 
        command = "dxc.exe /T cs_6_3 /nologo -I kernels/ /Od /Zi /Fo #{output} #{hlsl}"
    else
        command = "dxc.exe /T cs_6_3 /nologo -I kernels/ /Fo #{output} #{hlsl}"
    end
    puts command
    system command
end
