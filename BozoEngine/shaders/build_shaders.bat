forfiles /s /m *.glsl /c "cmd /c %VULKAN_SDK%/Bin/glslangValidator.exe @path -V -o @file.spv"