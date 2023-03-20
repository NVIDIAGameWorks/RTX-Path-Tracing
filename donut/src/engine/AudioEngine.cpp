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

#include <donut/engine/AudioEngine.h>
#include <donut/engine/AudioCache.h>
#include <donut/core/log.h>

#ifdef WIN32
#include <xaudio2.h>
#include <xaudio2fx.h>
#include <x3daudio.h>
#endif

#include <cassert>
#include <chrono>
#include <list>
#include <map>
#include <thread>
#include <unordered_map>

using namespace donut;
using namespace donut::math;

// lock-free container for safe asynchronous manipulation of small data items
template <typename T> class Lockfree
{
public:

    Lockfree(T const & v) : value{ v, {} }, toggle(0) { }

    Lockfree & operator = (T const & rhs) { this->set(rhs); return *this; }

    void set(T const & v) { value[!toggle] = v; toggle = !toggle; }

    T const & get() const { return value[toggle]; }

    T & get() { return value[toggle]; }

private:

    T value[2];  // read/write data container
    volatile bool toggle; // toggle pointing at safe-to-read value
};

namespace donut::engine::audio
{

static uint32_t makeKey(std::shared_ptr<AudioData const> sample)
{
    if (!sample || !sample->valid())
        return 0;
    if (sample->nchannels > (2^8-1))
        return 0;

    union KeyGen
    {
        struct PCM
        {
            uint32_t tag      : 8,
                     channels : 8,
                     bps      : 8;
        } pcm;
        uint32_t value;
    } key = { 0 };

    key.pcm.tag = (uint8_t)AudioData::Format::WAVE_PCM_INTEGER;
    key.pcm.channels = sample->nchannels;
    key.pcm.bps = sample->bitsPerSample;
    return key.value;
}

// virtual PIMPL base
class Engine::Implementation
{
public:

    Implementation(Options const & opts) : m_options(opts) { }

    Options const & getOptions() const { return m_options; }

    virtual std::weak_ptr<Effect> playEffect(EffectDesc const & desc) = 0;
    virtual std::weak_ptr<Effect> playMusic(std::shared_ptr<AudioData const> sample, float crossfade) = 0;

    virtual bool crossfadeActive() const = 0;

    virtual void setMasterVolume(float volume) = 0;
    virtual void setEffectsVolume(float volume) = 0;
    virtual void setMusicVolume(float volume) = 0;

    virtual void setListenerTransform(affine3 const & transform) = 0;
    virtual void setListenerCallback(ListenerCallback const & callback) = 0;

    virtual bool startUpdateThread() = 0;
    virtual void stopUpdateThread() = 0;

    virtual ~Implementation() { }

protected:

