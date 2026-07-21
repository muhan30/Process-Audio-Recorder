/*
 * M4A -> AAC remuxer — AAC sync-word frame extraction
 * Scans M4A for 0xFFF AAC sync words, wraps each frame in ADTS header.
 * Zero re-encoding, zero quality loss, zero Media Foundation dependency.
 * Reference: ffmpeg -acodec copy semantics from MystiQ.
 */
#include <Windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <algorithm>

// ADTS header: 7 bytes before each AAC frame
static void WriteADTS(FILE* f, int frameSize, int profile, int srIdx, int channels)
{
    int flen = frameSize + 7;
    BYTE h[7] = {};
    h[0] = 0xFF; h[1] = 0xF1;
    h[2] = (BYTE)((profile << 6) | (srIdx << 2) | ((channels >> 2) & 1));
    h[3] = (BYTE)(((channels & 3) << 6) | ((flen >> 11) & 3));
    h[4] = (BYTE)((flen >> 3) & 0xFF);
    h[5] = (BYTE)(((flen & 7) << 5) | 0x1F);
    h[6] = 0xFC;
    fwrite(h, 1, 7, f);
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2) {
        wprintf(L"M4A -> AAC remuxer (sync-word extraction, no re-encode)\n");
        wprintf(L"Usage: %s input.m4a [output.aac]\n", argv[0]);
        return 0;
    }

    // Read entire file
    FILE* in = _wfopen(argv[1], L"rb");
    if (!in) { wprintf(L"[ERROR] Cannot open: %s\n", argv[1]); return 1; }
    fseek(in, 0, SEEK_END);
    long fsize = ftell(in);
    fseek(in, 0, SEEK_SET);
    if (fsize <= 0) { wprintf(L"[ERROR] Empty file\n"); fclose(in); return 1; }

    std::vector<BYTE> data(fsize);
    if (fread(data.data(), 1, fsize, in) != (size_t)fsize) { wprintf(L"[ERROR] Read failed\n"); fclose(in); return 1; }
    fclose(in);

    // Find mdat atoms — only search within these for AAC frames
    std::vector<std::pair<long,long>> mdatRanges; // {offset, size}
    for (long i = 0; i < fsize - 8; i++) {
        DWORD sz = ((DWORD)data[i] << 24) | ((DWORD)data[i+1] << 16) | ((DWORD)data[i+2] << 8) | data[i+3];
        DWORD tp = ((DWORD)data[i+4] << 24) | ((DWORD)data[i+5] << 16) | ((DWORD)data[i+6] << 8) | data[i+7];
        if (tp == 'mdat' && sz >= 8 && (long)(i + sz) <= fsize) {
            mdatRanges.push_back({i + 8, sz - 8}); // data starts after 8-byte header
            i += sz - 1;
        }
    }
    if (mdatRanges.empty()) { wprintf(L"[ERROR] No mdat atom found (not a valid M4A/MP4 file)\n"); return 1; }

    // Helper: is position within any mdat?
    auto inMdat = [&](long pos) {
        for (auto& r : mdatRanges) if (pos >= r.first && pos < r.first + r.second) return true;
        return false;
    };

    // Find AAC frames by sync word 0xFFF (12 bits: 0xFFF)
    // AAC frame header structure (after sync):
    //   1 bit  ID (0=MPEG-4)
    //   2 bits layer (0)
    //   1 bit  protection
    //   2 bits profile (1=LC, 2=profile+1 ... actually 0=Main,1=LC,2=SSR,3=reserved)
    //   4 bits sampling_frequency_index
    //   1 bit  private
    //   3 bits channel_configuration
    std::vector<long> frameOffsets;
    int profile = 1; // default AAC-LC
    int srIdx = 4;    // default 44100
    int channels = 2;

    for (long i = 0; i < fsize - 2; i++) {
        if (data[i] == 0xFF && (data[i+1] & 0xF0) == 0xF0) {
            // Found potential AAC sync word
            // Validate: ID=0 (MPEG-4), layer=0
            if (i + 1 < fsize) {
                BYTE b1 = data[i+1];
                int id = (b1 >> 3) & 1;
                int layer = (b1 >> 1) & 3;
                if (id == 0 && layer == 0 && inMdat(i)) { // Valid MPEG-4 AAC in mdat
                    if (frameOffsets.empty() && i + 2 < fsize) {
                        // Extract profile, srIdx, channels from first frame
                        BYTE b2 = data[i+2];
                        profile = (b2 >> 6) & 3; // 0=Main,1=LC,2=SSR,3=reserved
                        srIdx = (b2 >> 2) & 0xF;
                        channels = ((b2 & 3) << 1) | ((data[i+3] >> 7) & 1);
                        if (profile == 0) profile = 1; // default LC
                        if (srIdx > 12) srIdx = 4;
                        if (channels == 0) channels = 2;
                        if (channels == 7) channels = 8;
                    }
                    frameOffsets.push_back(i);
                    i += 1; // skip the next byte (already consumed as sync word low bits)
                }
            }
        }
    }

    if (frameOffsets.size() < 2) {
        wprintf(L"[ERROR] No AAC frames found (not a valid AAC bitstream)\n"); return 1;
    }

    // Output path
    std::wstring outPath;
    if (argc >= 3) outPath = argv[2];
    else { std::wstring s(argv[1]); auto d = s.rfind(L'.'); outPath = (d != std::wstring::npos) ? s.substr(0, d) + L".aac" : s + L".aac"; }

    static const int srTable[] = { 96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350 };
    int sampleRate = (srIdx < 13) ? srTable[srIdx] : 44100;
    wprintf(L"[INFO] %d frames, profile=%d, %dHz %dch\n", (int)frameOffsets.size(), profile, sampleRate, channels);

    FILE* out = _wfopen(outPath.c_str(), L"wb");
    if (!out) { wprintf(L"[ERROR] Cannot create: %s\n", outPath.c_str()); return 1; }

    // Write each frame with ADTS header (frame size = distance to next sync word)
    int written = 0;
    for (size_t i = 0; i < frameOffsets.size(); i++) {
        long start = frameOffsets[i];
        long end = (i + 1 < frameOffsets.size()) ? frameOffsets[i+1] : fsize;
        int frameSize = (int)(end - start);
        if (frameSize < 2) continue;
        WriteADTS(out, frameSize, profile, srIdx, channels);
        fwrite(data.data() + start, 1, frameSize, out);
        written++;
    }
    fclose(out);

    LARGE_INTEGER sz = {}; HANDLE h = CreateFileW(outPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { GetFileSizeEx(h, &sz); CloseHandle(h); }
    wprintf(L"[OK] %d frames remuxed, %.1f KB -> %s\n", written, sz.QuadPart / 1024.0, outPath.c_str());
    return 0;
}
