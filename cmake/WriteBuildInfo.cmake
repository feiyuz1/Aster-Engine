if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()
file(WRITE "${OUTPUT}"
    "engine.version=${ENGINE_VERSION}\n"
    "engine.abi=${ENGINE_ABI}\n"
    "scene.format=${SCENE_FORMAT}\n"
    "build.id=${BUILD_ID}\n"
    "build.configuration=${CONFIGURATION}\n"
    "build.compiler=${COMPILER}\n")
