#ifndef AUDIOINPUT_H
#define AUDIOINPUT_H
#pragma once

#include <iostream>
#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <portaudio.h>
#include "hacktv/rf.h"
#include <thread>  // For std::this_thread::sleep_for
#include <chrono>  // For std::chrono::milliseconds
#include "types.h"
#include "stream_tx.h"
#include "modulation.h"

class PortAudioInput : public QObject
{
    Q_OBJECT

public:
    explicit PortAudioInput(QObject *parent = nullptr, rf_t* rf = nullptr)
        : QObject(parent), stream(nullptr), isRunning(false), rf_ptr(rf)
    {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cout<< "PortAudio initialization error:" << Pa_GetErrorText(err) << std::endl;
        }
    }

    ~PortAudioInput()
    {
        stop();
        Pa_Terminate();
    }

    bool start()
    {
        if (isRunning) {
            return false; // Already running
        }

        // Open an input stream
        PaError err = Pa_OpenDefaultStream(&stream,
                                           1,                  // Number of input channels
                                           0,                  // Number of output channels
                                           paFloat32,          // Sample format
                                           44100,              // Sample rate
                                           4096,                // Frames per buffer
                                           audioCallback,      // Callback function
                                           this);              // User data
        if (err != paNoError) {
            std::cout << "PortAudio stream error:" << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        err = Pa_StartStream(stream);
        if (err != paNoError) {
            std::cout << "PortAudio stream start error:" << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        isRunning = true;
        std::cout<< "PortAudio initialized" << std::endl;
        return true;
    }

    void stop()
    {
        if (!isRunning) {
            return;
        }

        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        stream = nullptr;
        std::cout<< "PortAudio stopped" << std::endl;
        isRunning = false;
    }

    std::vector<float> readStreamToSize(size_t size) {
        std::vector<float> float_buffer;
        float_buffer.reserve(size);

        while (float_buffer.size() < size) {
            std::vector<float> temp_buffer = stream_tx.readBufferToVector();
            if (temp_buffer.empty()) {
                // Add a sleep or yield to avoid busy-waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                size_t elements_needed = size - float_buffer.size();
                size_t elements_to_add = (elements_needed < temp_buffer.size()) ? elements_needed : temp_buffer.size();
                float_buffer.insert(float_buffer.end(), temp_buffer.begin(), temp_buffer.begin() + elements_to_add);
            }
        }
        return float_buffer;
    }

    int apply_modulation(int8_t* buffer, uint32_t length)
    {
        int decimation = 1;
        float amplitude = 1.0;
        float modulation_index = 5.0;
        float interpolation = 48;
        float filter_size = 0.0;

        size_t desired_size = length / 2;
        std::vector<float> float_buffer = readStreamToSize(desired_size);

        if (float_buffer.size() < desired_size) {
            return 0;
        }

        int noutput_items = float_buffer.size();
        for (int i = 0; i < noutput_items; ++i) {
            float_buffer[i] *= amplitude;
        }

        std::vector<std::complex<float>> modulated_signal(noutput_items);
        float sensitivity = modulation_index;
        FrequencyModulator modulator(sensitivity);
        modulator.work(noutput_items, float_buffer, modulated_signal);

        RationalResampler resampler(interpolation, decimation, filter_size);
        std::vector<std::complex<float>> resampled_signal = resampler.resample(modulated_signal);

        for (int i = 0; i < noutput_items; ++i) {
            buffer[2 * i] = static_cast<int8_t>(std::real(resampled_signal[i]) * 127.0f);
            buffer[2 * i + 1] = static_cast<int8_t>(std::imag(resampled_signal[i]) * 127.0f);
        }
        return 0;
    }

private:
    static int audioCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData)
    {
        PortAudioInput *paInput = static_cast<PortAudioInput*>(userData);
        if (inputBuffer == nullptr) {
            std::cerr << "audioCallback: inputBuffer is null!" << std::endl;
            return paContinue;
        }

        std::memcpy(paInput->stream_tx.writeBuf, inputBuffer, framesPerBuffer * sizeof(dsp::complex_tx));
        paInput->stream_tx.swap(framesPerBuffer);
        return paContinue;
    }

    PaStream *stream;
    bool isRunning;
    QMutex m_mutex;
    QWaitCondition m_bufferNotEmpty;
    rf_t* rf_ptr;
    dsp::stream_tx<dsp::complex_tx> stream_tx;
};

#endif // AUDIOINPUT_H
