cmake_minimum_required(VERSION 3.16)
project(ParquetPlot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/lib")
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/$<CONFIG>/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_BINARY_DIR}/$<CONFIG>/lib)
endforeach()

find_package(Qt6 COMPONENTS Core Gui Widgets PrintSupport Qml REQUIRED)
# Arrow probes optional CMake configs before falling back to pkg-config.
# Keep those probes quiet; REQUIRED still reports genuinely missing dependencies.

include(cmake/CPM.cmake)
include(cmake/GitVersionDetect.cmake)

CPMAddPackage(
    NAME ApacheArrow
    URL https://github.com/apache/arrow/archive/refs/tags/apache-arrow-26.0.1.tar.gz
    URL_HASH SHA256=1fee09a4b032cd814ad8ce27365499a42d4fd78ab64870045453b3dd0343e64a
    SOURCE_SUBDIR "./cpp" # this is relative to the top directoy of the cloned repository
    OPTIONS "ARROW_DEFINE_OPTIONS ON"
            "ARROW_SIMD_LEVEL SSE4_2"
            "ARROW_RUNTIME_SIMD_LEVEL AVX2"
            "ARROW_PARQUET ON"
            "ARROW_MIMALLOC OFF"
            "ARROW_BOOST_USE_SHARED OFF"
            "ARROW_BUILD_SHARED OFF"
            "ARROW_BUILD_INTEGRATION OFF"
            "ARROW_BUILD_TESTS OFF"
            "ARROW_TESTING OFF"
            "ARROW_BUILD_BENCHMARKS OFF"
)

add_library(parquetplot_table STATIC
    src/table/TableBrowser.cpp src/table/TableBrowser.h
    src/table/FileTab.cpp src/table/FileTab.h
    src/table/FilterBuilderWidget.cpp src/table/FilterBuilderWidget.h
    src/table/FilterCondition.h
    src/table/ParquetDocument.cpp src/table/ParquetDocument.h
    src/table/ParquetTableModel.cpp src/table/ParquetTableModel.h
    src/parquet_reader.cpp src/parquet_reader.h)
target_include_directories(parquetplot_table PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${arrow_SOURCE_DIR}/src
    ${arrow_BINARY_DIR}/src)
target_link_libraries(parquetplot_table PUBLIC Qt6::Widgets arrow_static parquet_static)

# QCustomPlot is not consistently packaged by Linux distributions and is not
# normally present on Windows. Download a pinned, checksum-verified source
# release and compile it into ParquetPlot so end users do not need to install it.
option(PARQUETPLOT_USE_SYSTEM_QCUSTOMPLOT
       "Use an installed qcustomplot-qt6 library instead of the pinned source" OFF)

if(PARQUETPLOT_USE_SYSTEM_QCUSTOMPLOT)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(QCUSTOMPLOT QUIET IMPORTED_TARGET qcustomplot-qt6)
    endif()

    if(QCUSTOMPLOT_FOUND)
        set(QCUSTOMPLOT_TARGET PkgConfig::QCUSTOMPLOT)
    else()
        find_path(QCUSTOMPLOT_INCLUDE_DIR qcustomplot.h)
        find_library(QCUSTOMPLOT_LIBRARY NAMES qcustomplot-qt6 qcustomplot)
        if(NOT QCUSTOMPLOT_INCLUDE_DIR OR NOT QCUSTOMPLOT_LIBRARY)
            message(FATAL_ERROR
                "QCustomPlot was not found. Disable PARQUETPLOT_USE_SYSTEM_QCUSTOMPLOT "
                "to use the bundled-at-build source release.")
        endif()

        add_library(qcustomplot_system UNKNOWN IMPORTED)
        set_target_properties(qcustomplot_system PROPERTIES
            IMPORTED_LOCATION "${QCUSTOMPLOT_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${QCUSTOMPLOT_INCLUDE_DIR}")
        set(QCUSTOMPLOT_TARGET qcustomplot_system)
    endif()
else()
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    CPMAddPackage(
        NAME qcustomplot
        URL https://www.qcustomplot.com/release/2.1.1/QCustomPlot.tar.gz
        URL_HASH SHA512=2e49a9b3f7ab03bcd580e5f3c3ae0d5e8c57d3ccce0ceed9862cde7ea23e2f3672a963af988be60e504cb5aa50bc462e4b28acf577eae41cc6fea2802642dc19)

    add_library(qcustomplot_source STATIC
        "${qcustomplot_SOURCE_DIR}/qcustomplot.cpp"
        "${qcustomplot_SOURCE_DIR}/qcustomplot.h")
    target_include_directories(qcustomplot_source PUBLIC "${qcustomplot_SOURCE_DIR}")
    target_link_libraries(qcustomplot_source PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport)
    set(QCUSTOMPLOT_TARGET qcustomplot_source)
endif()

add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    resources/resources.rc)

qt_add_resources(${PROJECT_NAME} "resources" FILES "resources/io.github.selimct.ParquetPlot.svg")

target_compile_definitions(${PROJECT_NAME} PRIVATE VERSION_STRING="${GITVERSIONDETECT_VERSION}")

if(WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND Qt6::windeployqt
                --no-compiler-runtime
                --no-translations
                --no-system-d3d-compiler
                --no-system-dxc-compiler
                --no-opengl-sw
                --no-quick-import
                --skip-plugin-types generic,networkinformation,tls,imageformats,qmltooling
                --dir "${CMAKE_BINARY_DIR}/$<CONFIG>/bin"
                $<TARGET_FILE:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/$<CONFIG>/bin")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    parquetplot_table
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Qml
    arrow_static
    parquet_static
    ${ARROW_TARGETS}
    ${QCUSTOMPLOT_TARGET})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${arrow_SOURCE_DIR}/src
    ${arrow_BINARY_DIR}/src)

option(PARQUETPLOT_BUILD_TESTS "Build plot interaction tests" OFF)
if(PARQUETPLOT_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 COMPONENTS Test REQUIRED)
    add_executable(parquetplot_tests
        tests/mainwindow_test.cpp
        src/mainwindow.cpp src/mainwindow.h)
    target_include_directories(parquetplot_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(parquetplot_tests PRIVATE parquetplot_table Qt6::Test Qt6::Widgets Qt6::PrintSupport arrow_static parquet_static ${QCUSTOMPLOT_TARGET})
    add_test(NAME plot_panels COMMAND parquetplot_tests)
    set(PARQUETPLOT_TEST_PLATFORM "offscreen" CACHE STRING "Qt platform plugin for interaction tests (use xcb with xvfb-run on Linux)")
    set_tests_properties(plot_panels PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=${PARQUETPLOT_TEST_PLATFORM}" TIMEOUT 30)
endif()

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/io.github.selimct.ParquetPlot-mime.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
install(FILES resources/io.github.selimct.ParquetPlot.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/ParquetPlot)
