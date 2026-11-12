get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(stage "${root}/build/source-package")
set(name "Copyparty-Viewer-1.0.0")
file(REMOVE_RECURSE "${stage}")
file(MAKE_DIRECTORY "${stage}/${name}" "${root}/dist")
# Explicit allowlist excludes build products, local settings, logs and test credentials.
foreach(item CMakeLists.txt Makefile README.md BUILDING.md LICENSE THIRD_PARTY_NOTICES.md CHANGELOG.md CONTRIBUTING.md SECURITY.md .gitignore .gitattributes .github scripts src assets cmake packaging tests vendor docs)
  if(EXISTS "${root}/${item}")
    file(COPY "${root}/${item}" DESTINATION "${stage}/${name}"
      PATTERN ".git" EXCLUDE
      PATTERN ".env*" EXCLUDE
      PATTERN "settings.json" EXCLUDE
      PATTERN "private-defaults.cmake" EXCLUDE
      PATTERN "private-site.cmake" EXCLUDE
      PATTERN "site-defaults.cmake" EXCLUDE
      PATTERN "*.pem" EXCLUDE
      PATTERN "*.key" EXCLUDE
      PATTERN "*.p12" EXCLUDE
      PATTERN "*.pfx" EXCLUDE
      PATTERN "*.log" EXCLUDE
      PATTERN "__pycache__" EXCLUDE
      PATTERN "*.pyc" EXCLUDE
      PATTERN "build" EXCLUDE
      PATTERN "dist" EXCLUDE)
  endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/ArchiveTar.cmake")
cpv_create_tar("${root}/dist/Copyparty-Viewer-1.0.0-Source.tar.gz" "${stage}" "${name}")
message(STATUS "Created ${root}/dist/Copyparty-Viewer-1.0.0-Source.tar.gz")
