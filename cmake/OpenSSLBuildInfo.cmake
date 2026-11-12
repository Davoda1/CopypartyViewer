# Generate OpenSSL's diagnostic build description without machine-local paths.
# This changes generated build metadata only; pinned upstream sources are intact.
foreach(required CPV_OPENSSL_SOURCE CPV_OPENSSL_BINARY CPV_PERL CPV_COMPILER_DESCRIPTION)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
file(MAKE_DIRECTORY "${CPV_OPENSSL_BINARY}/crypto")
execute_process(
  COMMAND "${CPV_PERL}" "${CPV_OPENSSL_SOURCE}/util/mkbuildinf.pl"
    "${CPV_COMPILER_DESCRIPTION} (build paths omitted)" linux-x86_64
  OUTPUT_FILE "${CPV_OPENSSL_BINARY}/crypto/buildinf.h"
  COMMAND_ERROR_IS_FATAL ANY)
