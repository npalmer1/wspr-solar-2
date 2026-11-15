cmake_minimum_required(VERSION 3.19)
project(WsprSolar VERSION 0.2.1 LANGUAGES CXX)   # WSPR Solar 2 (the C# WSPR Solar was 0.1.27)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network)

# Non-window part (NOAA files, levels, radio bursts, the wspr_sol database, ~/DBcredential) - shared
# by the program and the tests
add_library(solarcore STATIC
    solarcore.h solarcore.cpp
    solarstore.h solarstore.cpp
    credentials.h credentials.cpp
)
target_link_libraries(solarcore PUBLIC Qt6::Core Qt6::Sql Qt6::Network)
target_include_directories(solarcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(WSPRSOLAR_SOURCES mainwindow.h mainwindow.cpp mainwindow.ui msg.h msg.cpp loaderror.h loaderror.cpp
    rtfhtml.h rtfhtml.cpp appfont.h wsprsolar.qrc)

if(WIN32)   # icon and version details for WsprSolar.exe (Explorer, taskbar, installer)
    set(RC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/wsprsolar.ico")
    set(RC_NAME "WSPR Solar 2")
    set(RC_EXE WsprSolar)
    configure_file(wsprsolar.rc.in "${CMAKE_CURRENT_BINARY_DIR}/wsprsolar.rc" @ONLY)
    set(WSPRSOLAR_RC "${CMAKE_CURRENT_BINARY_DIR}/wsprsolar.rc")
    # rebuild the .rc when the icon changes (CMake doesn't track the files an .rc points to)
    set_property(SOURCE "${WSPRSOLAR_RC}" APPEND PROPERTY OBJECT_DEPENDS "${RC_ICON}")
endif()

add_executable(WsprSolar WIN32 main.cpp ${WSPRSOLAR_SOURCES} ${WSPRSOLAR_RC})
target_link_libraries(WsprSolar PRIVATE solarcore Qt6::Widgets)
target_compile_definitions(WsprSolar PRIVATE WSPRSOLAR_VERSION="${PROJECT_VERSION}")

# Windows: copy the Qt DLLs and plugins next to the program after each build (windeployqt).
# NOAA's files come over https: windeployqt adds Qt's TLS plugin (tls/qschannelbackend.dll) for that.
# The MySQL driver (sqldrivers/qsqlmysql.dll) and libmysql.dll are not part of Qt - they're copied from
# installer/thirdparty.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET WsprSolar POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --verbose 0 "$<TARGET_FILE:WsprSolar>"
            COMMENT "windeployqt WsprSolar")
    else()
        message(WARNING "windeployqt not found - run the program from Qt Creator or add the Qt bin folder to PATH")
    endif()
    set(_mysql_dir "${CMAKE_CURRENT_SOURCE_DIR}/installer/thirdparty")
    if(EXISTS "${_mysql_dir}/sqldrivers/qsqlmysql.dll" AND EXISTS "${_mysql_dir}/libmysql.dll")
        file(GLOB _mysql_dlls "${_mysql_dir}/*.dll")
        add_custom_command(TARGET WsprSolar POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:WsprSolar>/sqldrivers"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_mysql_dir}/sqldrivers/qsqlmysql.dll" "$<TARGET_FILE_DIR:WsprSolar>/sqldrivers/"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_mysql_dlls} "$<TARGET_FILE_DIR:WsprSolar>/"
            COMMENT "MySQL driver for WsprSolar")
    else()
        message(WARNING "installer/thirdparty/sqldrivers/qsqlmysql.dll not found - copy the MySQL driver "
                        "(sqldrivers/qsqlmysql.dll, libmysql.dll ...) next to the program yourself")
    endif()
endif()

# Linux: "cmake --install" puts the program in bin, a menu entry and icon in share
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS WsprSolar RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES linux/wspr-solar-2.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES linux/wspr-solar-2.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    file(GLOB _help "${CMAKE_CURRENT_SOURCE_DIR}/About_WS_Solar*.rtf")   # the F1 help file
    if(_help)
        install(FILES ${_help} DESTINATION ${CMAKE_INSTALL_DATADIR}/wspr-solar-2)
    endif()
endif()

option(WSPRSOLAR_TESTS "Build the tests (the database one needs a scratch MySQL, never the real one)" OFF)
if(WSPRSOLAR_TESTS)
    add_executable(solar_test tests/solar_test.cpp)   # NOAA files in tests/data; scratch MySQL
    target_link_libraries(solar_test PRIVATE solarcore)
    target_compile_definitions(solar_test PRIVATE SOLAR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_executable(solarwindow_uitest tests/solarwindow_uitest.cpp ${WSPRSOLAR_SOURCES})   # offscreen; fake NOAA
    set_target_properties(solarwindow_uitest PROPERTIES AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(solarwindow_uitest PRIVATE solarcore Qt6::Widgets)
    target_compile_definitions(solarwindow_uitest PRIVATE WSPRSOLAR_VERSION="${PROJECT_VERSION}"
        SOLAR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
endif()
