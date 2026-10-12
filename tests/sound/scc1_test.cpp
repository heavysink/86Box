#include <gtest/gtest.h>
#include <cstring>
#include <cmath>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "../../src/sound/midi_scc1.cpp"

extern "C" {
#include <86box/timer.h>
#include <86box/snd_mpu401.h>
}

namespace {
std::map<std::string, std::vector<uint8_t>> rom_files;
midi_device_t *output;
bool isa = true;
int output_rate, output_bytes, audio_buffers;
bool capture_audio;
std::vector<float> captured_audio;
const device_t *added_device;
int configured_base = 0x330, configured_irq = 2;
uint16_t installed_base;
uint8_t (*io_read)(uint16_t, void *);
void (*io_write)(uint16_t, uint8_t, void *);
void *io_priv;
}

extern "C" {
int machine, sound_is_float = 1, midi_output_device_current;

FILE *rom_fopen(const char *path, char *)
{
    auto it = rom_files.find(path);
    if (it == rom_files.end())
        return nullptr;
    FILE *file = tmpfile();
    if (!file)
        return nullptr;
    fwrite(it->second.data(), 1, it->second.size(), file);
    rewind(file);
    return file;
}
int machine_has_bus(int, uintptr_t bus) { return isa ? !!(bus & MACHINE_BUS_ISA) : !!(bus & MACHINE_BUS_MCA); }
void pclog(const char *, ...) {}
void al_set_midi(int rate, int bytes) { output_rate = rate; output_bytes = bytes; }
void midi_out_init(midi_device_t *device) { output = device; }
void givealbuffer_common(const void *buffer, uint8_t src, int size)
{
    EXPECT_EQ(src, I_MIDI);
    EXPECT_EQ(size, 1280);
    ++audio_buffers;
    if (capture_audio && sound_is_float) {
        const auto *samples = static_cast<const float *>(buffer);
        captured_audio.insert(captured_audio.end(), samples, samples + size);
    }
}
const device_t *midi_out_device_getdevice(int card) { return card ? &scc1_device : nullptr; }
void *device_add(const device_t *device) { added_device = device; return nullptr; }
int device_get_config_int(const char *name) { return !strcmp(name, "irq") ? configured_irq : 0; }
int device_get_config_hex16(const char *) { return configured_base; }
uint64_t TIMER_USEC = 1ULL << 32, tsc;
void fatal(const char *, ...) { abort(); }
void midi_clear_buffer(void) {}
void midi_reset(void) {}
void midi_raw_out_byte(uint8_t byte) { if (output) output->write(byte); }
void midi_raw_out_rt_byte(uint8_t byte) { midi_raw_out_byte(byte); }
void midi_in_handler(int, void (*)(void *, uint8_t *, uint32_t),
                     int (*)(void *, uint8_t *, uint32_t, int), int (*)(void *), void *) {}
void picint_common(uint16_t, int, int, uint8_t *) {}
void timer_enable(pc_timer_t *timer) { timer->flags |= TIMER_ENABLED; }
void timer_disable(pc_timer_t *timer) { timer->flags &= ~TIMER_ENABLED; }
void timer_add(pc_timer_t *timer, void (*callback)(void *), void *priv, int)
{
    timer->callback = callback;
    timer->priv = priv;
}
uint8_t mca_add(uint8_t (*)(uint16_t, void *), void (*)(uint16_t, uint8_t, void *),
                uint8_t (*)(void *), void (*)(void *), void *) { return 0; }
void io_sethandler(uint16_t base, uint16_t size,
                   uint8_t (*read)(uint16_t, void *), uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
                   void (*write)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *),
                   void (*)(uint16_t, uint32_t, void *), void *priv)
{
    if (size == 2) {
        installed_base = base;
        io_read = read;
        io_write = write;
        io_priv = priv;
    }
}
void io_removehandler(uint16_t, uint16_t,
                      uint8_t (*)(uint16_t, void *), uint16_t (*)(uint16_t, void *), uint32_t (*)(uint16_t, void *),
                      void (*)(uint16_t, uint8_t, void *), void (*)(uint16_t, uint16_t, void *),
                      void (*)(uint16_t, uint32_t, void *), void *) {}

}

