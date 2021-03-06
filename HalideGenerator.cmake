include(CMakeParseArguments)

function(halide_generator_genfiles_dir NAME OUTVAR)
  set(GENFILES_DIR "${CMAKE_BINARY_DIR}/generator_genfiles/${NAME}")
  file(MAKE_DIRECTORY "${GENFILES_DIR}")
  set(${OUTVAR} "${GENFILES_DIR}" PARENT_SCOPE)
endfunction()

function(halide_generator_get_exec_path TARGET OUTVAR)
  if(MSVC)
    # In MSVC, the generator executable will be placed in a configuration specific
    # directory specified by ${CMAKE_CFG_INTDIR}.
    set(${OUTVAR} "${CMAKE_BINARY_DIR}/bin/${CMAKE_CFG_INTDIR}/${TARGET}${CMAKE_EXECUTABLE_SUFFIX}" PARENT_SCOPE)
  elseif(XCODE)
    # In Xcode, the generator executable will be placed in a configuration specific
    # directory, so the Xcode variable $(CONFIGURATION) is passed in the custom build script.
    set(${OUTVAR} "${CMAKE_BINARY_DIR}/bin/$(CONFIGURATION)/${TARGET}${CMAKE_EXECUTABLE_SUFFIX}" PARENT_SCOPE)
  else()
    set(${OUTVAR} "${CMAKE_BINARY_DIR}/bin/${TARGET}${CMAKE_EXECUTABLE_SUFFIX}" PARENT_SCOPE)
  endif()
endfunction()

function(halide_generator_add_exec_generator_target EXEC_TARGET)
  set(options )
  set(oneValueArgs GENERATOR_TARGET GENFILES_DIR)
  set(multiValueArgs OUTPUTS GENERATOR_ARGS)
  cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  halide_generator_get_exec_path(${args_GENERATOR_TARGET} EXEC_PATH)

  add_custom_command(
    OUTPUT ${args_OUTPUTS}
    DEPENDS ${args_GENERATOR_TARGET}
    COMMAND ${EXEC_PATH} ${args_GENERATOR_ARGS}
    WORKING_DIRECTORY ${args_GENFILES_DIR}
    COMMENT "Executing Generator ${args_GENERATOR_TARGET} with args ${args_GENERATOR_ARGS}..."
  )

  add_custom_target(${EXEC_TARGET} DEPENDS ${args_OUTPUTS})
  set_target_properties(${EXEC_TARGET} PROPERTIES FOLDER "generator")
endfunction()

