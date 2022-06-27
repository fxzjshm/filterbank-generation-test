cmake_minimum_required(VERSION 3.6)

project(filterbank-generation-test)

include(cmake/prelude.cmake)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

find_package(OpenCL REQUIRED)
find_package(clFFT REQUIRED)
find_package(Boost 1.61 COMPONENTS program_options REQUIRED)

set(CMAKE_CXX_STANDARD 17) 

# ---- Declare executable ----

add_executable(filterbank-generation-test source/main.cpp)
add_executable(pad_filterbank source/pad_filterbank.cpp)

# set_target_properties(
#     filterbank-generation-test_filterbank-generation-test PROPERTIES
#     OUTPUT_NAME filterbank-generation-test
#     EXPORT_NAME filterbank-generation-test
# )

target_include_directories(filterbank-generation-test PRIVATE ${OPENCL_INCLUDE_DIR} ${CLFFT_INCLUDE_DIR} ${Boost_INCLUDE_DIR})
target_link_libraries(filterbank-generation-test ${OPENCL_LIBRARIES} ${CLFFT_LIBRARIES} ${Boost_LIBRARIES})

target_include_directories(pad_filterbank PRIVATE ${Boost_INCLUDE_DIR})
target_link_libraries(pad_filterbank ${Boost_LIBRARIES})

# ---- Developer mode ----

if(NOT filterbank-generation-test_DEVELOPER_MODE)
  return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
  message(
      AUTHOR_WARNING
      "Developer mode is intended for developers of filterbank-generation-test"
  )
endif()

include(cmake/dev-mode.cmake)
