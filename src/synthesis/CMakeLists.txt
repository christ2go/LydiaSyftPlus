set (SYNTHESIS_LIB_NAME "synthesis")
set (SYNTHESIS_MODULE_PATH "${PROJECT_SOURCE_DIR}/src/synthesis")
set (SYNTHESIS_INCLUDE_PATH  "${SYNTHESIS_MODULE_PATH}/header")
set (SYNTHESIS_SRC_PATH "${SYNTHESIS_MODULE_PATH}/source" )

include_directories(${EXT_INCLUDE_PATH} ${SYNTHESIS_INCLUDE_PATH} ${PARSER_INCLUDE_PATH} ${UTILS_INCLUDE_PATH})
message(STATUS EXT_INCLUDE_PATH ${EXT_INCLUDE_PATH})

#set sources
file (GLOB_RECURSE HEADER_FILES ${SYNTHESIS_INCLUDE_PATH}/*.h)
# To exclude specific .h files
list(REMOVE_ITEM HEADER_FILES
        "${SYNTHESIS_MODULE_PATH}/header/OneStepRealizability.h"
        "${SYNTHESIS_MODULE_PATH}/header/OneStepUnrealizability.h"
        "${SYNTHESIS_MODULE_PATH}/header/Preprocessing.h"
        "${SYNTHESIS_MODULE_PATH}/header/synthesizer/FairnessLtlfSynthesizer.h"
        "${SYNTHESIS_MODULE_PATH}/header/synthesizer/GR1LTLfSynthesizer.h"
        "${SYNTHESIS_MODULE_PATH}/header/synthesizer/LTLfMaxSetSynthesizer.h"
        "${SYNTHESIS_MODULE_PATH}/header/synthesizer/LTLfSynthesizer.h"
        "${SYNTHESIS_MODULE_PATH}/header/synthesizer/StabilityLtlfSynthesizer.h"
)
message(STATUS "Value of HEADER_FILES: ${HEADER_FILES}")


file (GLOB_RECURSE SOURCE_FILES ${SYNTHESIS_SRC_PATH}/*.cpp)
# To exclude specific .cpp files
list(REMOVE_ITEM SOURCE_FILES
        "${SYNTHESIS_MODULE_PATH}/source/OneStepRealizability.cpp"
        "${SYNTHESIS_MODULE_PATH}/source/OneStepUnrealizability.cpp"
        "${SYNTHESIS_MODULE_PATH}/source/Preprocessing.cpp"
        "${SYNTHESIS_MODULE_PATH}/source/synthesizer/FairnessLtlfSynthesizer.cpp"
        "${SYNTHESIS_MODULE_PATH}/source/synthesizer/GR1LTLfSynthesizer.cpp"
        "${SYNTHESIS_MODULE_PATH}/source/synthesizer/LTLfMaxSetSynthesizer.cpp"
        "${SYNTHESIS_MODULE_PATH}/source/synthesizer/LTLfSynthesizer.cpp"
        "${SYNTHESIS_MODULE_PATH}/source/synthesizer/StabilityLtlfSynthesizer.cpp"
)

add_library(${SYNTHESIS_LIB_NAME} STATIC ${HEADER_FILES} ${SOURCE_FILES}
        source/game/EmersonLei.cpp
        source/game/ZielonkaTree.cc)

target_link_libraries(${SYNTHESIS_LIB_NAME} ${UTILS_LIB_NAME} ${PARSER_LIB_NAME} ${EXT_LIBRARIES_PATH})

#export vars
set (SYNTHESIS_INCLUDE_PATH  ${SYNTHESIS_INCLUDE_PATH} PARENT_SCOPE)
set (SYNTHESIS_LIB_NAME ${SYNTHESIS_LIB_NAME} PARENT_SCOPE)
