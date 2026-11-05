# tests/sim_libm.cmake - the code that steers jets inside sim_update (the CPU hands: cheat.c, testpilot.c) uses
# fmath's own functions, not the system's libm, whose atan2f and acosf differ between systems and would make
# the matches differ (network play, the audit's digest). Run with cmake -DSRC=<source dir> -P.
foreach(f cheat.c testpilot.c)
  file(STRINGS ${SRC}/src/${f} lines REGEX "[^_a-z](atan2f|acosf|asinf|atanf|sinf|cosf|tanf|expf|logf|powf)\\(")
  foreach(l ${lines})
    if(NOT l MATCHES "^ *(/\\*|\\*)")
      message(SEND_ERROR "${f}: libm in the sim's hands: ${l}")
    endif()
  endforeach()
endforeach()
