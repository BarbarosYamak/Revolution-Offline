# Deterministic protocol/state tests. Built with the normal build and run by
# ctest; they need no server, no MUL files and no network.
#
# The older *_probe.cpp files in this directory are manual diagnostic tools
# (they want game assets or a live shard) and are deliberately not wired up.

add_executable(sphere_regression sphere_regression.cpp
    ${CMAKE_SOURCE_DIR}/src/net/PacketStream.cpp
    ${CMAKE_SOURCE_DIR}/src/builders/Builders.cpp)
target_include_directories(sphere_regression PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src)

add_test(NAME sphere_regression COMMAND sphere_regression)
