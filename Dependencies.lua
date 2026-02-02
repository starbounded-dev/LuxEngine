-- Core Dependencies

VULKAN_SDK = os.getenv("VULKAN_SDK")

IncludeDir = {}
IncludeDir["stb_image"] = "%{wks.location}/Core/vendor/stb_image"
IncludeDir["yaml_cpp"] = "%{wks.location}/Core/vendor/yaml-cpp/include"
IncludeDir["Box2D"] = "%{wks.location}/Core/vendor/Box2D/include"
IncludeDir["GLFW"] = "%{wks.location}/Core/vendor/GLFW/include"
IncludeDir["GLAD"] = "%{wks.location}/Core/vendor/GLAD/include"
IncludeDir["ImGui"] = "%{wks.location}/Core/vendor/imgui"
IncludeDir["ImGuizmo"] = "%{wks.location}/Core/vendor/imguizmo"
IncludeDir["glm"] = "%{wks.location}/Core/vendor/glm"
IncludeDir["filewatch"] = "%{wks.location}/Core/vendor/filewatch"
IncludeDir["entt"] = "%{wks.location}/Core/vendor/entt/include"
IncludeDir["mono"] = "%{wks.location}/Core/vendor/mono/include"
IncludeDir["shaderc"] = "%{wks.location}/Core/vendor/shaderc/include"
IncludeDir["SPIRV_Cross"] = "%{wks.location}/Core/vendor/SPIRV-Cross"
IncludeDir["VulkanSDK"] = "%{VULKAN_SDK}/Include"
IncludeDir["msdfgen"] = "%{wks.location}/Core/vendor/msdf-atlas-gen/msdfgen"
IncludeDir["msdf_atlas_gen"] = "%{wks.location}/Core/vendor/msdf-atlas-gen/msdf-atlas-gen"
IncludeDir["miniaudio"] = "%{wks.location}/Core/vendor/miniaudio/include"
IncludeDir["Tracy"] = "%{wks.location}/Core/vendor/tracy/tracy/public"
IncludeDir["NVRHI"] = "%{wks.location}/Core/vendor/nvrhi/include"

LibraryDir = {}

LibraryDir["VulkanSDK"] = "%{VULKAN_SDK}/Lib"
LibraryDir["Mono"] = "%{wks.location}/Core/vendor/mono/lib/%{cfg.buildcfg}"
LibraryDir["Tracy"] = "%{wks.location}/Core/vendor/tracy/bin/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/Tracy"

Library = {}
Library["mono"] = "%{LibraryDir.Mono}/libmono-static-sgen.lib"
Library["Tracy"] = "%{LibraryDir.Tracy}/Tracy.lib"
Library["Vulkan"] = "%{LibraryDir.VulkanSDK}/vulkan-1.lib"
Library["VulkanUtils"] = "%{LibraryDir.VulkanSDK}/VkLayer_utils.lib"

Library["ShaderC_Debug"] = "%{LibraryDir.VulkanSDK}/shaderc_sharedd.lib"
Library["SPIRV_Cross_Debug"] = "%{LibraryDir.VulkanSDK}/spirv-cross-cored.lib"
Library["SPIRV_Cross_GLSL_Debug"] = "%{LibraryDir.VulkanSDK}/spirv-cross-glsld.lib"
Library["SPIRV_Tools_Debug"] = "%{LibraryDir.VulkanSDK}/SPIRV-Toolsd.lib"

Library["ShaderC_Release"] = "%{LibraryDir.VulkanSDK}/shaderc_shared.lib"
Library["SPIRV_Cross_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-core.lib"
Library["SPIRV_Cross_GLSL_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-glsl.lib"

-- DXC (DirectX Shader Compiler) for runtime HLSL compilation
Library["DXC_Debug"] = "%{LibraryDir.VulkanSDK}/dxcompilerd.lib"
Library["DXC_Release"] = "%{LibraryDir.VulkanSDK}/dxcompiler.lib"


-- Windows
Library["WinSock"] = "Ws2_32.lib"
Library["WinMM"] = "Winmm.lib"
Library["WinVersion"] = "Version.lib"
Library["BCrypt"] = "Bcrypt.lib"
