/*
 * M4A -> AAC converter
 * decode M4A to PCM (IMFSourceReader) -> encode to AAC ADTS (IMFSinkWriter)
 * Zero external dependency, same Media Foundation stack as the recorder.
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
    if (FAILED(hr)) { wprintf(L"CoInitializeEx failed: 0x%08X\n", hr); return 1; }
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) { wprintf(L"MFStartup failed: 0x%08X\n", hr); CoUninitialize(); return 1; }

    wprintf(L"[STEP 1] Opening: %s\n", argv[1]);
    fflush(stdout);

    // 1. Source reader
    IMFSourceReader* reader = nullptr;
    hr = MFCreateSourceReaderFromURL(argv[1], nullptr, &reader);
    if (FAILED(hr)) {
        wprintf(L"[ERROR] Cannot open file: 0x%08X\n", hr);
        MFShutdown(); CoUninitialize(); return 1;
    }

    // 2. Get source audio info (default PCM output type)
    IMFMediaType* pcmType = nullptr;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pcmType);
    if (FAILED(hr) || !pcmType) {
        // Try getting native media type and set as current
        for (DWORD i = 0; i < 5; i++) {
            hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, i, &pcmType);
            if (SUCCEEDED(hr) && pcmType) {
                reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcmType);
                pcmType->Release(); pcmType = nullptr;
                reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pcmType);
                break;
            }
        }
    }

    UINT32 sampleRate = 44100, channels = 2;
    if (pcmType) {
        pcmType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
        pcmType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        pcmType->Release();
    }
    wprintf(L"[STEP 2] Source: %d Hz, %d ch\n", sampleRate, channels); fflush(stdout);

    // 3. Output path
    std::wstring outPath;
    if (argc >= 3) {
        outPath = argv[2];
    } else {
        std::wstring in(argv[1]);
        auto dot = in.rfind(L'.');
        outPath = (dot != std::wstring::npos) ? in.substr(0, dot) + L".aac" : in + L".aac";
    }

    // 4. Sink writer for AAC ADTS output
    IMFAttributes* sinkAttrs = nullptr;
    MFCreateAttributes(&sinkAttrs, 1);
    sinkAttrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_ADTS);

    IMFSinkWriter* writer = nullptr;
    hr = MFCreateSinkWriterFromURL(outPath.c_str(), nullptr, sinkAttrs, &writer);
    sinkAttrs->Release();
    if (FAILED(hr)) {
        wprintf(L"[ERROR] SinkWriter create: 0x%08X\n", hr);
        reader->Release(); MFShutdown(); CoUninitialize(); return 1;
    }

    // Output type (AAC)
    IMFMediaType* outType = nullptr;
    MFCreateMediaType(&outType);
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
    outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);
    DWORD outIdx = 0;
    hr = writer->AddStream(outType, &outIdx);
    outType->Release();
    if (FAILED(hr)) {
        wprintf(L"[ERROR] AddStream: 0x%08X\n", hr);
        writer->Release(); reader->Release(); MFShutdown(); CoUninitialize(); return 1;
    }

    // Input type (PCM) — BlockAlign and AvgBytesPerSec required
    DWORD blockAlign = channels * 2;
    IMFMediaType* inType = nullptr;
    MFCreateMediaType(&inType);
    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
    inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
    inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sampleRate * blockAlign);
    hr = writer->SetInputMediaType(outIdx, inType, nullptr);
    inType->Release();
    if (FAILED(hr)) {
        wprintf(L"[ERROR] SetInputMediaType: 0x%08X\n", hr);
        writer->Release(); reader->Release(); MFShutdown(); CoUninitialize(); return 1;
    }

    hr = writer->BeginWriting();
    if (FAILED(hr)) {
        wprintf(L"[ERROR] BeginWriting: 0x%08X\n", hr);
        writer->Release(); reader->Release(); MFShutdown(); CoUninitialize(); return 1;
    }

    wprintf(L"[STEP 3] Converting...\n"); fflush(stdout);

    // 5. Read PCM -> Write AAC
    LONGLONG rt = 0;
    int frames = 0;
    DWORD streamIdx, flags;
    LONGLONG ts;

    for (;;) {
        IMFSample* sample = nullptr;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &streamIdx, &flags, &ts, &sample);
        if (FAILED(hr)) { wprintf(L"  ReadSample error: 0x%08X\n", hr); break; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (sample) sample->Release(); break; }
        if (!sample) continue;

        DWORD len = 0;
        hr = sample->GetTotalLength(&len);
        if (FAILED(hr) || len == 0) { sample->Release(); continue; }

        sample->SetSampleTime(rt);
        LONGLONG dur = (LONGLONG)len * 10000000LL / (sampleRate * channels * 2);
        sample->SetSampleDuration(dur);
        rt += dur;

        hr = writer->WriteSample(outIdx, sample);
        sample->Release();
        if (SUCCEEDED(hr)) frames++;
        else wprintf(L"  WriteSample error: 0x%08X\n", hr);
    }

    wprintf(L"[STEP 4] Finalizing... (%d samples)\n", frames); fflush(stdout);

    // 6. Finalize
    hr = writer->Finalize();
    writer->Release();
    reader->Release();

    if (SUCCEEDED(hr)) {
        wprintf(L"[OK] %d samples -> %s\n", frames, outPath.c_str());
    } else {
        wprintf(L"[ERROR] Finalize: 0x%08X\n", hr);
    }

    MFShutdown();
    CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 1;
}
