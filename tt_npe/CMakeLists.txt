file(GLOB_RECURSE NOC_MODEL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(tt_npe_run ${NOC_MODEL_SRC})
#add_library(tt_npe OBJECT ${NOC_MODEL_SRC})

target_link_libraries(tt_npe_run PUBLIC
    fmt::fmt-header-only
    magic_enum
    yaml-cpp
    #Boost::core
    Boost::container
    #span
)

target_include_directories(tt_npe_run PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
