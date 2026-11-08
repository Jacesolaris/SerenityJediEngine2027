# SerenityJediEngine2027 launcher (Windows only): SINGLEPLAYER / MULTIPLAYER / SETTINGS / EXIT window that
# starts SerenityJediEngine2027-SP.x86_64.exe or SerenityJediEngine2027-MP.x86_64.exe from its own folder, with
# SETTINGS, UPDATES (ModDB, through winhttp) and the INSTALLATION CHECK. texts/AllowedFiles.txt is the release's
# SerenityJediEngine2027 mod folder: a Release build of the launcher only starts the game when the folder holds exactly
# those files (Debug builds show the check but start anyway).
# art/launcher_bg.png is built by MD-work/sje-launcher/make_art.ps1 from the SP main menu background
# (SerenityJediEngine2027-Assets-0.pk3: gfx/menus/main_opening.JPG); the icon by make_icon.ps1 from the
# Jedi emblem icon (win32/SerenityJediEngine2027-glsl-SP.ico) in green.
# The sounds are the SJE main menu's own (SerenityJediEngine2027-Assets-1.pk3: sound/interface/menuroam.mp3,
# decoded to sound/menuroam.wav by mp3towav.ps1, and music/MainMenuTheme.mp3).

set(LauncherName "SerenityJediEngine2027-Launcher")

set(LauncherFiles
	"${SPDir}/launcher/sje_launcher.cpp"
	"${SPDir}/launcher/sje_launcher.rc"
	"${SPDir}/launcher/resource.h"
	)
source_group("launcher" FILES ${LauncherFiles})

add_executable(${LauncherName} WIN32 ${LauncherFiles})
target_compile_definitions(${LauncherName} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(${LauncherName} PRIVATE "${SPDir}")
target_link_libraries(${LauncherName} gdiplus shlwapi winmm shell32 winhttp)
set_target_properties(${LauncherName} PROPERTIES PROJECT_LABEL "Launcher")

install(TARGETS ${LauncherName}
	RUNTIME
	DESTINATION ${JKAInstallDir}
	COMPONENT ${JKASPClientComponent})
