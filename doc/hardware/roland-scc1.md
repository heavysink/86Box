# Roland SCC-1

Select **Roland SCC-1** as the MIDI output device on an ISA machine. This
uses the ROM-backed Nuked-SC55 CM-300/SCC-1 core, independently of the
Sound Canvas CLAP backend. It also supports a complete SCC-1A ROM set.

Selecting SCC-1 checks and locks **Standalone MPU-401**. Configure that
interface for one of `330h`, `332h`, `334h`, or `336h`, and IRQ 2, 3, 4, 5,
6, or 7. Defaults are `330h` and IRQ 2. The existing intelligent MPU-401
implementation also provides UART mode. IRQ 2 is routed to IRQ 9 on AT
machines by the existing PIC implementation. Avoid assigning another
sound card's MPU-401 to the same ports.

The settings are stored under `[Roland SCC-1 MPU-401]` using `base`, `irq`,
and `receive_input`. Selecting `midi_device = scc1` also enables this
interface when loading a hand-written configuration. Invalid SCC-1 jumper
values fall back to the factory defaults. MCA-only machines cannot use the
SCC-1.

## ROM installation

Place these files under `sound/roland/scc1/` in an 86Box ROM search path:

| Filename | Size | Contents |
| --- | ---: | --- |
| `cm300_rom1.bin` | 32,768 bytes | H8/532 internal MCU ROM |
| `cm300_rom2.bin` | 262,144 bytes | External program ROM |
| `cm300_waverom1.bin` | 1,048,576 bytes | R15279806, GS.A |
| `cm300_waverom2.bin` | 1,048,576 bytes | R15279807, GS.B |
| `cm300_waverom3.bin` | 1,048,576 bytes | R15279808, GS.C |

Supported program/MCU pairs:

| Model | Internal MCU | External program |
| --- | --- | --- |
| SCC-1 v1.10 | R15199774 | R15279809 |
| SCC-1 v1.20 | R15199774 | R15279812 |
| SCC-1A | R00128523 | R00128567 |

The device is offered only for an entire recognized set. Sizes and SHA-256
hashes must match the upstream CM-300/SCC-1 definitions. Sample ROMs must
remain in their original physical dump format; the loader decodes their
address/data wiring. ROM images are not distributed with the code.

Internal MCU SHA-256 hashes:

- R15199774: `72ed35481efbf25b3c492b83183655d17a3b266ecb30ffbc6dc977e6a8d261b2`
- R00128523: `9ec66abb5231b6c6f46f48b33d5412703041037d69a6803626ac402f25552af2`

### Supplied archive findings

`Roland SCC1-SCC1A_Roms.zip` contains all three supported external program
ROMs and all three sample ROMs, but neither internal MCU ROM.
`Roland SCC1 Control Rom.zip` contains the SCC-1A external program ROM,
identical to R00128567; it is not an internal MCU dump.
`Roland SCC1-Card.zip` contains a duplicate of the GS.A sample ROM.

`cm300.zip` supplies the same SCC-1 program/sample ROMs plus the 4 KB
R15239182. That 4 KB image cannot replace the 32 KB H8 ROM.
`sc55.zip` contains R15199778 and a complete SC-55 v1.21 set. R15199778
boots and produces note audio with that SC-55 set, but failed to boot with
each of the three supplied SCC-1/SCC-1A program ROMs in the CM-300 model.
It is therefore not accepted as an SCC-1 replacement.

The later `roland_SCC-1A(mcu).rar` contains the correct R00128523 internal
MCU ROM. `roland_scc-1a Rxxx.rar` contains a complete SCC-1A set: R00128523,
R00128567 and all three GS sample ROMs. Every file matches the upstream
SCC-1A hashes. Neither new RAR contains R15199774. `Roland SCC55_IC23.zip`
contains the SC-55 external program ROM R15209363, not an SCC-1 MCU.

`scc1.rar` is the utility disk, including CHKSCC, MPUPLAY, SCMODE and MIDI
demos. These utilities are not ROMs and are not required by the backend.

## Implementation and validation

The core is vendored from jcmoyer/Nuked-SC55 at revision
`b75cfc2e2ccd6b4f4a61c872acc61e7fe3bf24f4`, under GPL-2.0-or-later.
Only the core and SCC-1 adapter require C++23. No plugin installation,
network access during builds, or additional runtime library is needed.

The MIDI adapter passes raw bytes, including running status, SysEx and
realtime messages, into the MCU UART. Rendering runs on the emulator
thread and is paced by 64 kHz PCM output, using the existing host MIDI
audio stream. A full UART preserves pending bytes and drops new bytes.
Unsupported core operations stop the synthesizer and log an error instead
of terminating the host application.

Build and run the focused tests:

```sh
cmake --build build --target scc1_tests 86Box
ctest --test-dir build -R '^SCC1\.' --output-on-failure
```

Tests use synthetic H8 firmware without copyrighted ROMs. They cover ROM
size and identity rejection, PCM ROM wiring, both audio formats, audio
cadence, raw MIDI, UART overflow, core-fault containment, ISA eligibility,
mandatory independent MPU selection, all 24 jumper combinations,
intelligent-mode version queries, UART note delivery and invalid jumper
fallbacks. The full application was also built on Linux.

An optional real-ROM test exercises the actual backend, independent MPU-401
UART, H8 firmware and PCM output. `SCC1_ROM_DIR` names a directory containing
the five `cm300_*.bin` files listed above; without it this test is skipped.

```sh
SCC1_ROM_DIR=/path/to/roms/sound/roland/scc1 \
    ctest --test-dir build -R '^SCC1\.' --output-on-failure
```

The supplied SCC-1A set passed this test on 2026-10-12. It booted, consumed a
program change and note-on through the independent MPU-401, rendered stereo
audio at 64 kHz, and returned to silence after GS reset. Measured AC RMS was
0.00000000 before the note, 0.03012191 during a sustained organ note, and
0.00000000 after reset. AC measurements exclude the core's constant idle DAC
offset. All 13 focused tests passed with the external ROM set supplied.

Original SCC-1 firmware and DOS utility compatibility remain unverified.
R15199774 is still absent from the supplied archives. These are device-level
tests with mocked host audio and I/O dispatch, not DOS guest or listening tests.
The upstream core does not deliver synthesizer UART transmit data back to
the guest, so synthesizer SysEx readback is not implemented. The standalone
MPU's external MIDI input support remains available.

Hardware reference: Roland SCC-1 Owner's Manual, appendix page 22:
https://theretroweb.com/expansioncard/documentation/roland-scc-1-6784d7e434969197043274.pdf