# This function adds custom build steps to invoke a Halide generator exectuable
# and produce a static library containing the generated code.
#
# The generator executable must be produced separately, e.g. using a call to the
# function halide_add_generator() or halide_project(...) or add_executable(...) 
# and passed to this function in the GENERATOR_TARGET parameter.
#
# Usage:
#   halide_add_aot_library(<name>
#                          GENERATOR_TARGET <target>
#                          GENERATOR_NAME <string>
#                          GENERATED_FUNCTION <string>
#                          GENERATOR_OUTPUTS <arg> <arg> ...
#                          GENERATOR_ARGS <arg> <arg> ...)
#
#   <name> is the name of the library being defined.
#   GENERATOR_TARGET is the name of the generator executable target, which is assumed to be
#       defined elsewhere.
#   GENERATOR_TARGET is the name of the generator executable target, which is assumed to be
#       defined elsewhere.
#   GENERATOR_NAME is the registered name of the Halide::Generator derived object
#   GENERATED_FUNCTION is the name of the C function to be generated by Halide, including C++ 
#       namespace (if any); if omitted, default to GENERATOR_NAME
#   GENERATOR_OUTPUTS are the values to pass to -e; if omitted, defaults to "h static_library"
#   GENERATOR_ARGS are optional extra arguments passed to the generator executable during
#     build.
function(halide_add_aot_library AOT_LIBRARY_TARGET)

  # Parse arguments
  set(options )
  set(oneValueArgs GENERATOR_TARGET GENERATOR_NAME GENERATED_FUNCTION)
  set(multiValueArgs GENERATOR_ARGS GENERATOR_OUTPUTS)
  cmake_parse_arguments(args "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (args_GENERATED_FUNCTION STREQUAL "")
    set(args_GENERATED_FUNCTION ${args_GENERATOR_NAME})
  endif()

  # Create a directory to contain generator specific intermediate files
  halide_generator_genfiles_dir(${AOT_LIBRARY_TARGET} GENFILES_DIR)

  # Determine the name of the output files
  set(FILTER_LIB "${AOT_LIBRARY_TARGET}${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(FILTER_HDR "${AOT_LIBRARY_TARGET}.h")

  set(GENERATOR_EXEC_ARGS "-o" "${GENFILES_DIR}")
  if (NOT ${args_GENERATED_FUNCTION} STREQUAL "")
    list(APPEND GENERATOR_EXEC_ARGS "-f" "${args_GENERATED_FUNCTION}" )
  endif()
  if (NOT ${args_GENERATOR_NAME} STREQUAL "")
    list(APPEND GENERATOR_EXEC_ARGS "-g" "${args_GENERATOR_NAME}")
  endif()
  if (NOT ${args_GENERATOR_OUTPUTS} STREQUAL "")
    list(APPEND GENERATOR_EXEC_ARGS "-e" ${args_GENERATOR_OUTPUTS})
  endif()
  # GENERATOR_ARGS always come last
  list(APPEND GENERATOR_EXEC_ARGS ${args_GENERATOR_ARGS})

  halide_generator_add_exec_generator_target(
    "${AOT_LIBRARY_TARGET}.exec_generator"
    GENERATOR_TARGET ${args_GENERATOR_TARGET} 
    GENERATOR_ARGS   "${GENERATOR_EXEC_ARGS}"
    GENFILES_DIR     ${GENFILES_DIR}
    OUTPUTS          "${GENFILES_DIR}/${FILTER_LIB}" "${GENFILES_DIR}/${FILTER_HDR}"
  )
  set_source_files_properties("${GENFILES_DIR}/${FILTER_HDR}" PROPERTIES GENERATED TRUE)
endfunction(halide_add_aot_library)

# Usage:
#   halide_add_aot_library_dependency(TARGET AOT_LIBRARY_TARGET)
function(halide_add_aot_library_dependency TARGET AOT_LIBRARY_TARGET)
    halide_generator_genfiles_dir(${AOT_LIBRARY_TARGET} GENFILES_DIR)
  
    add_dependencies("${TARGET}" "${AOT_LIBRARY_TARGET}.exec_generator")

    set(FILTER_LIB "${AOT_LIBRARY_TARGET}${CMAKE_STATIC_LIBRARY_SUFFIX}")
    target_link_libraries("${TARGET}" PRIVATE "${GENFILES_DIR}/${FILTER_LIB}")
    target_include_directories("${TARGET}" PRIVATE "${GENFILES_DIR}")

    if (WIN32)
      if (MSVC)
        # /FORCE:multiple allows clobbering the halide runtime symbols in the lib
        set_target_properties("${TARGET}" PROPERTIES LINK_FLAGS "/STACK:8388608,1048576 /FORCE:multiple")
      else()
        set_target_properties("${TARGET}" PROPERTIES LINK_FLAGS "-Wl,--allow-multiple-definition")
      endif()
    else()
      target_link_libraries("${TARGET}" PRIVATE dl pthread z)
    endif()
endfunction(halide_add_aot_library_dependency)

function(halide_add_generator NAME)
  set(options )
  set(oneValueArgs )
  set(multiValueArgs SRCS)
  cmake_parse_arguments(args "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  halide_project("${NAME}" 
                 "generator" 
                 "${CMAKE_SOURCE_DIR}/tools/GenGen.cpp" 
                 ${args_SRCS})
endfunction(halide_add_generator)
