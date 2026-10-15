# Writes <FILE>.sha256 as "<hash>  <file name>", the format CPack uses for the
# other packages.  Usage: cmake -DFILE=<path> -P WriteChecksum.cmake
if(NOT DEFINED FILE OR NOT EXISTS "${FILE}")
    message(FATAL_ERROR "WriteChecksum.cmake: pass -DFILE=<existing file>")
endif()
file(SHA256 "${FILE}" _hash)
get_filename_component(_name "${FILE}" NAME)
file(WRITE "${FILE}.sha256" "${_hash}  ${_name}\n")