class SCC1 : public testing::Test {
protected:
    void SetUp() override
    {
        isa = true;
        sound_is_float = 1;
        output = nullptr;
        audio_buffers = 0;
        capture_audio = false;
        captured_audio.clear();
        added_device = nullptr;
        mpu401_standalone_enable = 0;
        midi_output_device_current = 1;
        configured_base = 0x330;
        configured_irq = 2;
        for (const auto &rom : scc1_roms)
            rom_files[rom.path].assign(rom.size, 0);
        // Synthetic H8 firmware: reset at 0x100, then BRA to itself.
        auto &mcu = rom_files[scc1_roms[0].path];
        mcu[2] = 1;
        mcu[0x100] = 0x20;
        mcu[0x101] = 0xfe;
    }
    void *StartSynthetic()
    {
        auto dev = std::make_unique<scc1_t>();
        if (!scc1_load_roms(dev->roms))
            return nullptr;
        return scc1_start(std::move(dev));
    }
    void TearDown() override
    {
        if (scc1)
            scc1_device.close(scc1);
        free(output);
        output = nullptr;
        rom_files.clear();
    }
};

TEST_F(SCC1, RequiresEveryRomAtExactSize)
{
    RomsetInfo roms;
    ASSERT_TRUE(scc1_load_roms(roms));
    for (const auto &rom : scc1_roms) {
        auto &data = rom_files[rom.path];
        data.pop_back();
        EXPECT_FALSE(scc1_load_roms(roms));
        EXPECT_EQ(scc1_device.init(&scc1_device), nullptr);
        data.resize(rom.size + 1);
        EXPECT_FALSE(scc1_load_roms(roms));
        data.resize(rom.size);
    }
    rom_files.erase(scc1_roms[0].path);
    EXPECT_FALSE(scc1_device.available());
}

TEST_F(SCC1, RejectsMachinesWithoutISA)
{
    isa = false;
    EXPECT_EQ(scc1_device.init(&scc1_device), nullptr);
    mpu401_device_add();
    EXPECT_EQ(added_device, nullptr);
}

TEST_F(SCC1, StartsAndReopensBothAudioFormats)
{
    for (int floating : {0, 1}) {
        sound_is_float = floating;
        ASSERT_NE(StartSynthetic(), nullptr);
        ASSERT_NE(output, nullptr);
        EXPECT_EQ(output_rate, 64000);
        EXPECT_EQ(output_bytes, 1280 * (floating ? 4 : 2));
        // 29 slots plus one overhead slot, with 2x oversampling: 64 kHz.
        PCM_Write(scc1->emu.GetPCM(), 0x3d, 28);
        PCM_Write(scc1->emu.GetPCM(), 0x3c, 0x40);
        auto cycles = scc1->emu.GetMCU().cycles;
        audio_buffers = 0;
        output->poll();
        EXPECT_LE(scc1->emu.GetMCU().cycles - cycles, 240000u);
        EXPECT_GE(scc1->emu.GetMCU().cycles - cycles, 239000u);
        EXPECT_EQ(scc1->samples, 640u);
        EXPECT_EQ(audio_buffers, 1);
        scc1_device.close(scc1);
        free(output);
        output = nullptr;
    }
}

TEST_F(SCC1, PassesRawMidiAndDoesNotOverwriteFullUART)
{
    ASSERT_NE(StartSynthetic(), nullptr);
    const uint8_t message[] = {0x90, 60, 100, 61, 100, 0xf8, 0xf0, 0x41, 0xf7};
    auto &mcu = scc1->emu.GetMCU();
    for (auto byte : message)
        EXPECT_EQ(output->write(byte), 1);
    EXPECT_EQ(mcu.uart_write_ptr, sizeof(message));
    EXPECT_EQ(memcmp(mcu.uart_buffer, message, sizeof(message)), 0);
    for (unsigned i = sizeof(message); i < uart_buffer_size - 1; ++i)
        output->write(0x55);
    output->write(0xaa);
    EXPECT_EQ(mcu.uart_write_ptr, uart_buffer_size - 1);
    EXPECT_EQ(mcu.uart_buffer[0], 0x90);
}

