cl -I%VULKAN_SDK%\Include -I. -I.. -I../examples build.c -Febuild.exe -MD -link -libpath:%VULKAN_SDK%\Lib
build.exe
cl -I%VULKAN_SDK%\Include -I. -I.. -I../examples test.c -Fetest.exe -MD -link -libpath:%VULKAN_SDK%\Lib