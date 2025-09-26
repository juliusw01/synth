#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <cmath>
#include <locale>
#include <codecvt>
#include <portaudio.h>

#ifndef FTYPE
#define FTYPE double
#endif

const FTYPE PI = 2.0 * acos(0.0);

template<class T>
class olcNoiseMaker
{
public:
    olcNoiseMaker(const std::wstring &sOutputDevice, unsigned int sampleRate = 44100, unsigned int channels = 1, unsigned int blocks = 8, unsigned int blockSamples = 512)
        : m_nSampleRate(sampleRate), m_nChannels(channels), m_nBlockSamples(blockSamples)
    {
        m_bReady = false;
        m_pCallback = nullptr;

        // Starte PortAudio
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cerr << "PortAudio Init Fehler: " << Pa_GetErrorText(err) << std::endl;
        }

        err = Pa_OpenDefaultStream(&m_stream, 0, m_nChannels, paFloat32, m_nSampleRate, m_nBlockSamples, &StaticCallback, this);
        if (err != paNoError) {
            std::cerr << "PortAudio Stream Fehler: " << Pa_GetErrorText(err) << std::endl;
        }
        Pa_StartStream(m_stream);
        m_bReady = true;
    }

    ~olcNoiseMaker()
    {
        m_bReady = false;
        Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        Pa_Terminate();
    }

    static std::vector<std::wstring> Enumerate()
    {
        std::vector<std::wstring> devices;
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter; //deprecated
        Pa_Initialize();
        int numDevices = Pa_GetDeviceCount();
        for (int i = 0; i < numDevices; i++)
        {
            const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
            //devices.push_back(std::wstring(info->name, info->name + strlen(info->name)));
            devices.push_back(converter.from_bytes(info->name));
        }
        Pa_Terminate();
        return devices;
    }

    void SetUserFunction(FTYPE(*func)(int, FTYPE))
    {
        m_pCallback = func;
    }

    FTYPE GetTime()
    {
        return m_dGlobalTime;
    }

private:
    static int StaticCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData)
    {
        return static_cast<olcNoiseMaker *>(userData)->ProcessAudio(outputBuffer, framesPerBuffer);
    }

    int ProcessAudio(void *outputBuffer, unsigned long framesPerBuffer)
    {
        if (!m_pCallback) return 0;

        float *out = (float *)outputBuffer;
        for (unsigned long i = 0; i < framesPerBuffer; i++)
        {
            *out++ = m_pCallback(m_nChannels, m_dGlobalTime);
            m_dGlobalTime += 1.0 / (FTYPE)m_nSampleRate;
        }
        return 0;
    }

    unsigned int m_nSampleRate;
    unsigned int m_nChannels;
    unsigned int m_nBlockSamples;
    bool m_bReady;
    PaStream *m_stream;
    T (*m_pCallback)(int, FTYPE);
    FTYPE m_dGlobalTime{0.0};
};
