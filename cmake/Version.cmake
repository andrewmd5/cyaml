find_package(Git QUIET)
set(CYAML_VERSION_STR "")

# Only inspect this checkout; vendored archives may sit in a parent's repo.
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*.[0-9]*.[0-9]*"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE CYAML_VERSION_STR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE CYAML_GIT_RESULT
    )
    if(NOT CYAML_GIT_RESULT EQUAL 0)
        set(CYAML_VERSION_STR "")
    endif()
endif()

# git archive substitutes this with the same tag description, so release source
# archives retain their version without Git, history, or network access.
if(NOT CYAML_VERSION_STR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git_archival.txt")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.git_archival.txt" CYAML_ARCHIVE_VERSION
        REGEX "^describe: v[0-9]")
    string(REGEX REPLACE "^describe: " "" CYAML_VERSION_STR "${CYAML_ARCHIVE_VERSION}")
endif()

if(CYAML_VERSION_STR MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9]+-g[0-9a-f]+)?$")
    set(CYAML_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(CYAML_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(CYAML_VERSION_PATCH "${CMAKE_MATCH_3}")
    set(CYAML_VERSION "${CYAML_VERSION_MAJOR}.${CYAML_VERSION_MINOR}.${CYAML_VERSION_PATCH}")
    string(REGEX REPLACE "^v" "" CYAML_VERSION_STR "${CYAML_VERSION_STR}")
else()
    set(CYAML_VERSION_MAJOR 0)
    set(CYAML_VERSION_MINOR 0)
    set(CYAML_VERSION_PATCH 0)
    set(CYAML_VERSION "0.0.0")
    set(CYAML_VERSION_STR "0.0.0-dev")
    message(WARNING "CYAML version unavailable: fetch release tags or use a git archive source distribution")
endif()
