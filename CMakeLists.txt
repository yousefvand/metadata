cmake_minimum_required(VERSION 3.21)

project(metadata VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(metadata
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/MetadataBackend.cpp
    src/MetadataBackend.h
    src/TagDialog.cpp
    src/TagDialog.h
)

target_link_libraries(metadata PRIVATE Qt6::Widgets)

target_compile_definitions(metadata PRIVATE
    METADATA_VERSION="${PROJECT_VERSION}"
    METADATA_REPOSITORY_URL="https://github.com/yousefvand/metadata"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(metadata PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

install(TARGETS metadata
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES resources/io.github.yousefvand.metadata.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES resources/io.github.yousefvand.metadata.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/metadata
)

option(INSTALL_DOLPHIN_SERVICE_MENU
       "Install a system-wide Dolphin context-menu action"
       OFF)

if(INSTALL_DOLPHIN_SERVICE_MENU)
    install(FILES integration/dolphin/metadata-show.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )
endif()
