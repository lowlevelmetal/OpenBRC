# CPACK_PRE_BUILD_SCRIPTS hook for the Windows ZIP package: adds portable.txt
# so the unpacked game keeps its settings next to the executable (see
# src/core/Paths.h). Runs after CPack staged the files, before archiving.
if(NOT CPACK_GENERATOR STREQUAL "ZIP")
    return()
endif()
if(NOT CPACK_TEMPORARY_INSTALL_DIRECTORY OR NOT EXISTS "${CPACK_OPENBRC_PORTABLE_TXT}")
    message(FATAL_ERROR "CPackPortable.cmake: missing staging directory or portable.txt")
endif()
# The staging directory holds one subdirectory per component when components
# are packaged separately; with a single archive it is the install root.
file(COPY "${CPACK_OPENBRC_PORTABLE_TXT}" DESTINATION "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
