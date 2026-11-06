cmake_minimum_required(VERSION 3.21)

project(OBD2_Scanner_Pro
    VERSION 2.0.0
    DESCRIPTION "Sistema de Diagnóstico Vehicular Multiplataforma Avanzado OBD2/UDS (OBDb / opendbc)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opciones automáticas de MOC, UIC y RCC para Qt6
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Soporte para dependencias en prefijo adicional si están presentes en /tmp
if(EXISTS "/tmp/qt6_deps/usr")
    list(PREPEND CMAKE_PREFIX_PATH "/tmp/qt6_deps/usr" "/tmp/qt6_deps/usr/lib/x86_64-linux-gnu/cmake")
    set(Qt6SerialPort_DIR "/tmp/qt6_deps/usr/lib/x86_64-linux-gnu/cmake/Qt6SerialPort" CACHE PATH "" FORCE)
    set(Qt6SerialBus_DIR "/tmp/qt6_deps/usr/lib/x86_64-linux-gnu/cmake/Qt6SerialBus" CACHE PATH "" FORCE)
    set(OPENGL_opengl_LIBRARY "/tmp/qt6_deps/usr/lib/x86_64-linux-gnu/libOpenGL.so" CACHE FILEPATH "" FORCE)
    set(OPENGL_glx_LIBRARY "/tmp/qt6_deps/usr/lib/x86_64-linux-gnu/libGLX.so" CACHE FILEPATH "" FORCE)
    set(OPENGL_INCLUDE_DIR "/tmp/qt6_deps/usr/include" CACHE PATH "" FORCE)
    set(XKB_LIBRARY "/tmp/qt6_deps/usr/lib/x86_64-linux-gnu/libxkbcommon.so" CACHE FILEPATH "" FORCE)
    set(XKB_INCLUDE_DIR "/tmp/qt6_deps/usr/include" CACHE PATH "" FORCE)
    link_directories("/tmp/qt6_deps/usr/lib" "/tmp/qt6_deps/usr/lib/x86_64-linux-gnu")
endif()

# Cumplimiento estricto LGPLv3 para uso comercial:
# Enlace dinámico exclusivo con módulos LGPLv3 de Qt6.
find_package(Qt6 6.2 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    SerialPort
    SerialBus
    Sql
    Test
)

# Fuentes de la aplicación principal
set(PROJECT_SOURCES
    src/main.cpp
    src/hal/IObdInterface.h
    src/hal/SerialElmDriver.h
    src/hal/SerialElmDriver.cpp
    src/hal/SocketCanDriver.h
    src/hal/SocketCanDriver.cpp
    src/hal/MockElmDriver.h
    src/hal/MockElmDriver.cpp
    src/core/SafetyGuard.h
    src/core/SafetyGuard.cpp
    src/core/SignalExtractor.h
    src/core/SignalExtractor.cpp
    src/core/ObdbJsonLoader.h
    src/core/ObdbJsonLoader.cpp
    src/core/DbcParser.h
    src/core/DbcParser.cpp
    src/core/ObdProtocol.h
    src/core/ObdProtocol.cpp
    src/core/UdsProtocol.h
    src/core/UdsProtocol.cpp
    src/core/PidDecoder.h
    src/core/PidDecoder.cpp
    src/core/DtcManager.h
    src/core/DtcManager.cpp
)

add_executable(OBD2_Scanner_Pro ${PROJECT_SOURCES})

# Recursos QML embebidos para la UI nativa
qt_add_resources(OBD2_Scanner_Pro "qml_res"
    PREFIX "/"
    BASE "src"
    FILES
        src/ui/MainDashboard.qml
        src/ui/LiveGraphView.qml
)

target_include_directories(OBD2_Scanner_Pro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

# Enlace dinámico con bibliotecas Qt6 LGPLv3
target_link_libraries(OBD2_Scanner_Pro PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::SerialPort
    Qt6::SerialBus
    Qt6::Sql
)

# Configuración específica multiplataforma
if(MSVC)
    target_compile_options(OBD2_Scanner_Pro PRIVATE /W4 /permissive-)
else()
    target_compile_options(OBD2_Scanner_Pro PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Asegurar que la base de datos, escenarios y opendbc se copien al directorio de compilación
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/database/dtc_codes_es.sqlite"
     "${CMAKE_CURRENT_SOURCE_DIR}/database/scenarios"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/database")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/opendbc"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# Suite de pruebas automatizadas
enable_testing()
add_subdirectory(tests)
