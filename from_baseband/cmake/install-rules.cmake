if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_INSTALL_INCLUDEDIR include/filterbank-generation-test CACHE PATH "")
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# find_package(<package>) call for consumers to find this project
set(package filterbank-generation-test)

install(
    TARGETS filterbank-generation-test_filterbank-generation-test
    EXPORT filterbank-generation-testTargets
    RUNTIME COMPONENT filterbank-generation-test_Runtime
)

write_basic_package_version_file(
    "${package}ConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)

# Allow package maintainers to freely override the path for the configs
set(
    filterbank-generation-test_INSTALL_CMAKEDIR "${CMAKE_INSTALL_DATADIR}/${package}"
    CACHE PATH "CMake package config location relative to the install prefix"
)
mark_as_advanced(filterbank-generation-test_INSTALL_CMAKEDIR)

install(
    FILES cmake/install-config.cmake
    DESTINATION "${filterbank-generation-test_INSTALL_CMAKEDIR}"
    RENAME "${package}Config.cmake"
    COMPONENT filterbank-generation-test_Development
)

install(
    FILES "${PROJECT_BINARY_DIR}/${package}ConfigVersion.cmake"
    DESTINATION "${filterbank-generation-test_INSTALL_CMAKEDIR}"
    COMPONENT filterbank-generation-test_Development
)

install(
    EXPORT filterbank-generation-testTargets
    NAMESPACE filterbank-generation-test::
    DESTINATION "${filterbank-generation-test_INSTALL_CMAKEDIR}"
    COMPONENT filterbank-generation-test_Development
)

if(PROJECT_IS_TOP_LEVEL)
  include(CPack)
endif()
