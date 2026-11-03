# Brave adblock-rust integration for the `vibrium` target.
# Included from CMakeLists.txt right after add_executable(vibrium ...).
#
# Builds rust/vibrium-adblock as a static library with a static C runtime
# (/MT, matching the CEF wrapper configured in CMakeLists.txt) and links it.
# Release configuration only: the Rust library links the release static CRT,
# which does not mix with /MTd.

find_program(VIBRIUM_CARGO cargo REQUIRED)

set(VIBRIUM_RUST_DIR "${CMAKE_CURRENT_LIST_DIR}/../rust")
set(VIBRIUM_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/rust-target")
set(VIBRIUM_RUST_LIB "${VIBRIUM_RUST_TARGET_DIR}/release/vibrium_adblock.lib")
# Static CRT to match CEF's /MT. Overridable for experiments (e.g. non-MSVC hosts).
set(VIBRIUM_RUSTFLAGS "-C target-feature=+crt-static" CACHE STRING "RUSTFLAGS for the Rust library")
set(VIBRIUM_CARGO_ARGS
  --locked --release -p vibrium-adblock --lib
  --manifest-path "${VIBRIUM_RUST_DIR}/Cargo.toml"
  --target-dir "${VIBRIUM_RUST_TARGET_DIR}")

add_custom_target(vibrium_adblock_rust
  COMMAND ${CMAKE_COMMAND} -E env "RUSTFLAGS=${VIBRIUM_RUSTFLAGS}"
          ${VIBRIUM_CARGO} build ${VIBRIUM_CARGO_ARGS}
  BYPRODUCTS "${VIBRIUM_RUST_LIB}"
  COMMENT "Building Rust adblock static library")

# Ask rustc which system libraries and linker options the static library needs.
if(DEFINED ENV{RUSTFLAGS})
  set(_vibrium_had_rustflags TRUE)
  set(_vibrium_saved_rustflags "$ENV{RUSTFLAGS}")
else()
  set(_vibrium_had_rustflags FALSE)
endif()
set(ENV{RUSTFLAGS} "${VIBRIUM_RUSTFLAGS}")
execute_process(
  COMMAND ${VIBRIUM_CARGO} rustc ${VIBRIUM_CARGO_ARGS} --crate-type staticlib
          -- --print=native-static-libs
  RESULT_VARIABLE _vibrium_probe_rc
  ERROR_VARIABLE _vibrium_probe_out
  OUTPUT_QUIET)

# Preserve the environment that CMake inherited.
if(_vibrium_had_rustflags)
  set(ENV{RUSTFLAGS} "${_vibrium_saved_rustflags}")
else()
  unset(ENV{RUSTFLAGS})
endif()

set(VIBRIUM_RUST_NATIVE_LIBS "")
set(VIBRIUM_RUST_NATIVE_LINK_OPTIONS "")
if(_vibrium_probe_rc EQUAL 0 AND
   _vibrium_probe_out MATCHES "native-static-libs: ([^\r\n]*)")
  separate_arguments(_vibrium_native_tokens WINDOWS_COMMAND "${CMAKE_MATCH_1}")
  foreach(_native_token IN LISTS _vibrium_native_tokens)
    # rustc emits /DEFAULTLIB:LIBCMT as a linker option, not a library name.
    if(WIN32 AND MSVC AND _native_token MATCHES "^/DEFAULTLIB:")
      list(APPEND VIBRIUM_RUST_NATIVE_LINK_OPTIONS "${_native_token}")
    else()
      list(APPEND VIBRIUM_RUST_NATIVE_LIBS "${_native_token}")
    endif()
  endforeach()
else()
  message(WARNING "Could not query native-static-libs (rc=${_vibrium_probe_rc}); "
                  "using a default list.\n${_vibrium_probe_out}")
  set(VIBRIUM_RUST_NATIVE_LIBS kernel32.lib advapi32.lib bcrypt.lib ntdll.lib
                               userenv.lib ws2_32.lib dbghelp.lib)
endif()
message(STATUS "Rust native libs: ${VIBRIUM_RUST_NATIVE_LIBS}")
message(STATUS "Rust native linker options: ${VIBRIUM_RUST_NATIVE_LINK_OPTIONS}")

# Rust's native-static-libs output names the Windows import library but does
# not include its Cargo registry directory. Resolve that directory through
# Cargo metadata instead of assuming a user-specific Cargo home path.
set(VIBRIUM_RUST_NATIVE_LINK_DIRS "")
if(WIN32 AND MSVC)
  execute_process(
    COMMAND ${VIBRIUM_CARGO} metadata
            --format-version 1 --locked
            --manifest-path "${VIBRIUM_RUST_DIR}/Cargo.toml"
    RESULT_VARIABLE _vibrium_metadata_rc
    OUTPUT_VARIABLE _vibrium_metadata
    ERROR_VARIABLE _vibrium_metadata_error)

  if(NOT _vibrium_metadata_rc EQUAL 0)
    message(FATAL_ERROR
      "Cargo metadata failed: ${_vibrium_metadata_error}")
  endif()

  string(JSON _vibrium_package_count LENGTH "${_vibrium_metadata}" packages)
  set(_vibrium_windows_lib_found FALSE)

  if(_vibrium_package_count GREATER 0)
    math(EXPR _vibrium_package_last "${_vibrium_package_count} - 1")
    foreach(_index RANGE 0 ${_vibrium_package_last})
      string(JSON _package_name GET
             "${_vibrium_metadata}" packages ${_index} name)

      if(_package_name STREQUAL "windows_x86_64_msvc")
        string(JSON _package_manifest GET
               "${_vibrium_metadata}" packages ${_index} manifest_path)
        get_filename_component(_package_dir "${_package_manifest}" DIRECTORY)
        set(_windows_lib_dir "${_package_dir}/lib")

        foreach(_native_lib IN LISTS VIBRIUM_RUST_NATIVE_LIBS)
          if(_native_lib MATCHES "^windows\..*\.lib$")
            if(EXISTS "${_windows_lib_dir}/${_native_lib}")
              list(APPEND VIBRIUM_RUST_NATIVE_LINK_DIRS "${_windows_lib_dir}")
              set(_vibrium_windows_lib_found TRUE)
              message(STATUS
                "Found Windows Rust import library: ${_windows_lib_dir}/${_native_lib}")
            endif()
          endif()
        endforeach()
      endif()
    endforeach()
  endif()

  if(NOT _vibrium_windows_lib_found)
    message(FATAL_ERROR
      "Could not locate the Windows MSVC import library reported by "
      "rustc in Cargo's windows_x86_64_msvc package.")
  endif()
endif()

add_library(vibrium_adblock STATIC IMPORTED GLOBAL)
set_target_properties(vibrium_adblock PROPERTIES
  IMPORTED_LOCATION "${VIBRIUM_RUST_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${VIBRIUM_RUST_DIR}/vibrium-adblock/include"
  INTERFACE_LINK_LIBRARIES "${VIBRIUM_RUST_NATIVE_LIBS}"
  INTERFACE_LINK_DIRECTORIES "${VIBRIUM_RUST_NATIVE_LINK_DIRS}"
  INTERFACE_LINK_OPTIONS "${VIBRIUM_RUST_NATIVE_LINK_OPTIONS}")
add_dependencies(vibrium_adblock vibrium_adblock_rust)

target_sources(vibrium PRIVATE
  c++/adblock/adblock_cosmetic_router.cc
  c++/adblock/adblock_cosmetic_router.h
  c++/adblock/adblock_engine.cc
  c++/adblock/adblock_engine.h
  c++/adblock/adblock_handler.cc
  c++/adblock/adblock_handler.h
  c++/adblock/adblock_service.cc
  c++/adblock/adblock_service.h
  c++/adblock/request_policy.cc
  c++/adblock/request_policy.h
  c++/adblock/cosmetic_content_js.inc
  c++/selftest.cc
  c++/selftest.h)
target_include_directories(vibrium PRIVATE c++/adblock)
target_link_libraries(vibrium PRIVATE vibrium_adblock)

# Native tests of the CEF-independent layer (not in the default build):
#   cmake --build <dir> --config Release --target vibrium_native_tests vibrium_ffi_smoke_test
foreach(_test IN ITEMS vibrium_native_tests vibrium_ffi_smoke_test)
  if(_test STREQUAL "vibrium_native_tests")
    add_executable(${_test} EXCLUDE_FROM_ALL
      tests/native/adblock_wrapper_tests.cc
      c++/adblock/adblock_engine.cc
      c++/adblock/request_policy.cc)
  else()
    add_executable(${_test} EXCLUDE_FROM_ALL tests/native/ffi_smoke_test.cc)
  endif()
  target_include_directories(${_test} PRIVATE c++/adblock)
  target_link_libraries(${_test} PRIVATE vibrium_adblock)
  set_target_properties(${_test} PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded")
endforeach()
