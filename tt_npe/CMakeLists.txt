#-----------------------------------------------------------
#------- src file globs ------------------------------------
#-----------------------------------------------------------
# cpp files for shared lib build
file(GLOB_RECURSE TT_NPE_SRC 
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
# cpp files for tt_npe_run cli exe
file(GLOB_RECURSE TT_NPE_CLI_SRC 
	"${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp"
)
# cpp files for unit tests
file(GLOB_RECURSE TT_NPE_UT_SRC
	"${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp"
)

#-----------------------------------------------------------
#------- common link deps ----------------------------------
#-----------------------------------------------------------
add_library(npe_common_libs INTERFACE)
target_link_libraries(
    npe_common_libs
    INTERFACE
        fmt::fmt
        magic_enum
        yaml-cpp
        Boost::container
        Boost::program_options
)
#-----------------------------------------------------------
#------- library (libtt_npe.so) config ----------------------
#-----------------------------------------------------------
add_library(tt_npe SHARED ${TT_NPE_SRC})

target_link_libraries(tt_npe PUBLIC 
	npe_common_libs
)

target_include_directories(tt_npe PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(tt_npe PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

#-----------------------------------------------------------
#------- Python Bindings -----------------------------------
#-----------------------------------------------------------
pybind11_add_module(tt_npe_pybind pybind/11/bindings.cpp)
target_link_libraries(tt_npe_pybind PRIVATE tt_npe)

#-----------------------------------------------------------
#------- exe (tt_npe_run) config ----------------------------
#-----------------------------------------------------------
add_executable(tt_npe_run ${TT_NPE_CLI_SRC})
target_link_libraries(tt_npe_run PRIVATE 
	npe_common_libs 
	tt_npe
)
target_include_directories(tt_npe_run PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

#-----------------------------------------------------------
#------- unit test config ----------------------------------
#-----------------------------------------------------------
add_executable(tt_npe_ut ${test} ${TT_NPE_UT_SRC})
target_link_libraries(tt_npe_ut PRIVATE 
	npe_common_libs 
    test_common_libs
	tt_npe
)
target_include_directories(tt_npe_ut PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

#-----------------------------------------------------------
#------- Installation --------------------------------------
#-----------------------------------------------------------
install(TARGETS tt_npe tt_npe_run
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)