    Options m_options;
};


//
// Windows XAudio2 implementation
//

#ifdef WIN32

// Xaudio2 helpers

inline void getFormatEX(std::shared_ptr<AudioData const> sample, WAVEFORMATEX * wfmtx)
{
    wfmtx->wFormatTag = WAVE_FORMAT_PCM;
    wfmtx->nChannels = sample->nchannels;
    wfmtx->nSamplesPerSec = sample->sampleRate;
    wfmtx->nAvgBytesPerSec = sample->byteRate;
    wfmtx->nBlockAlign = sample->blockAlignment;
    wfmtx->wBitsPerSample = sample->bitsPerSample;
    wfmtx->cbSize = 0;
}

static bool setOutputVoice(IXAudio2SubmixVoice * submix, IXAudio2SourceVoice * source)
{
    XAUDIO2_SEND_DESCRIPTOR sendDescriptors[1];
    sendDescriptors[0].Flags = XAUDIO2_SEND_USEFILTER;
    sendDescriptors[0].pOutputVoice = submix;
    const XAUDIO2_VOICE_SENDS sendList = { 1, sendDescriptors };

    HRESULT hr;
    if (FAILED(hr = source->SetOutputVoices(&sendList))) {
        log::warning("AudioEngine : Xaudio2 failed to assign output voice (%08x)", hr);
        return false;
    }
    return true;
}

static bool computePanMatrix(float pan, int nchannels, float * result)
{
    if (!result)
        return false;

    switch (nchannels)
    {
        case 1 :
        {
            result[0] = pan >= 0.f ? 1.f-pan : 1.f;
            result[1] = pan <= 0.f ? (-pan -1.f) : 1.f;
        } break;

        case 2 :
        {
            if (-1.f <= pan && pan <= 0.f)
            {
                result[0] = .5f * pan + 1.f;    // .5 when pan is -1, 1 when pan is 0
                result[1] = .5f * -pan;         // .5 when pan is -1, 0 when pan is 0
                result[2] = 0.f;                //  0 when pan is -1, 0 when pan is 0
                result[3] = pan + 1.f;          //  0 when pan is -1, 1 when pan is 0
            }
            else
            {
                result[0] = -pan + 1.f;         //  1 when pan is 0,   0 when pan is 1
                result[1] = 0.f;                //  0 when pan is 0,   0 when pan is 1
                result[2] = .5f * pan;          //  0 when pan is 0, .5f when pan is 1
                result[3] = .5f * -pan + 1.f;   //  1 when pan is 0. .5f when pan is 1
            }
        } break;

        default:
            log::warning("AudioEngine : mono or stereo source data supported only for panning matrix");
            return false;
    }
    return true;
}

static bool applyPan(float pan, IXAudio2SourceVoice * voice, int nchannels)
{
    if (!voice)
        return false;

    float matrix[16] = { 0.f };
    if (!computePanMatrix(pan, nchannels, matrix))
        return false;

    // submix voices are always stereo, so it's safe to always set destination voice channels to 2
    HRESULT hr;
    if (FAILED(hr = voice->SetOutputMatrix(nullptr, nchannels, 2, matrix)))
    {
        log::warning("AudioEngine : failed to set output matrix for voice (%08x)", hr);
        return false;
    }
    return true;
}

class Xaudio2Implementation;

//
// Xaudio2 effect specialization
//

struct Xaudio2Effect : public Effect
{
    ~Xaudio2Effect() { }

    std::weak_ptr<AudioData const> getSample() const;

    void setVolume(float volume) override;
    void setPitch(float pitch) override;
    void setPan(float pan) override;
    void pause() override;
    void stop() override;
    float played() override;
    void setEffectCallback(EffectCallback const & callback);

    virtual bool setEmitterTransform(donut::math::affine3 const & transform);

    std::shared_ptr<AudioData const > sample;
    bool stopped = false;
    uint32_t key = 0;
    IXAudio2SourceVoice * voice = nullptr;
    EffectCallback callback;
};

std::weak_ptr<AudioData const> Xaudio2Effect::getSample() const { return sample; }

// note : because client code may be holding a locked effect pointer to a voice that stopped
// playing, all public interface functors MUST make sure the voice not null.
void Xaudio2Effect::setVolume(float volume) { if (voice) voice->SetVolume(volume); }
void Xaudio2Effect::setPitch(float pitch) { if (voice) voice->SetFrequencyRatio(pitch); }
void Xaudio2Effect::pause() { if (voice) voice->Stop(); }
void Xaudio2Effect::stop() { pause(); (const_cast<Xaudio2Effect *>(this))->stopped = true; }
void Xaudio2Effect::setPan(float pan) { if (sample) applyPan(pan, voice, sample->nchannels); }
bool Xaudio2Effect::setEmitterTransform(donut::math::affine3 const & transform) { return false; }
void Xaudio2Effect::setEffectCallback(EffectCallback const & cb) { callback = cb; }

float Xaudio2Effect::played()
{
    if (voice && sample && sample->valid())
    {
        XAUDIO2_VOICE_STATE xstate;
        voice->GetState(&xstate);
        return float(xstate.SamplesPlayed) / sample->sampleRate;
    }
    else
        return -1.f;
}

// Xaudio2 3D effect specialization

struct Xaudio2Effect3D : public Xaudio2Effect
{
    virtual bool setEmitterTransform(donut::math::affine3 const & transform);

    void update(std::chrono::system_clock::time_point const & now, bool leftHanded);

