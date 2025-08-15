cl -I%VULKAN_SDK%\Include -I. -I.. compile_at_runtime/main.c -Fecompile_at_runtime/main.exe -MD -link -libpath:%VULKAN_SDK%\Lib -libpath:. SDL3.lib
cl -I%VULKAN_SDK%\Include -I. -I.. precompiled/build.c -Feprecompiled/build.exe -MD -link -libpath:%VULKAN_SDK%\Lib
cl -I. -I.. precompiled/main.c -Feprecompiled/main.exe -MD -link -libpath:. SDL3.lib