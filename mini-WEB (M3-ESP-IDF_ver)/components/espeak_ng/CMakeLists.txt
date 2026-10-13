set(ESPEAK_SOURCES
    "src/common.c"
    "src/mnemonics.c"
    "src/error.c"
    "src/ieee80.c"
    "src/compiledata.c"
    "src/compiledict.c"
    "src/dictionary.c"
    "src/encoding.c"
    "src/intonation.c"
    "src/langopts.c"
    "src/numbers.c"
    "src/phoneme.c"
    "src/phonemelist.c"
    "src/readclause.c"
    "src/setlengths.c"
    "src/soundicon.c"
    "src/spect.c"
    "src/ssml.c"
    "src/synthdata.c"
    "src/synthesize.c"
    "src/tr_languages.c"
    "src/translate.c"
    "src/translateword.c"
    "src/voices.c"
    "src/wavegen.c"
    "src/speech.c"
    "src/espeak_api.c"
    "ucd/case.c"
    "ucd/categories.c"
    "ucd/ctype.c"
    "ucd/proplist.c"
    "ucd/scripts.c"
    "ucd/tostring.c"
)

idf_component_register(
    SRCS ${ESPEAK_SOURCES}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src" "include/compat"
    LDFRAGMENTS "linker.lf"
)

if(NOT CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
    message(FATAL_ERROR "M3 eSpeak buffers require PSRAM BSS; run idf.py reconfigure")
endif()

target_compile_definitions(
    ${COMPONENT_LIB}
    PRIVATE
        LIBESPEAK_NG_EXPORT=1
        PATH_ESPEAK_DATA="/tts/espeak-ng-data"
)
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        -fwrapv
        # ESP-IDF uses -Werror=all; generic -Wno-error cannot override it.
        # Keep bounded-format diagnostics visible in vendored dictionary tools.
        -Wno-error=format-truncation
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-missing-field-initializers
)

