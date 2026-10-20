# A configured SDK must change identity after either a commit or a dirty engine
# source edit, without requiring developers to remember a manual reconfigure.
execute_process(COMMAND git rev-parse --git-path HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _tiny_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git symbolic-ref -q HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _tiny_ref OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(_tiny_git_paths)
if(_tiny_head)
    get_filename_component(_tiny_head "${_tiny_head}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND _tiny_git_paths "${_tiny_head}")
endif()
if(_tiny_ref)
    execute_process(COMMAND git rev-parse --git-path "${_tiny_ref}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _tiny_ref_path OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    get_filename_component(_tiny_ref_path "${_tiny_ref_path}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    # CONFIGURE_DEPENDS also notices a packed ref becoming a loose ref.
    file(GLOB _tiny_loose_ref CONFIGURE_DEPENDS "${_tiny_ref_path}")
    list(APPEND _tiny_git_paths ${_tiny_loose_ref})
endif()
execute_process(COMMAND git rev-parse --git-path packed-refs WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _tiny_packed OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_tiny_packed)
    get_filename_component(_tiny_packed "${_tiny_packed}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(EXISTS "${_tiny_packed}")
        list(APPEND _tiny_git_paths "${_tiny_packed}")
    endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_tiny_git_paths})
file(GLOB_RECURSE _tiny_identity_inputs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.in")
list(APPEND _tiny_identity_inputs "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
list(SORT _tiny_identity_inputs)
set(_tiny_identity_content "")
foreach(path IN LISTS _tiny_identity_inputs)
    file(RELATIVE_PATH relative "${CMAKE_CURRENT_SOURCE_DIR}" "${path}")
    file(SHA256 "${path}" hash)
    string(APPEND _tiny_identity_content "${relative}:${hash}\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_tiny_identity_inputs})
string(SHA256 TINYENGINE_SDK_INPUT_ID "${_tiny_identity_content}")
