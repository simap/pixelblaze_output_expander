# Fails the build if the flashed binary contains the frame magic "UPXL" at
# a byte boundary. A parser resyncing inside an update frame that carries
# this image would otherwise find a frame start there. The compiler can emit
# the four bytes as one literal pool word, so the source alone cannot
# guarantee this. Run with -DBIN=<file>.

file(READ "${BIN}" hex HEX)
set(magic "5550584c")
set(pos 0)
while(TRUE)
    string(SUBSTRING "${hex}" ${pos} -1 rest)
    string(FIND "${rest}" "${magic}" idx)
    if(idx EQUAL -1)
        break()
    endif()
    math(EXPR at "${pos} + ${idx}")
    math(EXPR parity "${at} % 2")
    if(parity EQUAL 0)
        math(EXPR byte "${at} / 2")
        message(FATAL_ERROR "${BIN} contains the frame magic UPXL at byte offset ${byte}")
    endif()
    math(EXPR pos "${at} + 1")
endwhile()
