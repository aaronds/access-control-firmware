# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/main/errors.json JSON_ERRORS)


string(JSON TAGS_LENGTH LENGTH "${JSON_ERRORS}" "tags")
math(EXPR TAGS_RANGE "${TAGS_LENGTH}-1")

foreach(i RANGE ${TAGS_RANGE})
    string(JSON TAG_NAME GET ${JSON_ERRORS} "tags" ${i} "name")
    string(JSON TAG_VALUE GET ${JSON_ERRORS} "tags" ${i} "value")
    add_definitions(-D${TAG_NAME}=${TAG_VALUE})

    string(JSON ERRORS_LENGTH LENGTH ${JSON_ERRORS} tags ${i} errors)
    math(EXPR ERRORS_RANGE "${ERRORS_LENGTH}-1")

    foreach(j RANGE ${ERRORS_RANGE})
        string(JSON ERROR_NAME GET ${JSON_ERRORS} tags ${i} errors ${j} name)
        string(JSON ERROR_VALUE GET ${JSON_ERRORS} tags ${i} errors ${j} value)
        add_definitions(-D${TAG_NAME}_${ERROR_NAME}=${ERROR_VALUE})
    endforeach()

endforeach()

#string(JSON CUR_NAME GET ${JSON_ERRORS} tags 0 name)
#string(JSON CUR_VALUE GET ${JSON_ERRORS} tags 0 value)
#message("Current Name: ${CUR_NAME}")
#add_definitions(-D${CUR_NAME}=${CUR_VALUE})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(AccessControlV2)
