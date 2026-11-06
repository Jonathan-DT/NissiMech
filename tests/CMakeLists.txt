if(EXISTS "/tmp/qt6_deps/usr/lib")
    link_directories("/tmp/qt6_deps/usr/lib")
endif()

add_executable(tst_obd_simulator
    tst_obd_simulator.cpp
    ../src/hal/IObdInterface.h
    ../src/hal/MockElmDriver.h
    ../src/hal/MockElmDriver.cpp
    ../src/hal/SerialElmDriver.h
    ../src/hal/SerialElmDriver.cpp
    ../src/hal/SocketCanDriver.h
    ../src/hal/SocketCanDriver.cpp
    ../src/core/SafetyGuard.h
    ../src/core/SafetyGuard.cpp
    ../src/core/PidDecoder.h
    ../src/core/PidDecoder.cpp
    ../src/core/DtcManager.h
    ../src/core/DtcManager.cpp
    ../src/core/ObdProtocol.h
    ../src/core/ObdProtocol.cpp
    ../src/core/UdsProtocol.h
    ../src/core/UdsProtocol.cpp
)

target_include_directories(tst_obd_simulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
)

target_link_libraries(tst_obd_simulator PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Sql
    Qt6::SerialPort
    Qt6::SerialBus
)

add_test(NAME tst_obd_simulator COMMAND tst_obd_simulator)
