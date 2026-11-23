cmake_minimum_required(VERSION 3.22)

project(SightProc VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
include(GNUInstallDirs)

add_executable(sightproc
    src/main.cpp
    src/core/Lang.h
    src/core/Lang.cpp
    src/core/AppSettings.h
    src/core/AppSettings.cpp
    src/core/RingBuffer.h
    src/core/TelemetryTypes.h
    src/core/FormatUtils.h
    src/core/FormatUtils.cpp
    src/core/BatteryCollector.h
    src/core/BatteryCollector.cpp
    src/core/DisplaySessionDetector.h
    src/core/DisplaySessionDetector.cpp
    src/core/HwmonCollector.h
    src/core/HwmonCollector.cpp
    src/core/CpuCollector.h
    src/core/CpuCollector.cpp
    src/core/MemoryCollector.h
    src/core/MemoryCollector.cpp
    src/core/DiskCollector.h
    src/core/DiskCollector.cpp
    src/core/NetworkCollector.h
    src/core/NetworkCollector.cpp
    src/core/NvmlBridge.h
    src/core/NvmlBridge.cpp
    src/core/GpuCollector.h
    src/core/GpuCollector.cpp
    src/core/TelemetryWorker.h
    src/core/TelemetryWorker.cpp
    src/core/NetlinkSockDiag.h
    src/core/NetlinkSockDiag.cpp
    src/core/DesktopEntryIndex.h
    src/core/DesktopEntryIndex.cpp
    src/core/ProcessCollector.h
    src/core/ProcessCollector.cpp
    src/core/ProcessWorker.h
    src/core/ProcessWorker.cpp
    src/core/SystemdController.h
    src/core/SystemdController.cpp
    src/models/DeviceListModel.h
    src/models/DeviceListModel.cpp
    src/models/ProcessTreeModel.h
    src/models/ProcessTreeModel.cpp
    src/models/ProcessFilterProxyModel.h
    src/models/ProcessFilterProxyModel.cpp
    src/models/ApplicationTableModel.h
    src/models/ApplicationTableModel.cpp
    src/models/ServiceTableModel.h
    src/models/ServiceTableModel.cpp
    src/widgets/ThemeUtils.h
    src/widgets/ThemeUtils.cpp
    src/widgets/AreaChartWidget.h
    src/widgets/AreaChartWidget.cpp
    src/widgets/CombinedCpuWidget.h
    src/widgets/CombinedCpuWidget.cpp
    src/widgets/DeviceListDelegate.h
    src/widgets/DeviceListDelegate.cpp
    src/widgets/CoreGridWidget.h
    src/widgets/CoreGridWidget.cpp
    src/widgets/MemoryBarWidget.h
    src/widgets/MemoryBarWidget.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/PerformanceTab.h
    src/ui/PerformanceTab.cpp
    src/ui/ApplicationsTab.h
    src/ui/ApplicationsTab.cpp
    src/ui/ProcessesTab.h
    src/ui/ProcessesTab.cpp
    src/ui/ServicesTab.h
    src/ui/ServicesTab.cpp
    src/ui/ProcessDetailsDialog.h
    src/ui/ProcessDetailsDialog.cpp
    src/ui/PreferencesDialog.h
    src/ui/PreferencesDialog.cpp
)

target_include_directories(sightproc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(sightproc PRIVATE
    Qt6::Widgets
    ${CMAKE_DL_LIBS}
)

target_compile_options(sightproc PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

qt_add_resources(sightproc "sightproc_resources"
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/data/icons"
    FILES
        data/icons/sightproc.svg
)

install(TARGETS sightproc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES data/icons/sightproc.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
install(FILES sightproc.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