    X3DAUDIO_EMITTER emitter;
    Lockfree<affine3> transform = affine3::identity();
    float3 prevPosition = { 0.f, 0.f, 0.f };
    std::chrono::system_clock::time_point prevTime;
};

bool Xaudio2Effect3D::setEmitterTransform(donut::math::affine3 const & xform) 
{
    transform = xform;
    return true; 
}

void Xaudio2Effect3D::update(std::chrono::system_clock::time_point const & now, bool leftHanded)
{
    using namespace std::chrono;

    float dt = (float)0.001f * duration_cast<milliseconds>(now - prevTime).count();

    affine3 const & xform = transform.get();

    float3 prevP = prevPosition,
           curP = xform.m_translation,
           V = dt * (curP - prevP);

    float3 front = xform.transformVector({ 0.f, 0.f, -1.f }),
           up = xform.transformVector({ 0.f, 1.f, 0.f });

    if (leftHanded)
    {
        emitter.Position = X3DAUDIO_VECTOR(curP.x, curP.y, curP.z);
        emitter.Velocity = X3DAUDIO_VECTOR(V.x, V.y, V.z);
        emitter.OrientFront = X3DAUDIO_VECTOR(front.x, front.y, front.z);
        emitter.OrientTop = X3DAUDIO_VECTOR(up.x, up.y, up.z);
    }
    else
    {
        emitter.Position = X3DAUDIO_VECTOR(curP.x, curP.y, -curP.z);
        emitter.Velocity = X3DAUDIO_VECTOR(V.x, V.y, -V.z);
        emitter.OrientFront = X3DAUDIO_VECTOR(front.x, front.y, -front.z);
        emitter.OrientTop = X3DAUDIO_VECTOR(up.x, up.y, -up.z);
    }

    prevPosition = prevP;
    prevTime = now;
}

//
// Xaudio2 engine specialization
//

class Xaudio2Implementation : public Engine::Implementation
{
public:

    virtual ~Xaudio2Implementation();

    static std::unique_ptr<Engine::Implementation> create(Options const & opts);

    virtual std::weak_ptr<Effect> playEffect(EffectDesc const & desc);

    virtual std::weak_ptr<Effect> playMusic(std::shared_ptr<AudioData const> sample, float crossfade);

    virtual bool crossfadeActive() const;

    virtual void setMasterVolume(float volume);
    virtual void setEffectsVolume(float volume);
    virtual void setMusicVolume(float volume);

    virtual bool startUpdateThread();
    virtual void stopUpdateThread();

    virtual void setListenerTransform(affine3 const & transform);
    virtual void setListenerCallback(Engine::ListenerCallback const & callback);

private:

    Xaudio2Implementation(Options const & opts) : Engine::Implementation(opts) { }

    static bool canPlaySample(std::shared_ptr<AudioData const> sample);

    IXAudio2SourceVoice * allocateVoice(uint32_t key, WAVEFORMATEX const & wfx);

    std::weak_ptr<Effect> playSample(IXAudio2SubmixVoice * submix, EffectDesc const & desc);

    void update();

    void clearVoices();

private:

    std::thread m_updateThread;
    bool m_updateRunning = false;

    IXAudio2 * m_xaudio2 = nullptr;

    // mastering & submix voices
    IXAudio2MasteringVoice * m_master = nullptr;
    uint32_t m_masterMask = 0,     // mastering voice mask
             m_masterChannels = 0, // number of master channels on hardware renderer
             m_masterRate = 0;     // effective mastering rate from hardware renderer

    IXAudio2SubmixVoice * m_effects = nullptr,
                        * m_music   = nullptr;

    // source voices interface
    std::mutex m_voicePoolMutex;

    std::unordered_multimap<uint32_t, IXAudio2SourceVoice *> m_voicePool; // see makeKey() for hashing details

    std::list<std::shared_ptr<Effect>> m_activeVoices;

    // music soundtrack
    std::weak_ptr<Effect> m_currentSong,
                          m_nextSong;

    std::chrono::system_clock::time_point m_crossfadeStart,
                                          m_crossfadeEnd;
    // 3D audio
    X3DAUDIO_HANDLE m_X3Daudio;