TEST_F(SCC1, ForcesIndependentMPUAndPreservesOtherBackends)
{
    mpu401_device_add();
    EXPECT_EQ(added_device, &scc1_mpu401_device);
    EXPECT_EQ(mpu401_standalone_enable, 1);
    midi_output_device_current = 0;
    added_device = nullptr;
    mpu401_standalone_enable = 0;
    mpu401_device_add();
    EXPECT_EQ(added_device, nullptr);
    mpu401_standalone_enable = 1;
    mpu401_device_add();
    EXPECT_EQ(added_device, &mpu401_device);
    isa = false;
    mpu401_device_add();
    EXPECT_EQ(added_device, &mpu401_mca_device);
}

TEST_F(SCC1, ExposesOnlyDocumentedJumpers)
{
    const auto *config = scc1_mpu401_device.config;
    EXPECT_EQ(config[0].default_int, 0x330);
    EXPECT_EQ(config[1].default_int, 2);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(config[0].selection[i].value, 0x330 + 2 * i);
    EXPECT_STREQ(config[0].selection[4].description, "");
    for (int i = 0; i < 6; ++i)
        EXPECT_EQ(config[1].selection[i].value, 2 + i);
    EXPECT_STREQ(config[1].selection[6].description, "");
}

TEST_F(SCC1, EveryJumperCombinationSupportsIntelligentAndUARTCommands)
{
    ASSERT_NE(StartSynthetic(), nullptr);
    for (configured_base = 0x330; configured_base <= 0x336; configured_base += 2) {
        for (configured_irq = 2; configured_irq <= 7; ++configured_irq) {
            auto *mpu = static_cast<mpu_t *>(scc1_mpu401_device.init(&scc1_mpu401_device));
            ASSERT_NE(mpu, nullptr);
            EXPECT_EQ(installed_base, configured_base);
            EXPECT_EQ(mpu->irq, configured_irq);
            EXPECT_EQ(mpu->mode, M_INTELLIGENT);
            io_write(installed_base + 1, 0xac, io_priv);
            EXPECT_EQ(io_read(installed_base, io_priv), MSG_MPU_ACK);
            EXPECT_EQ(io_read(installed_base, io_priv), MPU401_VERSION);
            io_write(installed_base + 1, 0x3f, io_priv);
            EXPECT_EQ(io_read(installed_base, io_priv), MSG_MPU_ACK);
            EXPECT_EQ(mpu->mode, M_UART);
            auto &mcu = scc1->emu.GetMCU();
            auto start = mcu.uart_write_ptr;
            io_write(installed_base, 0x90, io_priv);
            io_write(installed_base, 60, io_priv);
            io_write(installed_base, 100, io_priv);
            EXPECT_EQ(mcu.uart_write_ptr, (start + 3) % uart_buffer_size);
            EXPECT_EQ(mcu.uart_buffer[start], 0x90);
            scc1_mpu401_device.close(mpu);
        }
    }
}

TEST_F(SCC1, InvalidJumpersFallBackToFactoryDefaults)
{
    for (int base : {0, 0x331, 0x338, 0x220}) {
        configured_base = base;
        configured_irq = 9;
        auto *mpu = static_cast<mpu_t *>(scc1_mpu401_device.init(&scc1_mpu401_device));
        ASSERT_NE(mpu, nullptr);
        EXPECT_EQ(installed_base, 0x330);
        EXPECT_EQ(mpu->irq, 2);
        scc1_mpu401_device.close(mpu);
    }
}

TEST_F(SCC1, RejectsUnknownFirmwareBeforeExecutingIt)
{
    EXPECT_FALSE(scc1_device.available());
    EXPECT_EQ(scc1_device.init(&scc1_device), nullptr);
    EXPECT_EQ(output, nullptr);
}

TEST_F(SCC1, CoreErrorsStopSynthesizerWithoutExitingHost)
{
    auto &rom = rom_files[scc1_roms[0].path];
    rom[0x100] = 0xff;
    rom[0x101] = 0xff;
    ASSERT_NE(StartSynthetic(), nullptr);
    output->poll();
    EXPECT_TRUE(scc1->failed);
    auto cycles = scc1->emu.GetMCU().cycles;
    output->poll();
    EXPECT_EQ(scc1->emu.GetMCU().cycles, cycles);
}

