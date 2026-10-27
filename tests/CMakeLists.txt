function(dsalyrik_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE dsalyrik_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

dsalyrik_test(tst_lrcparser lyrics/tst_lrcparser.cpp)
dsalyrik_test(tst_wordtiming lyrics/tst_wordtiming.cpp)
dsalyrik_test(tst_lrclib lyrics/tst_lrclib.cpp lyrics/FakeHttpServer.h)
target_include_directories(tst_lrclib PRIVATE lyrics)

# MPRIS tests run on a private session bus so real players never interfere.
add_executable(tst_mpris playback/tst_mpris.cpp playback/FakeMprisPlayer.h)
target_include_directories(tst_mpris PRIVATE playback)
target_link_libraries(tst_mpris PRIVATE dsalyrik_core Qt6::Test)
add_test(NAME tst_mpris COMMAND dbus-run-session --config-file=${CMAKE_CURRENT_SOURCE_DIR}/dbus-test-session.conf -- $<TARGET_FILE:tst_mpris>)
dsalyrik_test(tst_sync sync/tst_sync.cpp)

# Render tests use the offscreen platform and software renderer (the lens fallback path).
dsalyrik_test(tst_render render/tst_render.cpp)
set_tests_properties(tst_render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

dsalyrik_test(tst_settings settings/tst_settings.cpp lyrics/FakeHttpServer.h)
set_tests_properties(tst_settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Portal tests use a private session bus with a fake org.freedesktop.portal.Desktop.
add_executable(tst_inhibitor platform/tst_inhibitor.cpp platform/FakePortal.h)
target_include_directories(tst_inhibitor PRIVATE platform)
target_link_libraries(tst_inhibitor PRIVATE dsalyrik_core Qt6::Test)
add_test(NAME tst_inhibitor COMMAND dbus-run-session --config-file=${CMAKE_CURRENT_SOURCE_DIR}/dbus-test-session.conf -- $<TARGET_FILE:tst_inhibitor>)

dsalyrik_test(tst_localprovider lyrics/tst_localprovider.cpp)

add_executable(tst_fullscreen platform/tst_fullscreen.cpp)
target_link_libraries(tst_fullscreen PRIVATE dsalyrik_core Qt6::Test)
add_test(NAME tst_fullscreen COMMAND dbus-run-session --config-file=${CMAKE_CURRENT_SOURCE_DIR}/dbus-test-session.conf -- $<TARGET_FILE:tst_fullscreen>)

dsalyrik_test(tst_settingsdialog ui/tst_settingsdialog.cpp)
set_tests_properties(tst_settingsdialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

dsalyrik_test(tst_spicylyrics lyrics/tst_spicylyrics.cpp lyrics/FakeHttpServer.h)
target_include_directories(tst_spicylyrics PRIVATE lyrics)
