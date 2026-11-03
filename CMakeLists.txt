cmake_minimum_required(VERSION 3.20)
project(Portfolio_Risk_Engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Portfolio_Risk_Engine main.cpp)

include(CTest)
if(BUILD_TESTING)
    add_executable(risk_tests tests/risk_tests.cpp)
    add_test(NAME risk_tests COMMAND risk_tests)
endif()