TEST_F(SCC1, UnscramblesPhysicalSampleROMs)
{
    // Physical address bit 2 maps to linear bit 0; data bit 2 maps to bit 0.
    rom_files[scc1_roms[2].path][4] = 4;
    ASSERT_NE(StartSynthetic(), nullptr);
    EXPECT_EQ(scc1->emu.GetPCM().waverom1[1], 1);
    EXPECT_EQ(scc1->emu.GetPCM().waverom1[4], 0);
}

TEST_F(SCC1, PacesBySamplesWith24VoiceFirmware)
{
    ASSERT_NE(StartSynthetic(), nullptr);
    PCM_Write(scc1->emu.GetPCM(), 0x3d, 23);
    PCM_Write(scc1->emu.GetPCM(), 0x3c, 0x40);
    for (int i = 0; i < 100; ++i)
        output->poll();
    EXPECT_EQ(scc1->samples, 64000u);
    EXPECT_EQ(audio_buffers, 100);
    EXPECT_FALSE(scc1->failed);
}

TEST_F(SCC1, ExternalRomPlayback)
{
    const char *directory = getenv("SCC1_ROM_DIR");
    if (!directory)
        GTEST_SKIP() << "Set SCC1_ROM_DIR to a complete, named SCC-1 ROM set";
    for (const auto &rom : scc1_roms) {
        const auto path = std::filesystem::path(directory) / std::filesystem::path(rom.path).filename();
        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(file.is_open()) << path;
        rom_files[rom.path] = {std::istreambuf_iterator<char>(file), {}};
    }
    ASSERT_TRUE(scc1_device.available());
    ASSERT_NE(scc1_device.init(&scc1_device), nullptr);
    std::unique_ptr<mpu_t, void (*)(void *)> mpu(
        static_cast<mpu_t *>(scc1_mpu401_device.init(&scc1_mpu401_device)), scc1_mpu401_device.close);
    ASSERT_NE(mpu, nullptr);
    io_write(installed_base + 1, 0x3f, io_priv);
    ASSERT_EQ(io_read(installed_base, io_priv), MSG_MPU_ACK);

    const auto run = [](int polls) {
        for (int i = 0; i < polls && !scc1->failed; ++i)
            output->poll();
    };
    const auto send = [](std::initializer_list<uint8_t> message) {
        for (auto byte : message)
            io_write(installed_base, byte, io_priv);
    };
    const auto measure = [&]() {
        captured_audio.clear();
        capture_audio = true;
        run(100);
        capture_audio = false;
        if (captured_audio.empty())
            return 0.0;
        // Measure AC energy: the emulated DAC has a constant DC offset at idle.
        double mean[2]{};
        for (size_t i = 0; i < captured_audio.size(); ++i)
            mean[i & 1] += captured_audio[i];
        for (auto &channel : mean)
            channel /= captured_audio.size() / 2;
        double energy = 0;
        for (size_t i = 0; i < captured_audio.size(); ++i) {
            const double sample = captured_audio[i] - mean[i & 1];
            energy += sample * sample;
        }
        return sqrt(energy / captured_audio.size());
    };
    run(300);
    ASSERT_FALSE(scc1->failed);
    const double quiet = measure();
    // A sustained organ note makes the reset's audible effect measurable.
    send({0xc0, 19, 0x90, 60, 100});
    run(10);
    const double note = measure();
    EXPECT_GT(note, quiet * 4 + 0.0001);
    EXPECT_EQ(captured_audio.size(), 128000u);
    send({0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7});
    run(200);
    const double reset = measure();
    EXPECT_LT(reset, note * 0.1);
    EXPECT_FALSE(scc1->failed);
    EXPECT_EQ(scc1->emu.GetMCU().uart_read_ptr, scc1->emu.GetMCU().uart_write_ptr);
    printf("SCC-1 ROM playback AC RMS: quiet=%.8f note=%.8f reset=%.8f\n", quiet, note, reset);
}
