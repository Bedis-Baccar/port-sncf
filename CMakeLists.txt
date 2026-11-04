# Minimum CMake version
cmake_minimum_required(VERSION 3.20)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project information
set(PROJECT_VERSION 0.1)
project(tusp VERSION ${PROJECT_VERSION} DESCRIPTION "Train routing using CG")

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

# Définition par défaut du type de build
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if (NOT DEFINED ENV{GUROBI_HOME} AND NOT DEFINED GUROBI_DIR)
    message(FATAL_ERROR "No cache variable named GUROBI_DIR is defined, and GUROBI_HOME was not found in the environment variables. Please, define one of them.")
endif ()

# Source code
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

include_directories(
    ${SRC_DIR}
    ${SRC_DIR}/parser
    ${SRC_DIR}/data
    ${SRC_DIR}/graph
    ${SRC_DIR}/builder
    ${SRC_DIR}/solver
    ${GUROBI_INC}
)

set(
    SOURCE_FILES_
    src/parser/GraphParser.cpp
    src/data/PLink.cpp
    src/data/PNode.cpp
    src/data/Service.cpp
    src/data/Train.cpp
    src/graph/Arc.cpp
    src/graph/Node.cpp
    src/graph/TimeSpaceGraph.cpp
    src/builder/GraphBuilder.cpp
    src/solver/MasterProblem.cpp
    src/solver/PricingProblem.cpp
    src/solver/GlobalStateManager.cpp
    src/solver/SearchTree.cpp
    src/solver/BranchAndPriceSolver.cpp
)

################# Libraries #################
# Add the core library
add_library(tusp_core ${SOURCE_FILES_})

# Finding Gurobi
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
message("CMAKE_MODULE_PATH : ${CMAKE_MODULE_PATH}")
find_package(GUROBI REQUIRED)

# Linking to Gurobi
include_directories(${GUROBI_INCLUDE_DIRS})
target_link_libraries(tusp_core PUBLIC gurobi)

################# Executables #################
# Create the column generation executable
add_executable(benchmark_tusp_solver src/benchmark_B&P.cpp)
target_link_libraries(benchmark_tusp_solver PUBLIC tusp_core)

add_executable(cli_tusp_solver src/cli_B&P.cpp)
target_link_libraries(cli_tusp_solver PUBLIC tusp_core)


################# Build Configurations #################

# --- MODE DEBUG ---
if(CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "Applying Debug options (Sanitizers, -O0) to ALL targets")
    
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # On définit les flags de debug une seule fois
        set(DEBUG_CXX_FLAGS "-g" "-O0" "-fsanitize=address" "-fno-omit-frame-pointer")
        
        # On les applique à la librairie et aux DEUX exécutables
        target_compile_options(tusp_core PRIVATE ${DEBUG_CXX_FLAGS})
        target_compile_options(benchmark_tusp_solver PRIVATE ${DEBUG_CXX_FLAGS})
        target_compile_options(cli_tusp_solver PRIVATE ${DEBUG_CXX_FLAGS})
        
        # Le linker a aussi besoin de savoir qu'on utilise AddressSanitizer
        target_link_options(benchmark_tusp_solver PRIVATE "-fsanitize=address")
        target_link_options(cli_tusp_solver PRIVATE "-fsanitize=address")
    endif()
endif()

# --- MODE RELEASE ---
if(CMAKE_BUILD_TYPE MATCHES Release)
    message(STATUS "Applying aggressive optimizations (Release mode) to ALL targets")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # On définit les flags de release
        set(RELEASE_CXX_FLAGS "-O3" "-DNDEBUG" "-march=native" "-w" "-pthread")
        
        # On les applique partout
        target_compile_options(tusp_core PRIVATE ${RELEASE_CXX_FLAGS})
        target_compile_options(benchmark_tusp_solver PRIVATE ${RELEASE_CXX_FLAGS})
        target_compile_options(cli_tusp_solver PRIVATE ${RELEASE_CXX_FLAGS})
        
        # Activation du Link Time Optimization (LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT lto_supported)
        if(lto_supported)
            message(STATUS "LTO (Link Time Optimization) supporté et activé !")
            set_target_properties(tusp_core PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
            set_target_properties(benchmark_tusp_solver PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
            set_target_properties(cli_tusp_solver PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(WARNING "LTO n'est pas supporté par ce compilateur.")
        endif()
    
    elseif(MSVC) # Si tu compiles sous Windows avec MSVC
        set(MSVC_RELEASE_FLAGS "/O2" "/Ob2" "/DNDEBUG" "/GL" "/w")
        target_compile_options(tusp_core PRIVATE ${MSVC_RELEASE_FLAGS})
        target_compile_options(benchmark_tusp_solver PRIVATE ${MSVC_RELEASE_FLAGS})
        target_compile_options(cli_tusp_solver PRIVATE ${MSVC_RELEASE_FLAGS})
        
        target_link_options(benchmark_tusp_solver PRIVATE "/LTCG")
        target_link_options(cli_tusp_solver PRIVATE "/LTCG")
    endif()
endif()