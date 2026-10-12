# Vendored backend only; no standalone frontends or host MIDI dependencies.
set(NUKED_ENABLE_DECODER2 1)
set(NUKED_SOURCE "jcmoyer/Nuked-SC55 b75cfc2e2ccd6b4f4a61c872acc61e7fe3bf24f4")
configure_file(backend/config.h.in backend/config.h @ONLY)
add_library(nuked_sc55 STATIC
    backend/config.cpp
    backend/diagnostics.cpp
    backend/emu.cpp
    backend/file_hashing.cpp
    backend/file_io.cpp
    backend/lcd.cpp
    backend/mcu_interrupt.cpp
    backend/mcu_opcodes.cpp
    backend/mcu_timer.cpp
    backend/mcu.cpp
    backend/pcm.cpp
    backend/rom_io.cpp
    backend/rom.cpp
    backend/standard_romsets.cpp
    backend/submcu.cpp

    backend/sha/sha-private.h
    backend/sha/sha.h
    backend/sha/sha224-256.c
    backend/decoder2/address_modes.cpp
    backend/decoder2/cache.cpp
    backend/decoder2/disassemble.cpp
    backend/decoder2/disassemble_handlers.cpp
    backend/decoder2/disassemblers_address.cpp
    backend/decoder2/disassemblers_imm16.cpp
    backend/decoder2/disassemblers_imm8.cpp
    backend/decoder2/disassemblers_rn.cpp
    backend/decoder2/disassemblers_top.cpp
    backend/decoder2/dispatch.cpp
    backend/decoder2/dispatchers_aa.cpp
    backend/decoder2/dispatchers_arn.cpp
    backend/decoder2/dispatchers_d8d16_rn.cpp
    backend/decoder2/dispatchers_imm16.cpp
    backend/decoder2/dispatchers_postinc_rn.cpp
    backend/decoder2/dispatchers_predec_rn.cpp
    backend/decoder2/dispatchers_rn.cpp
    backend/decoder2/dispatchers_top.cpp
    backend/decoder2/instruction_handlers.cpp
)
target_include_directories(nuked_sc55 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/backend
    ${CMAKE_CURRENT_BINARY_DIR}/backend)
target_compile_features(nuked_sc55 PRIVATE cxx_std_23)
