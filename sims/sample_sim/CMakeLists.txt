cmake_minimum_required(VERSION 3.15)
project(sample_sim C)

# Find ZeroMQ dependency
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZeroMQ REQUIRED libzmq)

# Create the executable
add_executable(sample_sim
    src/sample_sim.c
)

# Include directories
target_include_directories(sample_sim
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/simulith/include
    ${ZeroMQ_INCLUDE_DIRS}
)

# Link against Simulith and ZeroMQ
target_link_libraries(sample_sim
    PRIVATE
    simulith
    ${ZeroMQ_LIBRARIES}
)

# Set C standard
set_target_properties(sample_sim PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Add dependency on simulith
add_dependencies(sample_sim simulith) 