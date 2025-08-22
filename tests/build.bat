shad.exe sdl3 kitchensink.shader > kitchensink.h
cl -I%VULKAN_SDK%\Include -I. -I.. -I../examples test.c -Fetest.exe -MD -link -libpath:%VULKAN_SDK%\Lib