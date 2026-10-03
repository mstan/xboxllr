# The block scheduler must know even the very first TB's instruction count.
# Upstream suppresses the generation callback until a previous TB exists; that
# forced deferred bootstrap charges and allowed a cold self-loop to run without
# a bounded clock. Report the new TB with a null predecessor in that case.
# Derive a pinned build copy and retain the upstream license verbatim.
file(READ "${unicorn_SOURCE_DIR}/qemu/accel/tcg/cpu-exec.c" _xlle_exec_text)
string(REPLACE "\r\n" "\n" _xlle_exec_text "${_xlle_exec_text}")
string(SHA256 _xlle_exec_sha "${_xlle_exec_text}")
if(NOT _xlle_exec_sha STREQUAL "99db183f8b049c5530a90fc00aa28eb1c783188929053d2ecd4d62facfe80102")
  message(FATAL_ERROR "Unicorn execution loop changed: requalify first-TB accounting")
endif()
set(_xlle_old_first "        if (uc->last_tb) {\n            UC_TB_COPY(&cur_tb, tb);\n            UC_TB_COPY(&prev_tb, uc->last_tb);")
set(_xlle_new_first "        {\n            UC_TB_COPY(&cur_tb, tb);\n            if (uc->last_tb) { UC_TB_COPY(&prev_tb, uc->last_tb); }")
string(FIND "${_xlle_exec_text}" "${_xlle_old_first}" _xlle_first_position)
if(_xlle_first_position LESS 0)
  message(FATAL_ERROR "Unicorn first-TB callback anchor missing")
endif()
string(REPLACE "${_xlle_old_first}" "${_xlle_new_first}" _xlle_exec_text "${_xlle_exec_text}")
string(REPLACE "(uc, &cur_tb, &prev_tb, hook->user_data)"
  "(uc, &cur_tb, uc->last_tb ? &prev_tb : NULL, hook->user_data)"
  _xlle_exec_text "${_xlle_exec_text}")
set(_xlle_exec_derived "${CMAKE_CURRENT_BINARY_DIR}/generated/unicorn/cpu-exec.c")
file(WRITE "${_xlle_exec_derived}" "${_xlle_exec_text}")
