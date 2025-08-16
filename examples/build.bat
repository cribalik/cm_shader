cl -I%VULKAN_SDK%\Include -I. -I.. compile_at_runtime/main.c -Fecompile_at_runtime/main.exe -MD -link -libpath:%VULKAN_SDK%\Lib -libpath:. SDL3.lib
cl -I%VULKAN_SDK%\Include -I. -I.. precompiled_to_c/build.c -Feprecompiled_to_c/build.exe -MD -link -libpath:%VULKAN_SDK%\Lib
cl -I. -I.. precompiled_to_c/main.c -Feprecompiled_to_c/main.exe -MD -link -libpath:. SDL3.lib
cl -I%VULKAN_SDK%\Include -I. -I.. precompiled_to_binary/build.c -Feprecompiled_to_binary/build.exe -MD -link -libpath:%VULKAN_SDK%\Lib
cl -I. -I.. precompiled_to_binary/main.c -Feprecompiled_to_binary/main.exe -MD -link -libpath:. SDL3.lib