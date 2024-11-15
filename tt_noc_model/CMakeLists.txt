set(NOC_MODEL_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

add_executable(noc_model ${NOC_MODEL_SRC})
#add_library(noc_model OBJECT ${NOC_MODEL_SRC})
#target_link_libraries(noc_model PUBLIC common)

target_include_directories(noc_model PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
