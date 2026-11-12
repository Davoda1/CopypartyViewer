# Use native tar implementations to avoid publishing the builder's account in
# archive ownership headers. Windows includes bsdtar; GNU/Linux normally has tar.
function(cpv_create_tar output directory item)
  if(CMAKE_HOST_WIN32)
    find_program(CPV_TAR NAMES tar.exe
      HINTS "$ENV{SystemRoot}/System32" NO_DEFAULT_PATH)
  endif()
  if(NOT CPV_TAR)
    find_program(CPV_TAR NAMES tar bsdtar REQUIRED)
  endif()
  execute_process(COMMAND "${CPV_TAR}" --version
    OUTPUT_VARIABLE tar_version ERROR_VARIABLE tar_error
    RESULT_VARIABLE tar_status)
  if(NOT tar_status EQUAL 0)
    message(FATAL_ERROR "Cannot identify native tar: ${tar_error}")
  endif()
  if(tar_version MATCHES "GNU tar")
    set(ownership --owner=root:0 --group=root:0 --format=gnu)
  elseif(tar_version MATCHES "bsdtar|libarchive")
    set(ownership --uid 0 --gid 0 --uname root --gname root --format=gnutar)
  else()
    message(FATAL_ERROR "Packaging requires GNU tar or bsdtar (included with Windows)")
  endif()
  execute_process(COMMAND "${CPV_TAR}" ${ownership} -czf "${output}" "${item}"
    WORKING_DIRECTORY "${directory}" COMMAND_ERROR_IS_FATAL ANY)
endfunction()
