// SPDX-License-Identifier: GPL-3.0-or-later
// Native presentation child. Animation choreography is the MIT reconstructed
// core; owned-child scanout handoff does not transfer firmware machine state.
#include "boot_renderer.h"
#include "boot_timeline.h"
#include "boot_handoff.h"
#include <algorithm>
#include <bcrypt.h>
#include <chrono>
#include <cstring>
#include <d3d11sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mmsystem.h>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <windows.h>
#include <xinput.h>

namespace {
constexpr size_t PACK_BYTES = 73120;
constexpr const char *PACK_SHA256 =
    "e97b240aa8f0d20ce70d8c9ee2e4f3a150ca75808ba01c412c487335a7807dca";
bool close_requested = false;
struct Aligned {
    void *data;
    size_t size;
    explicit Aligned(size_t bytes) : data(_aligned_malloc(bytes + 64, XB_ALIGN)), size(bytes) {
        if (!data)
            throw std::bad_alloc();
        std::memset(data, 0, bytes);
        std::memset((char *)data + bytes, 0xa7, 64);
    }
    ~Aligned() {
        _aligned_free(data);
    }
    bool guard() const {
        const unsigned char *p = (const unsigned char *)data + size;
        return std::all_of(p, p + 64, [](unsigned char v) { return v == 0xa7; });
    }
    Aligned(const Aligned &) = delete;
    Aligned &operator=(const Aligned &) = delete;
};
void check(HRESULT rc, const char *op) {
    if (FAILED(rc))
        throw std::runtime_error(std::string(op) + " failed: " + std::to_string((unsigned)rc));
}
void core_check(int rc, const char *op) {
    if (rc != XB_OK)
        throw std::runtime_error(std::string(op) + " failed: " + std::to_string(rc));
}
std::string sha256(const void *bytes, size_t size) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char out[32]{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA-256 unavailable");
    NTSTATUS rc = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (rc >= 0)
        rc = BCryptHashData(hash, (PUCHAR)bytes, (ULONG)size, 0);
    if (rc >= 0)
        rc = BCryptFinishHash(hash, out, sizeof(out), 0);
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (rc < 0)
        throw std::runtime_error("SHA-256 failed");
    std::ostringstream text;
    for (unsigned char b : out)
        text << std::hex << std::setw(2) << std::setfill('0') << (unsigned)b;
    return text.str();
}
std::vector<unsigned char> read_pack(const std::filesystem::path &path) {
    std::error_code ec;
    auto status = std::filesystem::symlink_status(path, ec);
    if (ec || !std::filesystem::is_regular_file(status))
        throw std::runtime_error("Startup pack must be a regular file");
    if (std::filesystem::file_size(path) != PACK_BYTES)
        throw std::runtime_error("Unsupported startup pack size");
    std::ifstream stream(path, std::ios::binary);
    std::vector<unsigned char> bytes(PACK_BYTES);
    stream.read((char *)bytes.data(), bytes.size());
    if (!stream || stream.peek() != std::ifstream::traits_type::eof() ||
        sha256(bytes.data(), bytes.size()) != PACK_SHA256)
        throw std::runtime_error("Unsupported or damaged startup pack; extract it from the "
                                 "admitted complete owned kernel");
    return bytes;
}
struct Audio {
    Audio() = default;
    Audio(const Audio &) = delete;
    Audio &operator=(const Audio &) = delete;
    HWAVEOUT output = nullptr;
    WAVEHDR header{};
    bool prepared = false;
    void prepare(std::vector<int16_t> &pcm) {
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = 192000;
        if (waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            throw std::runtime_error("Startup audio output unavailable");
        header.lpData = (LPSTR)pcm.data();
        header.dwBufferLength = (DWORD)(pcm.size() * sizeof(int16_t));
        if (waveOutPrepareHeader(output, &header, sizeof(header)) != MMSYSERR_NOERROR)
            throw std::runtime_error("Startup audio buffer unavailable");
        prepared = true;
    }
    void start() {
        if (waveOutWrite(output, &header, sizeof(header)) != MMSYSERR_NOERROR)
            throw std::runtime_error("Startup audio playback failed");
    }
    void finish() {
        if (!output)
            return;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!(header.dwFlags & WHDR_DONE) && std::chrono::steady_clock::now() < deadline)
            Sleep(5);
        if (!(header.dwFlags & WHDR_DONE))
            throw std::runtime_error("Startup audio did not reach its completion boundary");
    }
    ~Audio() {
        if (output) {
            waveOutReset(output);
            if (prepared)
                waveOutUnprepareHeader(output, &header, sizeof(header));
            waveOutClose(output);
        }
    }
};
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CLOSE) {
        close_requested = true;
        return 0;
    }
    if (message == WM_KEYDOWN && !(lp & (LPARAM(1) << 30)) &&
        (wp == VK_ESCAPE || wp == VK_RETURN || wp == VK_SPACE)) {
        close_requested = true;
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
struct Window {
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;
    HWND value = nullptr;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    explicit Window(bool headless) {
        if (headless)
            return;
        WNDCLASSW wc{};
        wc.lpfnWndProc = window_proc;
        wc.hInstance = instance;
        wc.lpszClassName = L"xboxllr-boot-stage";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Cannot register startup window");
        RECT size{0, 0, 640, 480};
        AdjustWindowRect(&size, WS_OVERLAPPEDWINDOW, FALSE);
        value = CreateWindowExW(0, wc.lpszClassName, L"Xbox", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, size.right - size.left, size.bottom - size.top,
                                nullptr, nullptr, instance, nullptr);
        if (!value)
            throw std::runtime_error("Cannot create startup window");
    }
    ~Window() {
        if (value)
            DestroyWindow(value);
    }
};
class Controllers {
    HMODULE module_ = nullptr;
    using GetState = DWORD(WINAPI *)(DWORD, XINPUT_STATE *);
    GetState get_ = nullptr;
    WORD previous_[4]{};
    bool sampled_[4]{};

  public:
    Controllers(const Controllers &) = delete;
    Controllers &operator=(const Controllers &) = delete;
    Controllers() {
        wchar_t system[MAX_PATH]{};
        UINT length = GetSystemDirectoryW(system, MAX_PATH);
        if (!length || length >= MAX_PATH)
            return;
        for (const wchar_t *name : {L"xinput1_4.dll", L"xinput9_1_0.dll"}) {
            std::filesystem::path path = std::filesystem::path(system) / name;
            module_ = LoadLibraryW(path.c_str());
            if (module_) {
                get_ = (GetState)GetProcAddress(module_, "XInputGetState");
                if (get_)
                    break;
                FreeLibrary(module_);
                module_ = nullptr;
            }
        }
    }
    ~Controllers() {
        if (module_)
            FreeLibrary(module_);
    }
    bool skip() {
        if (!get_)
            return false;
        bool pressed = false;
        for (DWORD port = 0; port < 4; ++port) {
            XINPUT_STATE state{};
            if (get_(port, &state) != ERROR_SUCCESS) {
                sampled_[port] = false;
                previous_[port] = 0;
                continue;
            }
            const WORD buttons = state.Gamepad.wButtons &
                (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK);
            // The launcher's held Play button must not immediately skip the
            // intro. Only a new press after this stage's first sample counts.
            pressed |= sampled_[port] && (buttons & ~previous_[port]) != 0;
            previous_[port] = buttons;
            sampled_[port] = true;
        }
        return pressed;
    }
};
void wav(const std::filesystem::path &path, const std::vector<int16_t> &samples) {
    if (std::filesystem::exists(path))
        throw std::runtime_error("Audio capture already exists");
    std::ofstream stream(path, std::ios::binary);
    uint32_t size = (uint32_t)samples.size() * 2, file_size = size + 36;
    uint32_t fmt_size = 16, rate = 48000, byte_rate = 192000;
    uint16_t format = 1, channels = 2, align = 4, bits = 16;
    stream.write("RIFF", 4);
    stream.write((char *)&file_size, 4);
    stream.write("WAVEfmt ", 8);
    stream.write((char *)&fmt_size, 4);
    stream.write((char *)&format, 2);
    stream.write((char *)&channels, 2);
    stream.write((char *)&rate, 4);
    stream.write((char *)&byte_rate, 4);
    stream.write((char *)&align, 2);
    stream.write((char *)&bits, 2);
    stream.write("data", 4);
    stream.write((char *)&size, 4);
    stream.write((char *)samples.data(), size);
    if (!stream)
        throw std::runtime_error("Cannot write startup audio capture");
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        std::filesystem::path pack_path, capture_dir, audio_path, title_logo_path;
        std::wstring ready_event, presented_event, cancel_event;
        DWORD owner_pid=0;
        bool headless = false, mute = false, warp = false, validate = false, realtime = false,
             core_only = false;
        unsigned skip_at = 0, volume = 100;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            auto value = [&]() {
                if (++i >= argc)
                    throw std::runtime_error("Missing command argument");
                return std::wstring(argv[i]);
            };
            if (arg == L"--pack")
                pack_path = value();
            else if (arg == L"--title-logo")
                title_logo_path = value();
            else if (arg == L"--handoff-ready")
                ready_event = value();
            else if (arg == L"--handoff-presented")
                presented_event = value();
            else if (arg == L"--handoff-cancel")
                cancel_event = value();
            else if (arg == L"--handoff-parent") {
                std::wstring number=value(); size_t end=0;
                auto n=std::stoul(number,&end);
                if (end!=number.size() || !n) throw std::runtime_error("Invalid startup owner");
                owner_pid=DWORD(n);
            }
            else if (arg == L"--capture-dir")
                capture_dir = value();
            else if (arg == L"--audio-file")
                audio_path = value();
            else if (arg == L"--headless")
                headless = true;
            else if (arg == L"--mute")
                mute = true;
            else if (arg == L"--warp")
                warp = true;
            else if (arg == L"--validate")
                validate = true;
            else if (arg == L"--realtime")
                realtime = true;
            else if (arg == L"--core-only")
                core_only = true;
            else if (arg == L"--volume") {
                std::wstring number = value();
                size_t end = 0;
                unsigned long n = std::stoul(number, &end);
                if (end != number.size() || n > 100)
                    throw std::runtime_error("Volume must be between 0 and 100");
                volume = (unsigned)n;
            } else if (arg == L"--skip-at-ms") {
                std::wstring number = value();
                size_t end = 0;
                unsigned long n = std::stoul(number, &end);
                if (end != number.size() || n < 1 || n > 8000)
                    throw std::runtime_error("Skip boundary must be between 1 and 8000 ms");
                skip_at = (unsigned)n;
            } else
                throw std::runtime_error("Unknown startup argument");
        }
        if (pack_path.empty())
            throw std::runtime_error("Use --pack with locally extracted startup assets");
        if (headless && !mute)
            throw std::runtime_error("Unattended startup runs require --mute");
        if (core_only && !capture_dir.empty())
            throw std::runtime_error("Core-only run cannot capture rendered pictures");
        if (!capture_dir.empty() && !std::filesystem::create_directories(capture_dir))
            throw std::runtime_error("Capture directory already exists");
        auto input = read_pack(pack_path);
        std::vector<unsigned char> title_logo;
        if (!title_logo_path.empty()) {
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(title_logo_path)) ||
                std::filesystem::file_size(title_logo_path)!=690)
                throw std::runtime_error("Unsupported selected-title startup logo size");
            title_logo.resize(690);
            std::ifstream file(title_logo_path,std::ios::binary);
            file.read(reinterpret_cast<char *>(title_logo.data()),title_logo.size());
            if (!file || sha256(title_logo.data(),title_logo.size())!=
                    "7b6b049abaf7ebef37be861d1f5a868984e076c7d39347ac81e812d087ab7660")
                throw std::runtime_error("Unsupported selected-title startup logo");
        }
        const auto after_image=BootAfterImage::load(input,title_logo);
        BootHandoff handoff;
        handoff.open(ready_event,presented_event,cancel_event,owner_pid);
        if (handoff && core_only) throw std::runtime_error("Core-only run cannot own a presentation handoff");
        Aligned pack(input.size()), state_bytes(xb_state_size()), audio_bytes(xb_audio_size());
        std::memcpy(pack.data, input.data(), input.size());
        xb_state *state = (xb_state *)state_bytes.data;
        xb_config config{};
        auto start = std::chrono::steady_clock::now();
        core_check(xb_init(state, pack.data, pack.size, &config), "Initialize native startup");
        core_check(xb_audio_init(state, audio_bytes.data, audio_bytes.size),
                   "Initialize startup synthesis");
        std::vector<int16_t> pcm(384000 * 2);
        core_check(xb_audio(state, pcm.data(), 384000), "Synthesize startup audio");
        const auto pcm_hash = sha256(pcm.data(), pcm.size() * 2);
        int peak = 0;
        for (int16_t sample : pcm)
            peak = std::max(peak, std::abs((int)sample));
        if (!peak)
            throw std::runtime_error("Startup synthesis was unexpectedly silent");
        if (!audio_path.empty())
            wav(audio_path, pcm);
        Window window(headless || core_only);
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGISwapChain> swap;
        ComPtr<ID3D11InfoQueue> info;
        std::unique_ptr<BootRenderer> renderer;
        if (!core_only) {
            unsigned flags = validate ? D3D11_CREATE_DEVICE_DEBUG : 0;
            D3D_FEATURE_LEVEL level;
            if (window.value) {
                DXGI_SWAP_CHAIN_DESC desc{};
                desc.BufferCount = 2;
                desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.OutputWindow = window.value;
                desc.SampleDesc.Count = 1;
                desc.Windowed = TRUE;
                desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
                check(D3D11CreateDeviceAndSwapChain(
                          nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                          flags, nullptr, 0, D3D11_SDK_VERSION, &desc, &swap, &device, &level,
                          &context),
                      "Create startup graphics");
            } else
                check(D3D11CreateDevice(
                          nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                          flags, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context),
                      "Create offscreen startup graphics");
            if (level < D3D_FEATURE_LEVEL_11_0)
                throw std::runtime_error("Startup renderer requires D3D feature level 11");
            if (validate)
                check(device.As(&info), "Create graphics validation queue");
            renderer = std::make_unique<BootRenderer>(device.Get(), context.Get(), state);
        }
        Audio audio;
        Controllers controller;
        if (volume < 100)
            for (auto &sample : pcm)
                sample = (int16_t)((int)sample * (int)volume / 100);
        const bool play_audio = !mute && !core_only && volume;
        if (play_audio)
            audio.prepare(pcm);
        if (window.value) {
            ShowWindow(window.value, SW_SHOWNORMAL);
            UpdateWindow(window.value);
        }
        const double setup_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        auto animation_start = std::chrono::steady_clock::now();
        if (play_audio)
            audio.start();
        const bool paced = window.value || realtime;
        BootTimeline timeline;
        bool skipped = false;
        unsigned frames = 0, tick = 0;
        int result = XB_OK;
        std::vector<xb_draw> draws(core_only ? XB_FRAME_DRAWS_MAX : 0);
        std::vector<float> params(core_only ? XB_FRAME_PARAMS_MAX : 0);
        unsigned long long described = 0;
        auto present = [&]() {
            if (!window.value) return;
            RECT rect{};
            GetClientRect(window.value, &rect);
            unsigned width = std::max(1L, rect.right), height = std::max(1L, rect.bottom);
            ComPtr<ID3D11Texture2D> back;
            check(swap->GetBuffer(0, IID_PPV_ARGS(&back)), "Get startup backbuffer");
            D3D11_TEXTURE2D_DESC desc{};
            back->GetDesc(&desc);
            if (desc.Width != width || desc.Height != height) {
                back.Reset();
                check(swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "Resize startup window");
                check(swap->GetBuffer(0, IID_PPV_ARGS(&back)), "Get resized startup backbuffer");
            }
            ComPtr<ID3D11RenderTargetView> target;
            check(device->CreateRenderTargetView(back.Get(), nullptr, &target), "View startup backbuffer");
            renderer->show(target.Get(), width, height);
            check(swap->Present(0, 0), "Present startup");
        };
        do {
            if (window.value) {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                if (close_requested || controller.skip()) {
                    skipped = true;
                    break;
                }
            }
            if (paced) {
                std::this_thread::sleep_until(
                    animation_start + std::chrono::milliseconds(timeline.next_deadline_ms()));
                tick = timeline.observe(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - animation_start)
                                            .count());
            } else
                tick = static_cast<unsigned>(BootTimeline::frame_tick(frames + 1));
            if (skip_at && tick >= skip_at) {
                skipped = true;
                break;
            }
            result = xb_advance(state, tick);
            if (result < 0)
                throw std::runtime_error("Native startup advance failed");
            if (renderer) {
                renderer->draw();
                if (result == XB_DONE) {
                    if (!capture_dir.empty()) renderer->capture((capture_dir / L"animation-final.bmp").wstring());
                    renderer->after_image(after_image);
                }
                if (!capture_dir.empty() && (frames == 0 || frames == 119 || frames == 239 ||
                                             frames == 313 || frames == 358 || result == XB_DONE)) {
                    std::wostringstream file;
                    file << L"frame-" << std::setfill(L'0') << std::setw(3) << frames << L".bmp";
                    renderer->capture((capture_dir / file.str()).wstring());
                }
                present();
            } else {
                xb_frame_out frame{};
                frame.draws = draws.data();
                frame.draw_cap = (uint32_t)draws.size();
                frame.params = params.data();
                frame.param_cap = (uint32_t)params.size();
                core_check(xb_frame(state, &frame), "Describe native startup");
                described += frame.draw_count;
            }
            ++frames;
            if (frames > 600 ||
                std::chrono::steady_clock::now() - animation_start > std::chrono::seconds(20))
                throw std::runtime_error("Startup exceeded its bounded run budget");
        } while (result != XB_DONE);
        const double elapsed_ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - animation_start)
                                      .count();
        if (!skipped && (result != XB_DONE || xb_time(state) < 8))
            throw std::runtime_error("Startup ended before completion boundary");
        if (!skipped)
            audio.finish();
        unsigned lit_pixels = renderer ? renderer->nonblack_pixels() : 0;
        if (renderer && !skipped && !lit_pixels)
            throw std::runtime_error("Startup final picture was unexpectedly blank");
        if (renderer && !skipped && !capture_dir.empty()) {
            renderer->capture_presentation((capture_dir / L"final-16x9.bmp").wstring(), 1280, 720);
            renderer->capture_presentation((capture_dir / L"final-21x9.bmp").wstring(), 1680, 720);
            renderer->capture_presentation((capture_dir / L"final-32x9.bmp").wstring(), 2560, 720);
        }
        if (!pack.guard() || !state_bytes.guard() || !audio_bytes.guard() ||
            sha256(pack.data, pack.size) != PACK_SHA256)
            throw std::runtime_error("Startup memory integrity check failed");
        unsigned errors = 0;
        if (info) {
            for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
                SIZE_T length = 0;
                info->GetMessage(i, nullptr, &length);
                std::vector<unsigned char> storage(length);
                auto *message = (D3D11_MESSAGE *)storage.data();
                if (SUCCEEDED(info->GetMessage(i, message, &length)) &&
                    message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) {
                    ++errors;
                    std::cerr << "D3D: " << message->pDescription << "\n";
                }
            }
        }
        if (errors)
            throw std::runtime_error("Startup graphics validation reported errors");
        const char *handoff_result="not-requested";
        double handoff_ms=0;
        if (handoff && !skipped) {
            // The final original image remains the helper's scanout until the
            // owned title acknowledges its first successful presentation.
            auto began=std::chrono::steady_clock::now();
            handoff.ready();
            while (!(handoff_result=handoff.retirement())) {
                MSG msg;
                while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
                if (close_requested || (window.value && controller.skip())) { handoff_result="skipped"; break; }
                if (std::chrono::steady_clock::now()-began>std::chrono::seconds(60)) { handoff_result="timeout"; break; }
                present();
                Sleep(10);
            }
            handoff_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
        }
        std::cout << "{\"schema\":\"xboxllr.native-boot-run.v1\",\"implementation\":\"native-"
                     "reconstruction\",\"status\":\""
                  << (skipped ? "skipped" : "completed") << "\",\"frames\":" << frames
                  << ",\"time\":" << xb_time(state)
                  << ",\"draws\":" << (renderer ? renderer->draw_count() : described)
                  << ",\"setup_ms\":" << setup_ms << ",\"run_ms\":" << elapsed_ms
                  << ",\"timeline\":\"" << (paced ? "monotonic-ms" : "deterministic-60hz") << "\""
                  << ",\"firmware_profile\":\"modified-4983-xecuter2\",\"after_image\":\""
                  << (title_logo.empty() ? "embedded-firmware-xecuter2" : "selected-title-xbe-microsoft") << "\""
                  << ",\"handoff\":\"" << handoff_result << "\",\"handoff_ms\":" << handoff_ms
                  << ",\"audio_frames\":384000,\"audio_peak\":" << peak
                  << ",\"final_nonblack_pixels\":" << lit_pixels << ",\"pcm_sha256\":\"" << pcm_hash
                  << "\",\"audio_output\":\"" << (!mute && !core_only && volume ? "enabled" : "muted")
                  << "\",\"graphics_validation\":" << (validate ? "true" : "false")
                  << ",\"presentation\":\"" << (core_only ? "core-only" : "d3d11") << "\"}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Native startup failed: " << error.what() << "\n";
        return 2;
    }
}
