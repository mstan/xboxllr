# Bound Windows lazy commits to the actual pinned translation reservation.
# Keep the upstream checkout and every non-Windows allocator unchanged.
set(codebuffer_original "${XLLE_UNICORN_SOURCE}/qemu/accel/tcg/translate-all.c")
file(SHA256 "${codebuffer_original}" codebuffer_original_sha)
if(NOT codebuffer_original_sha STREQUAL "d9e5abf75d9305ff96f50f8626f956893aba6c1c1b6b9a1bc365481a0015c19a")
  message(FATAL_ERROR "Code-buffer correction requires the reviewed pinned allocator")
endif()
file(READ "${codebuffer_original}" codebuffer_source)
set(codebuffer_anchor [=[            uint8_t* base_end = base + COMMIT_COUNT * 4096;
            uint32_t size = COMMIT_COUNT * 4096;
            if (base_end >= right) {
                size = base_end - base;
                // whoops, we are almost run out of memory! Commit all instead
            }]=])
set(codebuffer_corrected_text [=[            /* Commit at most the remaining reserved bytes. The fault may
             * be unaligned, even when the reservation is chunk-aligned. */
            SIZE_T size = (SIZE_T)(right - base);
            if (size > COMMIT_COUNT * 4096) {
                size = COMMIT_COUNT * 4096;
            }]=])
string(FIND "${codebuffer_source}" "${codebuffer_anchor}" codebuffer_first)
string(FIND "${codebuffer_source}" "${codebuffer_anchor}" codebuffer_last REVERSE)
if(codebuffer_first LESS 0 OR NOT codebuffer_first EQUAL codebuffer_last)
  message(FATAL_ERROR "Expected one reviewed Windows lazy-commit anchor")
endif()
string(REPLACE "${codebuffer_anchor}" "${codebuffer_corrected_text}" codebuffer_source "${codebuffer_source}")
set(codebuffer_corrected "${CMAKE_CURRENT_BINARY_DIR}/codebuffer-private/translate-all.c")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/codebuffer-private")
file(WRITE "${codebuffer_corrected}" "${codebuffer_source}")
get_target_property(x86_sources x86_64-softmmu SOURCES)
list(LENGTH x86_sources original_source_count)
get_target_property(x86_source_directory x86_64-softmmu SOURCE_DIR)
foreach(candidate IN LISTS x86_sources)
  if(candidate MATCHES "(^|/)translate-all[.]c$")
    get_filename_component(candidate_absolute "${candidate}" ABSOLUTE BASE_DIR "${x86_source_directory}")
    if(NOT candidate_absolute STREQUAL codebuffer_original)
      message(FATAL_ERROR "Compiled translation allocator is outside the pinned source")
    endif()
    list(REMOVE_ITEM x86_sources "${candidate}")
  endif()
endforeach()
list(LENGTH x86_sources remaining_source_count)
math(EXPR removed_source_count "${original_source_count}-${remaining_source_count}")
if(NOT removed_source_count EQUAL 1)
  message(FATAL_ERROR "Expected one actual compiled translation allocator to replace")
endif()
set_property(TARGET x86_64-softmmu PROPERTY SOURCES "${x86_sources}")
target_sources(x86_64-softmmu PRIVATE "${codebuffer_corrected}")
# The copied C file retains its original same-directory private includes.
target_include_directories(x86_64-softmmu PRIVATE "${XLLE_UNICORN_SOURCE}/qemu/accel/tcg")
file(SHA256 "${codebuffer_corrected}" codebuffer_corrected_sha)
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" codebuffer_patch_sha)
