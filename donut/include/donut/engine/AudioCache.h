/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <map>
#include <mutex>
#include <string>

#ifdef DONUT_WITH_TASKFLOW
namespace tf
{
    class Executor;
}
#endif

namespace donut::vfs
{
    class IBlob;
    class IFileSystem;
}

namespace donut::engine::audio
{

class AudioCache;

// AudioData : handle issued by the AudioCache with basic interface to
// audio sample data.
//
class AudioData
{
public:

    // duration of the sample (in seconds)
    float duration() const { return float(samplesSize) / float(byteRate); }

    uint32_t nsamples() const { return samplesSize / (bitsPerSample * nchannels); }

    // true if the audia data is playable
    bool valid() const { return m_data && samples; }

public:

    enum class Format
    {
        WAVE_UNDEFINED = 0,
        WAVE_PCM_INTEGER = 1
    } format = Format::WAVE_UNDEFINED;

    uint32_t nchannels = 0,          // 1 = mono, 2 = stereo, ...
             sampleRate = 0,         // in Hz
             byteRate = 0;           // = sampleRate * nchannels * bitsPerSample / 8

    uint16_t bitsPerSample = 0,
             blockAlignment = 0;     // = nchacnnels * bitsPerSample / 8

    uint32_t samplesSize = 0;        // size in bytes of the samples data

    void const * samples = nullptr;  // pointer to samples data start in m_data

private:

    friend class AudioCache;

    std::shared_ptr<donut::vfs::IBlob> m_data;
};

// AudioCache : cache for audio data with synch & async read from 
// donut vfs::IFileSystem
//
class AudioCache
{
public:

    AudioCache(std::shared_ptr<vfs::IFileSystem> fs);

    // Release all cached audio files
    void Reset();

public:

    // Synchronous read
    std::shared_ptr<AudioData const> LoadFromFile(const std::filesystem::path & path);

#ifdef DONUT_WITH_TASKFLOW
    // Asynchronous read
    std::shared_ptr<AudioData const> LoadFromFileAsync(const std::filesystem::path & path, tf::Executor& executor);
#endif

private:

    static std::shared_ptr<AudioData const> importRiff(std::shared_ptr<donut::vfs::IBlob> blob, char const * filepath);

    std::shared_ptr<AudioData const> loadAudioFile (const std::filesystem::path & path);

    bool findInCache(const std::filesystem::path & path, std::shared_ptr<AudioData const> & result);

    void sendAudioLoadedMessage(std::shared_ptr<AudioData const> audio, char const * path);

private:

    std::mutex m_LoadedDataMutex;

    std::map<std::string, std::shared_ptr<AudioData const>> m_LoadedAudioData;

    std::shared_ptr<donut::vfs::IFileSystem> m_fs;
};

} // namespace donut::engine::audio
