cmake_minimum_required(VERSION 3.16)

# Keep the CMake project and both clients on the same version.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/jj_rest.h" JJ_VERSION_LINE
    REGEX "^#define JJ_VERSION ")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" JJ_PROJECT_VERSION "${JJ_VERSION_LINE}")
if(NOT JJ_PROJECT_VERSION)
    message(FATAL_ERROR "Cannot read JJ_VERSION from jj_rest.h")
endif()
project(JustJournal VERSION ${JJ_PROJECT_VERSION} LANGUAGES C)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/jj_rest.h")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_GTK "Build the GTK 3 client" OFF)
option(BUILD_TESTING "Enable REST integration tests" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(REST REQUIRED IMPORTED_TARGET libcurl jansson)

add_library(jj_rest STATIC jj_rest.c)
target_link_libraries(jj_rest PUBLIC PkgConfig::REST)
target_compile_options(jj_rest PRIVATE -Wall -pedantic)

add_executable(jjclient jj.c)
target_link_libraries(jjclient PRIVATE jj_rest)
target_compile_options(jjclient PRIVATE -Wall -pedantic)

include(GNUInstallDirs)
install(TARGETS jjclient
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT cli
    PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
install(FILES jjclient.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 COMPONENT man
    PERMISSIONS OWNER_READ GROUP_READ WORLD_READ)

if(BUILD_GTK)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    add_executable(gtkjj gtk/gtkjj.c)
    target_link_libraries(gtkjj PRIVATE jj_rest PkgConfig::GTK3)
    target_compile_options(gtkjj PRIVATE -Wall -pedantic)
    target_compile_definitions(gtkjj PRIVATE _DEFAULT_SOURCE)
    install(TARGETS gtkjj RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT gtk)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(test_credentials tests/test_credentials.c)
    target_link_libraries(test_credentials PRIVATE PkgConfig::REST)
    target_compile_options(test_credentials PRIVATE -Wall -pedantic)
    add_test(NAME credential_buffers COMMAND test_credentials)
    add_test(NAME rest_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rest.py)
    set_tests_properties(rest_integration PROPERTIES ENVIRONMENT "CC=${CMAKE_C_COMPILER}")
endif()
