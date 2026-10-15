idf_component_register(SRCS "lapi.c" "lcode.c" "lctype.c" "ldebug.c" "ldo.c" "ldump.c" "lfunc.c" "lgc.c" "llex.c" "lmem.c" "lobject.c" "lopcodes.c" "lparser.c" "lstate.c" "lstring.c" "ltable.c" "ltm.c" "lundump.c" "lvm.c" "lzio.c" "lauxlib.c" "lbaselib.c" "ltablib.c" "lmathlib.c"
 INCLUDE_DIRS ".")
target_compile_definitions(${COMPONENT_LIB} PRIVATE LUAI_MAXCCALLS=60)
