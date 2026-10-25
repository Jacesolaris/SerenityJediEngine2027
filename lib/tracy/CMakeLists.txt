# Tracy profiler client v0.14.1 (https://github.com/wolfpld/tracy, BSD 3-clause, see LICENSE.txt).
# Only the client ("public" folder) is here. The viewer (tracy-profiler.exe) must be the SAME version.
#
# Built once as a DLL (TracyClient.dll) so the exe, renderers and game modules all report to one profiler.
# Enabled by the root option UseTracy (OFF by default): normal builds do not build or need this.

add_library(TracyClient SHARED "${CMAKE_CURRENT_SOURCE_DIR}/public/TracyClient.cpp")
target_include_directories(TracyClient PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public")
# TRACY_ON_DEMAND: collect data only while the viewer is connected, so a Tracy build runs at normal speed otherwise.
target_compile_definitions(TracyClient
	PUBLIC TRACY_ENABLE TRACY_ON_DEMAND
	PRIVATE TRACY_EXPORTS)
set_target_properties(TracyClient PROPERTIES FOLDER "Libs")
if(MSVC)
	target_compile_definitions(TracyClient PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
	target_link_libraries(TracyClient PRIVATE ws2_32 dbghelp)
	# third-party: keep it out of 'Run Code Analysis'
	set_target_properties(TracyClient PROPERTIES VS_GLOBAL_EnableMicrosoftCodeAnalysis false VS_GLOBAL_MSBuildWarningsAsMessages MSB8061)
endif()
