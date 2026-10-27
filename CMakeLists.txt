cmake_minimum_required(VERSION 3.25)
project(dsalyrik VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core DBus Widgets Quick Network)
if(DSALYRIK_BUILD_TESTS OR NOT DEFINED DSALYRIK_BUILD_TESTS)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
endif()

# Wayland idle notifications (screensaver mode), generated from the bundled protocol XML.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(_idle_xml "${CMAKE_CURRENT_SOURCE_DIR}/resources/protocols/ext-idle-notify-v1.xml")
set(_idle_header "${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h")
set(_idle_code "${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-protocol.c")
add_custom_command(OUTPUT "${_idle_header}" COMMAND "${WAYLAND_SCANNER}" client-header "${_idle_xml}" "${_idle_header}" DEPENDS "${_idle_xml}" VERBATIM)
add_custom_command(OUTPUT "${_idle_code}" COMMAND "${WAYLAND_SCANNER}" private-code "${_idle_xml}" "${_idle_code}" DEPENDS "${_idle_xml}" VERBATIM)

add_library(dsalyrik_core STATIC
    "${_idle_header}"
    "${_idle_code}"
    src/platform/IdleMonitor.cpp
    src/platform/FullscreenWatcher.cpp
    src/app/AppController.cpp
    src/lyrics/FallbackLyricProvider.cpp
    src/lyrics/LocalLyricProvider.cpp
    src/lyrics/LrcLibProvider.cpp
    src/lyrics/LyricProvider.h
    src/lyrics/LrcParser.cpp
    src/lyrics/PlainLyricsTimer.cpp
    src/lyrics/SpicyLyricsProvider.cpp
    src/lyrics/TrackNameNormalizer.cpp
    src/lyrics/WordTimingSynthesizer.cpp
    src/playback/MprisManager.cpp
    src/sync/LyricSession.cpp
    src/sync/LyricSyncEngine.cpp
    src/playback/MprisPlayer.cpp
    src/platform/DisplaySleepInhibitor.cpp
    src/platform/OverlayWindow.cpp
    src/render/PaletteExtractor.cpp
    src/render/WordGlyphResolver.cpp
    src/settings/AppSettings.cpp
    src/ui/SettingsDialog.cpp
    src/ui/TrayController.cpp
)
target_include_directories(dsalyrik_core PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Optional: native Wayland overlay surfaces (KDE LayerShellQt). Without it, Wayland
# sessions fall back to an XWayland window.
find_package(LayerShellQt CONFIG QUIET)
if(LayerShellQt_FOUND)
    target_compile_definitions(dsalyrik_core PUBLIC DSALYRIK_HAVE_LAYER_SHELL)
    target_link_libraries(dsalyrik_core PUBLIC LayerShellQt::Interface)
endif()
target_compile_definitions(dsalyrik_core PUBLIC DSALYRIK_VERSION="${PROJECT_VERSION}")
target_link_libraries(dsalyrik_core PUBLIC Qt6::Core Qt6::DBus Qt6::Widgets Qt6::Quick Qt6::Network PRIVATE PkgConfig::WAYLAND_CLIENT)

# Lens shader: compiled with qsb directly (Qt ShaderTools' CMake package is optional).
get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
find_program(QSB_EXECUTABLE qsb HINTS "${_qt_bin}" /usr/lib64/qt6/bin /usr/lib/qt6/bin REQUIRED)
set(_shader_outputs)
foreach(_shader lens gradient artglow)
    set(_qsb "${CMAKE_CURRENT_BINARY_DIR}/shaders/${_shader}.frag.qsb")
    add_custom_command(
        OUTPUT "${_qsb}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${QSB_EXECUTABLE}" --glsl "100es,120,150" --hlsl 50 --msl 12 -o "${_qsb}"
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/${_shader}.frag"
        DEPENDS resources/shaders/${_shader}.frag
        VERBATIM)
    set_source_files_properties("${_qsb}" PROPERTIES GENERATED TRUE QT_RESOURCE_ALIAS "shaders/${_shader}.frag.qsb")
    list(APPEND _shader_outputs "${_qsb}")
endforeach()
set_source_files_properties(src/render/LyricOverlay.qml PROPERTIES QT_RESOURCE_ALIAS "LyricOverlay.qml")
set_source_files_properties(resources/icons/dsalyrik.svg PROPERTIES QT_RESOURCE_ALIAS "icons/dsalyrik.svg")
set_source_files_properties(resources/fonts/Nunito-Variable.ttf PROPERTIES QT_RESOURCE_ALIAS "fonts/Nunito-Variable.ttf")
set_source_files_properties(resources/glyphs.txt PROPERTIES QT_RESOURCE_ALIAS "glyphs.txt")
set_source_files_properties(resources/phrases.txt PROPERTIES QT_RESOURCE_ALIAS "phrases.txt")
qt_add_resources(dsalyrik_core "overlay" PREFIX "/dsalyrik"
    FILES src/render/LyricOverlay.qml ${_shader_outputs} resources/icons/dsalyrik.svg resources/fonts/Nunito-Variable.ttf resources/glyphs.txt resources/phrases.txt)

add_executable(dsalyrik src/main.cpp)
target_link_libraries(dsalyrik PRIVATE dsalyrik_core)

include(GNUInstallDirs)
install(TARGETS dsalyrik RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.github.dd1o7.DSAlyrik.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/io.github.dd1o7.DSAlyrik.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES resources/icons/dsalyrik.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME io.github.dd1o7.DSAlyrik.svg)

option(DSALYRIK_BUILD_TESTS "Build the test suite" ON)
if(DSALYRIK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
