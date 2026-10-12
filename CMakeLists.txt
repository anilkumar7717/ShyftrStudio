cmake_minimum_required(VERSION 3.21)

project(ShyftrStudio
    VERSION 0.1.0
    DESCRIPTION "Offline desktop video editor with local AI subtitles"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(SHYFTR_BUILD_APP "Build the desktop application (needs Qt Widgets + Multimedia)" ON)
option(SHYFTR_BUILD_TESTS "Build unit and integration tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Concurrent)
if(SHYFTR_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Multimedia)
endif()
if(SHYFTR_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------- engine library
# Everything that is not UI: project model, persistence, captions, media/export.
add_library(shyftr_engine STATIC
    src/core/Model.h src/core/Model.cpp
    src/core/ProjectSerializer.h src/core/ProjectSerializer.cpp
    src/core/ProjectStore.h src/core/ProjectStore.cpp
    src/core/TimelineOps.h src/core/TimelineOps.cpp
    src/core/Document.h src/core/Document.cpp
    src/core/Settings.h src/core/Settings.cpp
    src/core/Tools.h src/core/Tools.cpp
    src/captions/CaptionStyle.h src/captions/CaptionStyle.cpp
    src/captions/CaptionEngine.h src/captions/CaptionEngine.cpp
    src/captions/OverlayRenderer.h src/captions/OverlayRenderer.cpp
    src/captions/SubtitleIO.h src/captions/SubtitleIO.cpp
    src/captions/Transcription.h src/captions/Transcription.cpp
    src/media/MediaProbe.h src/media/MediaProbe.cpp
    src/media/MediaCache.h src/media/MediaCache.cpp
    src/media/ColorMath.h src/media/ColorMath.cpp
    src/media/ExportGraph.h src/media/ExportGraph.cpp
    src/media/ExportJob.h src/media/ExportJob.cpp
    src/media/CanvasLayout.h src/media/CanvasLayout.cpp
    resources/resources.qrc
)
target_include_directories(shyftr_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(shyftr_engine PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent)

# ---------------------------------------------------------------- application
if(SHYFTR_BUILD_APP)
    set(APP_SOURCES
        src/app/main.cpp
        src/app/MainWindow.h src/app/MainWindow.cpp
        src/app/Theme.h src/app/Theme.cpp
        src/app/WelcomePage.h src/app/WelcomePage.cpp
        src/app/PreviewEngine.h src/app/PreviewEngine.cpp
        src/app/ViewerWidget.h src/app/ViewerWidget.cpp
        src/app/TimelineWidget.h src/app/TimelineWidget.cpp
        src/app/MediaBinWidget.h src/app/MediaBinWidget.cpp
        src/app/InspectorWidget.h src/app/InspectorWidget.cpp
        src/app/SubtitlePanel.h src/app/SubtitlePanel.cpp
        src/app/TemplateGallery.h src/app/TemplateGallery.cpp
        src/app/EffectsBrowser.h src/app/EffectsBrowser.cpp
        src/app/QuickPanel.h src/app/QuickPanel.cpp
        src/app/ExportDialog.h src/app/ExportDialog.cpp
        src/app/PreferencesDialog.h src/app/PreferencesDialog.cpp
        src/app/RecordDialog.h src/app/RecordDialog.cpp
        src/app/RelinkDialog.h src/app/RelinkDialog.cpp
        src/app/Widgets.h src/app/Widgets.cpp
    )
    if(APPLE)
        set(APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/ShyftrStudio.icns)
        set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        list(APPEND APP_SOURCES ${APP_ICON})
        set(MACOSX_BUNDLE_ICON_FILE ShyftrStudio.icns)
        set(MACOSX_BUNDLE_BUNDLE_NAME "Shyftr Studio")
        set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.shyftr.studio")
        set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
        set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
        set(MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in)
    endif()
    if(WIN32)
        list(APPEND APP_SOURCES resources/windows/app.rc)
    endif()
    qt_add_executable(ShyftrStudio MACOSX_BUNDLE WIN32 ${APP_SOURCES})
    target_link_libraries(ShyftrStudio PRIVATE shyftr_engine Qt6::Widgets Qt6::Multimedia)
    set_target_properties(ShyftrStudio PROPERTIES OUTPUT_NAME "Shyftr Studio")
    install(TARGETS ShyftrStudio BUNDLE DESTINATION . RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------- tests
if(SHYFTR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
