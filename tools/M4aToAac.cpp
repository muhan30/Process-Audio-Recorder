/*
 * M4A -> AAC converter (Media Foundation, zero external dependencies)
 * IMFSourceReader decodes M4A to PCM, IMFSinkWriter encodes PCM to AAC ADTS.
 * Reference: MystiQ presets "-vn -acodec aac -b:a 128k -ac 2 -ar 48000"
 */
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdio.h>
#include <string>

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2) {
        wprintf(L"M4A -> AAC converter (Media Foundation)\n");
        wprintf(L"Usage: %s input.m4a [output.aac]\n", argv[0]);
        return 0;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) { wprintf(L"CoInitializeEx: 0x%08X\n", hr); return 1; }
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) { wprintf(L"MFStartup: 0x%08X\n", hr); CoUninitialize(); return 1; }

    // 1. Source reader (decode M4A to PCM)
    IMFSourceReader* reader = nullptr;
    hr = MFCreateSourceReaderFromURL(argv[1], nullptr, &reader);
    if (FAILED(hr)) {
        wprintf(L"[ERROR] Cannot open: %s (0x%08X)\n", argv[1], hr);
        MFShutdown(); CoUninitialize(); return 1;
    }

    // 2. Get audio info from source
    UINT32 sampleRate = 44100, channels = 2;
    IMFMediaType* pcmType = nullptr;
    if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pcmType)) && pcmType) {
        pcmType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
        pcmType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        pcmType->Release();
    }
    wprintf(L"[INFO] %s -> %dHz %dch\n", argv[1], sampleRate, channels); fflush(stdout);

    // 3. Output path
    std::wstring outPath;
    if (argc >= 3) { outPath = argv[2]; }
    else { std::wstring s(argv[1]); auto d = s.rfind(L'.'); outPath = (d != std::wstring::npos) ? s.substr(0, d) + L".aac" : s + L".aac"; }

    // 4. Sink writer for AAC ADTS output
    IMFAttributes* sinkAttrs = nullptr;
    MFCreateAttributes(&sinkAttrs, 1);
    sinkAttrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_ADTS);

    IMFSinkWriter* writer = nullptr;
    hr = MFCreateSinkWriterFromURL(outPath.c_str(), nullptr, sinkAttrs, &writer);
    sinkAttrs->Release();
    if (FAILED(hr)) { wprintf(L"[ERROR] SinkWriter: 0x%08X\n", hr); reader->Release(); MFShutdown(); CoUninitialize(); return 1; }

    // Output stream type (AAC)
    IMFMediaType* outType = nullptr; MFCreateMediaType(&outType);
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
    outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);
    DWORD outIdx = 0; hr = writer->AddStream(outType, &outIdx); outType->Release();
    if (FAILED(hr)) { wprintf(L"[ERROR] AddStream: 0x%08X\n", hr); writer->Release(); reader->Release(); MFShutdown(); CoUninitialize(); return 1; }

    // Input type (PCM — minimal attrs, let MF negotiate the rest)
    IMFMediaType* inType = nullptr; MFCreateMediaType(&inType);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
    inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    hr = writer->SetInputMediaType(outIdx, inType, nullptr); inType->Release();
    if (FAILED(hr)) { wprintf(L"[ERROR] SetInput: 0x%08X\n", hr); writer->Release(); reader->Release(); MFShutdown(); CoUninitialize(); return 1; }

    hr = writer->BeginWriting();
    if (FAILED(hr)) { wprintf(L"[ERROR] BeginWrite: 0x%08X\n", hr); writer->Release(); reader->Release(); MFShutdown(); CoUninitialize(); return 1; }

    // 5. Pipe: Read PCM samples, write to AAC encoder
    DWORD streamIdx, flags; LONGLONG ts, rt = 0; int frames = 0;
    for (;;) {
        IMFSample* sample = nullptr;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &streamIdx, &flags, &ts, &sample);
        if (FAILED(hr)) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (sample) sample->Release(); break; }
        if (!sample) continue;
        DWORD len = 0;
        if (FAILED(sample->GetTotalLength(&len)) || len == 0) { sample->Release(); continue; }
        sample->SetSampleTime(rt);
        LONGLONG dur = (LONGLONG)len * 10000000LL / (sampleRate * channels * 2);
        sample->SetSampleDuration(dur);
        rt += dur;
        if (SUCCEEDED(writer->WriteSample(outIdx, sample))) frames++;
        sample->Release();
    }

    // 6. Finalize
    hr = writer->Finalize();
    writer->Release(); reader->Release();

    if (SUCCEEDED(hr)) {
        LARGE_INTEGER sz = {}; HANDLE h = CreateFileW(outPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) { GetFileSizeEx(h, &sz); CloseHandle(h); }
        wprintf(L"[OK] %d samples, %.1f KB -> %s\n", frames, sz.QuadPart / 1024.0, outPath.c_str());
    } else { wprintf(L"[ERROR] Finalize: 0x%08X\n", hr); DeleteFileW(outPath.c_str()); }

    MFShutdown(); CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 1;
}
