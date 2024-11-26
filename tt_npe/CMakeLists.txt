file(GLOB_RECURSE TT_NPE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(tt_npe_run ${TT_NPE_SRC})
#add_library(tt_npe OBJECT ${TT_NPE_SRC})

target_link_libraries(tt_npe_run PUBLIC
    fmt::fmt
    magic_enum
    yaml-cpp
    Boost::container
    Boost::program_options
    #span
)

target_include_directories(tt_npe_run PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
