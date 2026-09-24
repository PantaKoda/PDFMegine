find_path(ONNXRuntime_INCLUDE_DIR
  NAMES onnxruntime_cxx_api.h
  HINTS "${ONNXRUNTIME_ROOT}" "${ORT_DIR}" ENV ONNXRUNTIME_ROOT
  PATH_SUFFIXES include
)

find_library(ONNXRuntime_LIBRARY
  NAMES onnxruntime
  HINTS "${ONNXRUNTIME_ROOT}" "${ORT_DIR}" ENV ONNXRUNTIME_ROOT
  PATH_SUFFIXES lib lib64
)

if(WIN32)
  find_file(ONNXRuntime_RUNTIME
    NAMES onnxruntime.dll
    HINTS "${ONNXRUNTIME_ROOT}" "${ORT_DIR}" ENV ONNXRUNTIME_ROOT
    PATH_SUFFIXES lib bin
  )
else()
  set(ONNXRuntime_RUNTIME "${ONNXRuntime_LIBRARY}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ONNXRuntime
  REQUIRED_VARS
    ONNXRuntime_INCLUDE_DIR
    ONNXRuntime_LIBRARY
    ONNXRuntime_RUNTIME
)

if(ONNXRuntime_FOUND AND NOT TARGET ONNXRuntime::ONNXRuntime)
  add_library(ONNXRuntime::ONNXRuntime SHARED IMPORTED)
  set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}"
    IMPORTED_LOCATION "${ONNXRuntime_RUNTIME}"
  )
  if(WIN32)
    set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
      IMPORTED_IMPLIB "${ONNXRuntime_LIBRARY}"
    )
  endif()
endif()

mark_as_advanced(
  ONNXRuntime_INCLUDE_DIR
  ONNXRuntime_LIBRARY
  ONNXRuntime_RUNTIME
)
