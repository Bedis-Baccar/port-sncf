find_library(
        GUROBI_LIBRARY
        NAMES gurobi gurobi81 gurobi90 gurobi95 gurobi100 gurobi110 gurobi120 gurobi130
        HINTS ${GUROBI_DIR} $ENV{GUROBI_HOME}
        PATH_SUFFIXES lib)

message("-- Gurobi library : ${GUROBI_LIBRARY}")

if(MSVC)
        # determine Visual Studio year
        if(MSVC_TOOLSET_VERSION EQUAL 142)
            set(MSVC_YEAR "2019")
        elseif(MSVC_TOOLSET_VERSION EQUAL 141)
            set(MSVC_YEAR "2017")
        elseif(MSVC_TOOLSET_VERSION EQUAL 140)
            set(MSVC_YEAR "2015")
        endif()
    
        if(MT)
            set(M_FLAG "mt")
        else()
            set(M_FLAG "md")
        endif()
    
        find_library(
                GUROBI_CXX_LIBRARY
                NAMES gurobi_c++${M_FLAG}${MSVC_YEAR}
                HINTS ${GUROBI_DIR} $ENV{GUROBI_HOME}
                PATH_SUFFIXES lib)
else()
        find_library(
                GUROBI_CXX_LIBRARY
                NAMES gurobi_g++5.2 gurobi_c++
                HINTS ${GUROBI_DIR} $ENV{GUROBI_HOME}
                PATH_SUFFIXES lib)
endif()

message("-- Gurobi C++ library : ${GUROBI_CXX_LIBRARY}")

find_path(
        GUROBI_INCLUDE_DIRS
        NAMES gurobi_c.h
        HINTS ${GUROBI_DIR} $ENV{GUROBI_HOME}
        PATH_SUFFIXES include)


include(FindPackageHandleStandardArgs) # include the "FindPackageHandleStandardArgs" module
find_package_handle_standard_args(GUROBI DEFAULT_MSG GUROBI_LIBRARY GUROBI_CXX_LIBRARY GUROBI_INCLUDE_DIRS)

if (GUROBI_FOUND)
    message("-- Gurobi found, include directories: ${GUROBI_INCLUDE_DIRS}")
    add_library(gurobi STATIC IMPORTED)
    set_target_properties(gurobi PROPERTIES IMPORTED_LOCATION ${GUROBI_CXX_LIBRARY})
    target_link_libraries(gurobi INTERFACE ${GUROBI_LIBRARY})
    target_include_directories(gurobi INTERFACE ${GUROBI_INCLUDE_DIRS})
endif()
        

    