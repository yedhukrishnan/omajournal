cmake_minimum_required(VERSION 3.21)
project(omajournal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 DBus Concurrent Test)
# Every entry and the key file are encrypted with libsodium.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

# Everything but main.cpp, shared by the app and the tests, so each source
# compiles once and a new file in src/ needs no line here.
file(GLOB omajournal_sources CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(REMOVE_ITEM omajournal_sources ${CMAKE_SOURCE_DIR}/src/main.cpp)
add_library(omajournal_core STATIC ${omajournal_sources})
target_include_directories(omajournal_core PUBLIC src)
target_link_libraries(omajournal_core PUBLIC
  Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus Qt6::Concurrent
  PkgConfig::SODIUM)
target_compile_options(omajournal_core PUBLIC -Wall -Wextra)

add_executable(omajournal src/main.cpp src/resources.qrc)
target_link_libraries(omajournal PRIVATE omajournal_core)

# The tests load the real Main.qml, so they carry the resources too.
enable_testing()
add_executable(omajournal_tests tests/omajournal_tests.cpp src/resources.qrc)
target_link_libraries(omajournal_tests PRIVATE omajournal_core Qt6::Test)
# The command line tests run the real binary.
add_dependencies(omajournal_tests omajournal)
target_compile_definitions(omajournal_tests PRIVATE OMAJOURNAL_BINARY="$<TARGET_FILE:omajournal>")
add_test(NAME omajournal_tests COMMAND omajournal_tests)
set_tests_properties(omajournal_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

# `cmake --install build --prefix ~/.local` puts the app in the launcher
# before it is packaged; a package installs the same files under /usr.
include(GNUInstallDirs)
install(TARGETS omajournal RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES share/omajournal.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES share/omajournal.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/omajournal)
