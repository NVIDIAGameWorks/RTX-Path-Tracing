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

#include <donut/core/math/math.h>
#include <functional>
#include <memory>

namespace donut::engine::audio
{
class AudioData;

// Effect : transient interface to manipulate active sound effects
//
struct Effect
{
    // returns the audio sample associated with this effect
    virtual std::weak_ptr<AudioData const> getSample() const = 0;    
    
    virtual void setVolume(float volume) = 0;
    virtual void setPitch(float volume) = 0;
    virtual void setPan(float pan) = 0;

    virtual void pause() = 0; // pause playback & don't release voice to the pool
    virtual void stop() = 0; // permanently stops playback & release voice to the pool
    virtual float played() = 0; // duration of sample portion played (in seconds) ; -1.f if not playing

    // update 3D transform of this emitter ; can be set asynchronously
    // (returns false if effect was not set as 3D)
    virtual bool setEmitterTransform(donut::math::affine3 const & transform) = 0;

    // Effect update callback (see engine update thread)
    typedef std::function<void(Effect &)> EffectCallback;
    virtual void setEffectCallback(EffectCallback const & callback) = 0;

    virtual ~Effect() {};
};

// Descriptor used to create effects
//
struct EffectDesc
{
    std::shared_ptr<AudioData const> sample; // cached audio sample

    float volume = 1.f, // default volume / pitch / pan
          pitch = 1.f,
          pan = 0.f;

    uint32_t loop = false; // play once or repeat (up to Engine::infinite_loop)

    // if set, creates a 3D omnidirectional sound emitter at the position set by
    // the affine3 translation (see Effect::setEmitterTransform)
    donut::math::affine3 const * transform = nullptr;

    // if set, triggers the callback function to be called every tick of the
    // engine update (see Engine update thread)
    Effect::EffectCallback updateCB;
};

struct Options
{
    float masterVolume = 1.f,       // default volume settings
          effectsVolume = 1.f,      // default volume for the effects mixing track
          musicVolume = 1.f;        // default volume for the music mixing track

    bool leftHanded = true,         // swap Z direction to match graphics API
         use3D = false;             // enable positional sound for effects

    uint32_t masteringRate = 44100, // master voice mixing rate hint (in Hz)
             updateRate = 30,       // engine update thread tick rate (in Hz)
             maxVoices = 64;        // max number of mixing voices at once
};

// Audio Engine : interface to play audio samples on rendering hardware.
// The audio engine maintains 2 submix tracks (effects & music), both outputting
// independently to a mastering track.
//
// Queuing 'play' functions take audio::AudioData samples and return an interface
// to the active voice that plays the sample in the form of a std::weak_ptr<Effect>
//
// While the sample is playing, the client application can use the Effect 
// interface to safely control the playback rendering hardware.
// Note : client applications should always release locks as quickly as possible !
//
// Effect tracks are intended for sound effects and can be mixed spatially in 3D.
//
// Music tracks are intended for a continuous stereo music score : they do not
// support 3D, but the have the ability to transition smoothly between songs with
// a linear cross-fade.
//
// For improved CPU performance, the engine manages hardware audio tracks with
// an asynchronous voice pool. The pool recycles inactive voices at a fixed time
// rate in a parallel thread.
//
class Engine
{
public:
    typedef donut::math::affine3 affine3;

    Engine(Options options=Options());

    ~Engine();
    
    static uint32_t constexpr infinite_loop = 255; // constant for 'infinite' looping

    // plays an audio sample on the effects mixing track
    std::weak_ptr<Effect> playEffect(EffectDesc const & desc);

    // plays a song on the music mixing track
    std::weak_ptr<Effect> playMusic(std::shared_ptr<AudioData const> song, float crossfade = 2.f);

    // returns true the engine is transitioning (cross-fading) between 2 songs, false otherwise
    bool crossfadeActive() const;

    // adjust mixing track volumes
    void setMasterVolume(float volume);
    void setEffectsVolume(float volume);
    void setMusicVolume(float volume);

    // update the listener transform (can be called asynchronously)
    void setListenerTransform(affine3 const & transform);

    // Listener update callback (see engine update thread)
    typedef std::function<void()> ListenerCallback;
    void setListenerCallback(ListenerCallback const & callback);

    // the engine update thread manages the voice pool & computes 3D audio mix rates
    // it can also trigger the execution of callback functions for the effects and the
    // 3D listener
    bool startUpdateThread();
    void stopUpdateThread();

    // platform-specific engine implementation
    class Implementation;
    std::unique_ptr<Implementation> m_implementation;
};

} // namespace donut::engine::audio
