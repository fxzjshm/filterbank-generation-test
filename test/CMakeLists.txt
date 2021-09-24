# Parent project does not export its library target, so this CML implicitly
# depends on being added from it, i.e. the testing is done only from the build
# tree and is not feasible from an install location

project(filterbank-generation-testTests LANGUAGES CXX)

add_executable(filterbank-generation-test_test source/filterbank-generation-test_test.cpp)
target_link_libraries(filterbank-generation-test_test PRIVATE filterbank-generation-test_lib)
target_compile_features(filterbank-generation-test_test PRIVATE cxx_std_17)

add_test(NAME filterbank-generation-test_test COMMAND filterbank-generation-test_test)
