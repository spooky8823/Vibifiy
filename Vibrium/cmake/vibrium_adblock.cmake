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

# Ask rustc which system libraries the static library needs instead of
# hard-coding a list that changes between Rust releases.
set(ENV{RUSTFLAGS} "${VIBRIUM_RUSTFLAGS}")
execute_process(
  COMMAND ${VIBRIUM_CARGO} rustc ${VIBRIUM_CARGO_ARGS} --crate-type staticlib
          -- --print=native-static-libs
  RESULT_VARIABLE _vibrium_probe_rc
  ERROR_VARIABLE _vibrium_probe_out
  OUTPUT_QUIET)
unset(ENV{RUSTFLAGS})
if(_vibrium_probe_rc EQUAL 0 AND
   _vibrium_probe_out MATCHES "native-static-libs: ([^\r\n]*)")
  separate_arguments(VIBRIUM_RUST_NATIVE_LIBS WINDOWS_COMMAND "${CMAKE_MATCH_1}")
else()
  message(WARNING "Could not query native-static-libs (rc=${_vibrium_probe_rc}); "
                  "using a default list.\n${_vibrium_probe_out}")
  set(VIBRIUM_RUST_NATIVE_LIBS kernel32.lib advapi32.lib bcrypt.lib ntdll.lib
                               userenv.lib ws2_32.lib dbghelp.lib)
endif()
message(STATUS "Rust native libs: ${VIBRIUM_RUST_NATIVE_LIBS}")

add_library(vibrium_adblock STATIC IMPORTED GLOBAL)
set_target_properties(vibrium_adblock PROPERTIES
  IMPORTED_LOCATION "${VIBRIUM_RUST_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${VIBRIUM_RUST_DIR}/vibrium-adblock/include"
  INTERFACE_LINK_LIBRARIES "${VIBRIUM_RUST_NATIVE_LIBS}")
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
