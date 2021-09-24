cmake_minimum_required(VERSION 3.6)

project(filterbank-generation-test)

include(cmake/prelude.cmake)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

find_package(OpenCL REQUIRED)
find_package(clFFT REQUIRED)

# ---- Declare executable ----

add_executable(filterbank-generation-test_filterbank-generation-test source/main.cpp)
add_executable(filterbank-generation-test::filterbank-generation-test ALIAS filterbank-generation-test_filterbank-generation-test)

set_target_properties(
    filterbank-generation-test_filterbank-generation-test PROPERTIES
    OUTPUT_NAME filterbank-generation-test
    EXPORT_NAME filterbank-generation-test
)

target_include_directories(filterbank-generation-test_filterbank-generation-test PRIVATE ${OPENCL_INCLUDE_DIR} ${CLFFT_INCLUDE_DIR})
target_link_libraries(filterbank-generation-test_filterbank-generation-test ${OPENCL_LIBRARIES} ${CLFFT_LIBRARIES})


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
