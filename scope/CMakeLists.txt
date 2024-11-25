# Link Qt6 libraries
find_package(Qt6 REQUIRED COMPONENTS Charts)

# Define the executable
qt_add_executable(recorder_scope
    recorder_view.cpp
    recorder_scope.cpp
    recorder_slider.cpp
    recorder_slider.h
    recorder_view.h
)

set_target_properties(recorder_scope PROPERTIES AUTOMOC TRUE)

# Include directories
target_include_directories(recorder_scope PRIVATE ${CMAKE_SOURCE_DIR})

# Link the recorder library
target_link_libraries(recorder_scope PRIVATE recorder)

target_link_libraries(recorder_scope PRIVATE Qt6::Charts)

# Installation
install(TARGETS recorder_scope DESTINATION ${CMAKE_INSTALL_BINDIR})