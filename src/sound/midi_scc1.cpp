/*
 * 86Box Roland SCC-1 MIDI backend.
 *
 * Uses Nuked-SC55's CM-300/SCC-1 hardware model (GPL-2.0-or-later).
 * The independent MPU-401 owns the ISA interface; this device owns the
 * H8/532 synthesizer and PCM sound generator.
 */
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>

#include "nuked_sc55/backend/emu.h"
#include "nuked_sc55/backend/standard_romsets.h"

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/midi.h>
#include <86box/rom.h>
#include <86box/sound.h>

void al_set_midi(int freq, int buf_size);
}

static constexpr int SCC1_RATE = 64000;
static constexpr int SCC1_FRAMES = SCC1_RATE / RENDER_RATE;

static const struct {
    const char *path;
    RomLocation location;
    size_t size;
} scc1_roms[] = {
    { "sound/roland/scc1/cm300_rom1.bin", RomLocation::ROM1, 0x8000 },
    { "sound/roland/scc1/cm300_rom2.bin", RomLocation::ROM2, 0x40000 },
    { "sound/roland/scc1/cm300_waverom1.bin", RomLocation::WAVEROM1, 0x100000 },
    { "sound/roland/scc1/cm300_waverom2.bin", RomLocation::WAVEROM2, 0x100000 },
    { "sound/roland/scc1/cm300_waverom3.bin", RomLocation::WAVEROM3, 0x100000 }
};

struct scc1_t {
    RomsetInfo roms;
    Emulator emu;
    float buffer_float[SCC1_FRAMES * 2]{};
    int16_t buffer_int[SCC1_FRAMES * 2]{};
    int frames = 0;
    uint64_t samples = 0;
    bool failed = false;
};

static scc1_t *scc1;

static bool
scc1_load_roms(RomsetInfo &roms)
{
    for (const auto &rom : scc1_roms) {
        std::unique_ptr<FILE, int (*)(FILE *)> file(rom_fopen(rom.path, (char *) "rb"), fclose);
        if (!file || fseek(file.get(), 0, SEEK_END) || ftell(file.get()) != (long) rom.size)
            return false;
        auto &data = roms.rom_data[(size_t) rom.location];
        data.resize(rom.size);
        rewind(file.get());
        if (fread(data.data(), 1, data.size(), file.get()) != data.size())
            return false;
    }
    return true;
}

static bool
scc1_supported_roms(RomsetInfo &roms)
{
    SHA256_Digest hashes[ROMLOCATION_COUNT]{};
    for (const auto &rom : scc1_roms) {
        const auto index = (size_t) rom.location;
        if (!SHA256_HashBytes(roms.rom_data[index], hashes[index]))
            return false;
    }
    for (const auto &set : GetStandardRomsetDefinitions()) {
        if (set.romset != Romset::CM300)
            continue;
        bool matches = true;
        for (const auto &rom : scc1_roms)
            matches &= hashes[(size_t) rom.location] == set.GetHash(rom.location);
        if (matches)
            return true;
    }
    return false;
}

static int
scc1_available(void)
{
    try {
        RomsetInfo roms;
        return scc1_load_roms(roms) && scc1_supported_roms(roms);
    } catch (const std::bad_alloc &) {
        return 0;
    }
}

static void
scc1_sample(void *priv, const AudioFrame<int32_t> &sample)
{
    auto *dev = static_cast<scc1_t *>(priv);
    ++dev->samples;
    const int offset = dev->frames * 2;
    if (sound_is_float) {
        AudioFrame<float> out;
        Normalize(sample, out);
        dev->buffer_float[offset] = out.left;
        dev->buffer_float[offset + 1] = out.right;
    } else {
        AudioFrame<int16_t> out;
        Normalize(sample, out);
        dev->buffer_int[offset] = out.left;
        dev->buffer_int[offset + 1] = out.right;
    }
    if (++dev->frames == SCC1_FRAMES) {
        givealbuffer_midi(sound_is_float ? static_cast<void *>(dev->buffer_float)
                                       : static_cast<void *>(dev->buffer_int), SCC1_FRAMES * 2);
        dev->frames = 0;
    }
}

static void
scc1_poll(void)
{
    if (scc1->failed)
        return;
    try {
        /* Pace by PCM samples, as the MCU clock differs between models. */
        const auto end = scc1->samples + SCC1_FRAMES;
        const auto deadline = scc1->emu.GetMCU().cycles + 480000;
        while (scc1->samples < end) {
            if (scc1->emu.GetMCU().cycles >= deadline)
                throw std::runtime_error("PCM output stalled");
            scc1->emu.Step();
        }
    } catch (const std::exception &error) {
        pclog("SCC-1: synthesizer stopped: %s\n", error.what());
        scc1->failed = true;
    }
}

static int
scc1_write(uint8_t byte)
{
    if (scc1->failed)
        return 1;
    auto &mcu = scc1->emu.GetMCU();
    /* Preserve unread bytes when a guest overruns the synthesizer's UART. */
    if ((mcu.uart_write_ptr + 1) % uart_buffer_size != mcu.uart_read_ptr)
        scc1->emu.PostMIDI(byte);
    return 1;
}

static void *
scc1_start(std::unique_ptr<scc1_t> dev)
{
    /* LoadRomset unscrambles the physical PCM ROM address/data wiring. */
    if (!LoadRomset(dev->roms, nullptr) || !dev->emu.Init({}) ||
        !dev->emu.LoadRoms(Romset::CM300, dev->roms))
        return nullptr;

    auto *midi = static_cast<midi_device_t *>(calloc(1, sizeof(midi_device_t)));
    if (!midi)
        return nullptr;
    dev->emu.Reset();
    dev->emu.SetSampleCallback(scc1_sample, dev.get());
    al_set_midi(SCC1_RATE, SCC1_FRAMES * 2 * (sound_is_float ? sizeof(float) : sizeof(int16_t)));
    midi->write = scc1_write;
    midi->poll = scc1_poll;
    scc1 = dev.release();
    midi_out_init(midi);
    return scc1;
}

static void *
scc1_init(const device_t *)
{
    if (!machine_has_bus(machine, MACHINE_BUS_ISA))
        return nullptr;

    try {
        auto dev = std::make_unique<scc1_t>();
        if (!scc1_load_roms(dev->roms) || !scc1_supported_roms(dev->roms)) {
            pclog("SCC-1: missing, unreadable or unsupported ROM set.\n");
            return nullptr;
        }
        return scc1_start(std::move(dev));
    } catch (const std::exception &error) {
        pclog("SCC-1: initialization failed: %s\n", error.what());
        return nullptr;
    }
}

static void
scc1_close(void *priv)
{
    delete static_cast<scc1_t *>(priv);
    scc1 = nullptr;
}

extern const device_t scc1_device = {
    .name = "Roland SCC-1",
    .internal_name = "scc1",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = scc1_init,
    .close = scc1_close,
    .reset = nullptr,
    .available = scc1_available,
    .speed_changed = nullptr,
    .force_redraw = nullptr,
    .config = nullptr
};