    //X3DAUDIO_DSP_SETTINGS m_dsp;

    struct Listener
    {
        void update(std::chrono::system_clock::time_point const & now, bool leftHanded);

        X3DAUDIO_LISTENER listen;

        Lockfree<affine3> transform = affine3::identity();
        float3 prevPosition = { 0.f, 0.f, 0.f };
        std::chrono::system_clock::time_point prevTime;
    } m_listener;

    Engine::ListenerCallback m_listenerCB;

private:

    struct EngineCallback : public IXAudio2EngineCallback
    {
        EngineCallback() noexcept(false) {}
        virtual ~EngineCallback() {}

        STDMETHOD_(void, OnProcessingPassStart) () override {}
        STDMETHOD_(void, OnProcessingPassEnd)() override {}
        STDMETHOD_(void, OnCriticalError) (THIS_ HRESULT error)
        {
            log::fatal("AudioEngine : critical error %08x", error);
        }
    } m_callback;
};

Xaudio2Implementation::~Xaudio2Implementation()
{
    stopUpdateThread();

    clearVoices();

    if (m_effects)
        m_effects->DestroyVoice();
    if (m_music)
        m_music->DestroyVoice();
    if (m_master)
        m_master->DestroyVoice();
    if (m_xaudio2)
        m_xaudio2->Release();
}

void Xaudio2Implementation::clearVoices()
{
    bool updateRunning = m_updateRunning;

    stopUpdateThread();

    m_voicePoolMutex.lock();
    for (auto it : m_activeVoices)
    {
        auto effect = std::dynamic_pointer_cast<Xaudio2Effect>(it);
        effect->voice->Stop();
        effect->voice->DestroyVoice();
    }
    m_activeVoices.clear();
    for (auto it : m_voicePool)
    {
        it.second->DestroyVoice();
    }
    m_voicePool.clear();
    m_voicePoolMutex.unlock();

    if (updateRunning)
        startUpdateThread();
}

void Xaudio2Implementation::setMasterVolume(float volume)
{
    HRESULT hr;
    if (!m_master || FAILED(hr = m_master->SetVolume(volume)))
        log::warning("AudioEngine : cannot set master volume");
}

void Xaudio2Implementation::setEffectsVolume(float volume)
{
    HRESULT hr;
    if (!m_effects || FAILED(hr = m_effects->SetVolume(volume)))
        log::warning("AudioEngine : cannot set effects volume");
}

void Xaudio2Implementation::setMusicVolume(float volume)
{
    HRESULT hr;
    if (!m_music || FAILED(hr = m_music->SetVolume(volume)))
        log::warning("AudioEngine : cannot set music volume");
}

bool Xaudio2Implementation::canPlaySample(std::shared_ptr<AudioData const> sample)
{
    if (!sample)
    {
        log::warning("AudioEngine : no sample passed");
        return false;
    }
    if (sample->format != AudioData::Format::WAVE_PCM_INTEGER)
    {
        log::warning("AudioEngine : audio format not supported");
        return false;
    }
    return true;
}

IXAudio2SourceVoice * Xaudio2Implementation::allocateVoice(uint32_t key, WAVEFORMATEX const & wfx)
{
    assert(m_voicePoolMutex.try_lock()==false); // make sure we can maniuplate the voice pool safely

    if (m_activeVoices.size() >= m_options.maxVoices)
    {
        log::warning("AudioEngine : cannot allocate voice ; max pool size reached");
        return nullptr;
    }

    // find a compatible voice in the pool
    IXAudio2SourceVoice * voice = nullptr;
    if (key != 0)
    {
        auto it = m_voicePool.find(key);
        if (it != m_voicePool.end())
        {
            voice = it->second;
            m_voicePool.erase(it);
        }
        else
        {
            HRESULT hr;
            if (FAILED(hr = m_xaudio2->CreateSourceVoice(&voice, &wfx, 0, 4.0f))) {
                log::warning("AudioEngine : Xaudio2 failed to create source voice (%08x)", hr);
                return nullptr;
            }
        }
    }
    return voice;
}

std::weak_ptr<Effect> Xaudio2Implementation::playSample(IXAudio2SubmixVoice * submix, EffectDesc const & desc)
{
    std::weak_ptr<Effect> result;

    if (!canPlaySample(desc.sample))
        return result;

    uint32_t key = makeKey(desc.sample);

    WAVEFORMATEX wfx;
    getFormatEX(desc.sample, &wfx);

    std::lock_guard<std::mutex> guard(m_voicePoolMutex);

    if (IXAudio2SourceVoice * voice = allocateVoice(key, wfx))
    {
        HRESULT hr;
        if (FAILED(hr = voice->SetSourceSampleRate(desc.sample->sampleRate)))
        {
#ifdef _DEBUG
            // manuelk : not sure why sometimes Xaudio2 returns XAUDIO2_E_INVALID_CALL here
            // when queuing many effects too quickly - setting to fail silently in release mode
            log::warning("AudioEngine : failed to set sample rate for audio sample (%08x)", hr);
#endif
            return result;
        }

        if (!setOutputVoice(submix, voice))
            return result;

        XAUDIO2_BUFFER buffer = { 0 };
        buffer.pAudioData = (BYTE const *)desc.sample->samples;
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = desc.sample->samplesSize;
        buffer.LoopCount = desc.loop == 1 ? XAUDIO2_NO_LOOP_REGION : std::min(desc.loop, (uint32_t)XAUDIO2_LOOP_INFINITE);

        if (voice->SubmitSourceBuffer(&buffer) != S_OK) {
            log::warning("AudioEngine : error SubmitSourceBuffer");
            return result;
        }

        if (desc.volume!=1.f)
            voice->SetVolume(desc.volume);
        if (desc.pitch!=1.f)
            voice->SetFrequencyRatio(desc.pitch);
        if (desc.pan != 0.f)
            applyPan(desc.pan, voice, desc.sample->nchannels);

        if (FAILED(hr = voice->Start(0)))
        {
            log::warning("Error starting voice for audio sample");
            return result;
        }

        std::shared_ptr<Xaudio2Effect> effect;

        if (!m_options.use3D || !desc.transform)
            effect = std::make_shared<Xaudio2Effect>();
        else
        {
            auto effect3D = std::make_shared<Xaudio2Effect3D>();
            memset(&effect3D->emitter, 0, sizeof(X3DAUDIO_EMITTER));
            effect3D->emitter.pCone = nullptr;
            effect3D->emitter.ChannelCount = desc.sample->nchannels;
            effect3D->emitter.ChannelRadius = 1.f;
            effect3D->emitter.CurveDistanceScaler = 1.f;
            effect3D->emitter.DopplerScaler = 1.f;
            effect3D->transform = *desc.transform ;
            effect3D->prevPosition = desc.transform->m_translation;
            effect3D->prevTime = std::chrono::system_clock::now();
            effect = effect3D;
        }
        effect->sample = desc.sample;
        effect->key = key;
        effect->voice = voice;
        effect->callback = desc.updateCB;

        m_activeVoices.emplace_back(effect);

        result = effect;
    }
    return result;
}

std::weak_ptr<Effect> Xaudio2Implementation::playEffect(EffectDesc const & desc)
{
    return playSample(m_effects, desc);
}

std::weak_ptr<Effect> Xaudio2Implementation::playMusic(std::shared_ptr<AudioData const> sample, float crossfade)
{
    std::weak_ptr<Effect> result;

    if (!canPlaySample(sample))
        return result;

    EffectDesc desc;
    desc.sample = sample;
    desc.loop = Engine::infinite_loop;
    desc.transform = nullptr;

    if (auto cursong = m_currentSong.lock())
    {
        using namespace std::chrono;
        m_crossfadeStart = system_clock::now();
        m_crossfadeEnd = m_crossfadeStart + milliseconds(uint32_t(crossfade*1000.f));

        if (auto nextsong = m_nextSong.lock())
        {
            // we are already in the middle of a crossfade
            cursong->stop();
            m_currentSong = m_nextSong;
        }
        result = m_nextSong = playSample(m_music, desc);
    }
    else
        result = m_currentSong = playSample(m_music, desc);
    return result;
}

bool Xaudio2Implementation::crossfadeActive() const
{
    return m_currentSong.expired() == false && m_nextSong.expired() == false;
}

void Xaudio2Implementation::update()
{
    using namespace std::chrono;

    while (m_updateRunning)
    {
        // stop inactive voices and move them back to available voice pool
        m_voicePoolMutex.lock();

        system_clock::time_point now = system_clock::now();

        // 3D listener setup (optional)
        DWORD calcFlags = X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER;
        FLOAT32 matrix[XAUDIO2_MAX_AUDIO_CHANNELS * 8] = {};
        FLOAT32 delays[XAUDIO2_MAX_AUDIO_CHANNELS] = {};
        X3DAUDIO_DSP_SETTINGS dsp;
        if (m_options.use3D)
        {
            // if client application registered a callback, invoke before update
            if (m_listenerCB)
                m_listenerCB();
            m_listener.update(now, m_options.leftHanded);
            memset(&dsp, 0, sizeof(X3DAUDIO_DSP_SETTINGS));
            dsp.pMatrixCoefficients = matrix;
            dsp.pDelayTimes = delays;
            dsp.DstChannelCount = 2; // submix voices are all stereo only !
        }

//log::info("update active=%d pool=%d", (int)m_activeVoices.size(), (int)m_voicePool.size());
        int idx = 0;
        for (auto it = m_activeVoices.begin(); it != m_activeVoices.end(); ++idx)
        {
            std::shared_ptr<Xaudio2Effect> effect = std::dynamic_pointer_cast<Xaudio2Effect>(*it);
            if (effect)
            {
//log::info("    effect %d played=%f", idx, effect->played());
                XAUDIO2_VOICE_STATE xstate;
                effect->voice->GetState(&xstate);

                // check if the voice is still playing something
                if (xstate.SamplesPlayed == 0 || effect->stopped == true)
                {
                    uint32_t key = effect->key;
                    IXAudio2SourceVoice * voice = effect->voice;
                    effect->voice = nullptr; // prevent client code from accessing this voice asynchronously

                    voice->Stop(0);
                    voice->FlushSourceBuffers();

                    // if we haven't reached the maximum number of voices, place the this one
                    // back in the pool for re-use, otherwise destroy it & trim the pool
                    if (m_activeVoices.size() + m_voicePool.size() < m_options.maxVoices)
                    {
                        std::unordered_map<uint32_t, IXAudio2SourceVoice *>::value_type v(key, voice);
                        m_voicePool.emplace(v);
                    }
                    else
                    {   // we have reached the maximum number of voices in the pool
                        if (m_voicePool.empty())
                            // all voices are active, there is no space left : destroy this one
                            voice->DestroyVoice();
                        else
                        {
                            // in order to avoid the voice pool being filled with key that don't match
                            // any samples, replace one of them with this one, in the hope this one
                            // will be more re-usable in case the older one had an uncommon key
                            auto oldvoice = m_voicePool.begin();
                            oldvoice->second->DestroyVoice();
                            m_voicePool.erase(oldvoice);
                            std::unordered_map<uint32_t, IXAudio2SourceVoice *>::value_type v(key, voice);
                            m_voicePool.emplace(v);
                        }
                    }
                    // remove the voice from the active list
                    it = m_activeVoices.erase(it);
                }
                else
                {
                    if (effect->callback)
                        effect->callback(*effect);

                    // if the voice is still active & is a 3D emitter, compute volume mix
                    if (m_options.use3D)
                        if (auto effect3D = std::dynamic_pointer_cast<Xaudio2Effect3D>(effect))
                        {
                            effect3D->update(now, m_options.leftHanded);

                            memset(matrix, 0, sizeof(matrix));
                            dsp.SrcChannelCount = effect3D->sample->nchannels;
                            X3DAudioCalculate(m_X3Daudio, &m_listener.listen, &effect3D->emitter, calcFlags, &dsp);

                            effect3D->voice->SetFrequencyRatio(dsp.DopplerFactor);

                            HRESULT hr;
                            if (FAILED(hr=effect3D->voice->SetOutputMatrix(m_effects, effect3D->sample->nchannels, 2, matrix)))
                            {
                                log::warning("AudioEngine : failed to apply output matrix to effect");
                            }
                        }
                    ++it;
                }
            }
            else
            {
                log::warning("AudioEngine : update thread encountered null effect");
                ++it;
            }
        }

        // manage music crossfade between songs if necessary
        if (auto nextsong = m_nextSong.lock())
        {
            duration<float, std::milli> elapsed = now - m_crossfadeStart,
                                        total = m_crossfadeEnd - m_crossfadeStart;
            float fade = elapsed / total;

            nextsong->setVolume(fade);

            if (auto cursong = m_currentSong.lock())
            {
                if (fade >= 1.f)
                {
                    cursong->stop();
                    m_currentSong = m_nextSong;
                    m_nextSong.reset();
                }
                else
                    cursong->setVolume(1.f - fade);
            }
        }
        m_voicePoolMutex.unlock();

        // sleep until next update tick
        auto wakeup = now + std::chrono::milliseconds(1000 / std::max(uint32_t(1), m_options.updateRate));
        std::this_thread::sleep_until(wakeup);
    }
}

bool Xaudio2Implementation::startUpdateThread()
{
    if (m_updateThread.joinable())
    {
        log::error("AudioEngine : update thread already running");
        return false;
    }
    m_updateRunning = true;
    m_updateThread = std::thread(&Xaudio2Implementation::update, this);
    return true;
}

void Xaudio2Implementation::stopUpdateThread()
{
    m_updateRunning = false;
    if (m_updateThread.joinable())
        m_updateThread.join();
}

std::unique_ptr<Engine::Implementation> Xaudio2Implementation::create(Options const & opts)
{
    HRESULT hr;
    if (FAILED(hr = CoInitializeEx(NULL, COINIT_MULTITHREADED)))
    {
        log::warning("AudioEngine : cannot initialize multi-threaded mode");
        return nullptr;
    }

    std::unique_ptr<Xaudio2Implementation> result(std::move(new Xaudio2Implementation(opts)));

    // interface
    if (FAILED(hr = XAudio2Create(&result->m_xaudio2, 0)))
    {
        log::warning("AudioEngine : cannot initialize XAudio2 interface");
        return nullptr;
    }

    if (FAILED(hr = result->m_xaudio2->RegisterForCallbacks(&result->m_callback)))
    {
        log::warning("AudioEngine : failed to register XAudio2 callback");
        return nullptr;
    }

    // mastering voice
    if (FAILED(hr = result->m_xaudio2->CreateMasteringVoice(&result->m_master, 2, opts.masteringRate)))
    {
        log::warning("AudioEngine : cannot initialize mastering mixer");
        return nullptr;
    }

    DWORD masterMask;
    if (FAILED(hr = result->m_master->GetChannelMask(&masterMask)))
    {
        log::warning("AudioEngine : error retrivieving mastering voice mask");
        return nullptr;
    }
    result->m_masterMask = masterMask;

    XAUDIO2_VOICE_DETAILS details;
    result->m_master->GetVoiceDetails(&details);
    result->m_masterChannels = details.InputChannels;
    result->m_masterRate = details.InputSampleRate;

    log::info("AudioEngine master voice : %d channels %d kHz 0x%x", details.InputChannels, details.InputSampleRate, masterMask);

    result->m_master->SetVolume(opts.masterVolume);

    // submix voices
    if (FAILED(hr = result->m_xaudio2->CreateSubmixVoice(&result->m_music, 2, opts.masteringRate)))
    {
        log::warning("AudioEngine : cannot initialize music mixer");
        return nullptr;
    }

    result->m_music->SetVolume(opts.musicVolume);

    if (FAILED(hr = result->m_xaudio2->CreateSubmixVoice(&result->m_effects, 2, opts.masteringRate)))
    {
        log::warning("AudioEngine : cannot initialize effects mixer");
        return nullptr;
    }
    result->m_effects->SetVolume(opts.effectsVolume);

    // 3D sound
    memset(result->m_X3Daudio, 0, X3DAUDIO_HANDLE_BYTESIZE);
    if (opts.use3D)
    {
        if (FAILED(hr = X3DAudioInitialize(masterMask, X3DAUDIO_SPEED_OF_SOUND, result->m_X3Daudio)))
        {
            log::warning("AudioEngine : cannot initialize 3D audio");
            return nullptr;
        }
        result->setListenerTransform(affine3::identity());
        result->m_listener.listen.pCone = nullptr;
    }

    result->startUpdateThread();

    return result;
}


void Xaudio2Implementation::setListenerTransform(affine3 const & transform)
{
    m_listener.transform = transform;
}

void Xaudio2Implementation::setListenerCallback(Engine::ListenerCallback const & callback)
{
    m_listenerCB = callback;
}

void Xaudio2Implementation::Listener::update(std::chrono::system_clock::time_point const & now, bool leftHanded)
{
    using namespace std::chrono;

    float dt = prevTime != system_clock::time_point() ?
        (float)0.001f * duration_cast<milliseconds>(now - prevTime).count() : 0.f;

    affine3 inv = inverse(transform.get());

    float3 prevP = prevPosition,
           curP = inv.m_translation,
           V = dt * (curP - prevP);

    float3 front = inv.transformVector({ 0.f, 0.f, -1.f }),
           up = inv.transformVector({ 0.f, 1.f, 0.f });

    if (leftHanded)
    {
        listen.Position    = X3DAUDIO_VECTOR(curP.x, curP.y, curP.z);
        listen.Velocity    = X3DAUDIO_VECTOR(V.x, V.y, V.z);
        listen.OrientFront = X3DAUDIO_VECTOR(front.x, front.y, front.z);
        listen.OrientTop   = X3DAUDIO_VECTOR(up.x, up.y, up.z);
    }
    else
    {
        listen.Position    = X3DAUDIO_VECTOR(curP.x, curP.y, -curP.z);
        listen.Velocity    = X3DAUDIO_VECTOR(V.x, V.y, -V.z);
        listen.OrientFront = X3DAUDIO_VECTOR(front.x, front.y, -front.z);
        listen.OrientTop   = X3DAUDIO_VECTOR(up.x, up.y, -up.z);
    }
    prevPosition = curP;
    prevTime = now;
}

#endif

//
// Engine PIMPL
//

Engine::Engine(Options opts)
{
#ifdef WIN32
    m_implementation = Xaudio2Implementation::create(opts);
#else
    log::warning("Audio not supported on this platform");
#endif
}

Engine::~Engine()
{ }

std::weak_ptr<Effect> Engine::playEffect(EffectDesc const & desc)
{
    std::weak_ptr<Effect> effect;
    if (m_implementation)
        effect = m_implementation->playEffect(desc);
    return effect;
}

std::weak_ptr<Effect> Engine::playMusic(std::shared_ptr<AudioData const> song, float crossfade)
{
    std::weak_ptr<Effect> effect;
    if (m_implementation)
        effect = m_implementation->playMusic(song, crossfade);
    return effect;
}

bool Engine::crossfadeActive() const
{
    if (m_implementation)
        return m_implementation->crossfadeActive();
    return false;
}


void Engine::setMasterVolume(float volume)
{
    if (m_implementation)
        m_implementation->setMasterVolume(volume);
}

void Engine::setEffectsVolume(float volume)
{
    if (m_implementation)
        m_implementation->setEffectsVolume(volume);
}

void Engine::setMusicVolume(float volume)
{
    if (m_implementation)
        m_implementation->setMusicVolume(volume);
}

bool Engine::startUpdateThread()
{
    if (m_implementation)
        return m_implementation->startUpdateThread();
    return false;
}

void Engine::stopUpdateThread()
{
    if (m_implementation)
        m_implementation->stopUpdateThread();
}

void Engine::setListenerTransform(affine3 const & transform)
{
    if (m_implementation)
        m_implementation->setListenerTransform(transform);
}

void Engine::setListenerCallback(ListenerCallback const & callback)
{
    if (m_implementation)
        m_implementation->setListenerCallback(callback);
}

} // namespace donut::engine::audio
