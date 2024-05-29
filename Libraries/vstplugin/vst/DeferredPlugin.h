#pragma once

#include "Interface.h"
#include "PluginCommand.h"

#include <cstring>

namespace vst {

class DeferredPlugin : public IPlugin {
 public:
    void setParameter(int index, float value, int sampleOffset) override {
        Command command(Command::SetParamValue);
        auto& param = command.paramValue;
        param.index = index;
        param.value = value;
        param.offset = sampleOffset;
        pushCommand(command);
    }

    bool setParameter(int index, std::string_view str, int sampleOffset) override {
        auto size = str.size();
        if (size > Command::maxShortStringSize) {
            // bummer, we need to allocate on the heap
            auto buf = new char[size + 1];
            memcpy(buf, str.data(), size + 1);

            Command command(Command::SetParamString);
            auto& param = command.paramString;
            param.offset = sampleOffset;
            param.index = index;
            param.size = size;
            param.str = buf;

            pushCommand(command);
        } else {
            Command command(Command::SetParamStringShort);
            auto& param = command.paramStringShort;
            param.offset = sampleOffset;
            param.index = index;
            // pascal string!!
            param.pstr[0] = (uint8_t)size;
            memcpy(&param.pstr[1], str.data(), size);

            pushCommand(command);
        }

        return true; // what shall we do?
    }

    void setBypass(Bypass state) override {
        Command command(Command::SetBypass);
        command.i = static_cast<int32_t>(state);
        pushCommand(command);
    }

    void setProgram(int program) override {
        Command command(Command::SetProgram);
        command.i = program;
        pushCommand(command);
    }

    void sendMidiEvent(const MidiEvent& event) override {
        Command command(Command::SendMidi);
        auto& midi = command.midi;
        memcpy(midi.data, event.data, sizeof(event.data));
        midi.delta = event.delta;
        midi.detune = event.detune;
        pushCommand(command);
    }

    void sendSysexEvent(const SysexEvent& event) override {
        // copy data (LATER improve)
        auto data = new char[event.size];
        memcpy(data, event.data, event.size);

        Command command(Command::SendSysex);
        auto& sysex = command.sysex;
        sysex.data = data;
        sysex.size = event.size;
        sysex.delta = event.delta;
        pushCommand(command);
    }

    void setTempoBPM(double tempo) override {
        Command command(Command::SetTempo);
        command.d = tempo;
        pushCommand(command);
    }

    void setTimeSignature(int numerator, int denominator) override {
        Command command(Command::SetTimeSignature);
        command.timeSig.num = numerator;
        command.timeSig.denom = denominator;
        pushCommand(command);
    }

    void setTransportPlaying(bool play) override {
        Command command(Command::SetTransportPlaying);
        command.i = play;
        pushCommand(command);
    }

    void setTransportRecording(bool record) override {
        Command command(Command::SetTransportRecording);
        command.i = record;
        pushCommand(command);
    }

    void setTransportAutomationWriting(bool writing) override {
        Command command(Command::SetTransportAutomationWriting);
        command.i = writing;
        pushCommand(command);
    }

    void setTransportAutomationReading(bool reading) override {
        Command command(Command::SetTransportAutomationReading);
        command.i = reading;
        pushCommand(command);
    }

    void setTransportCycleActive(bool active) override {
        Command command(Command::SetTransportCycleActive);
        command.i = active;
        pushCommand(command);
    }

    void setTransportCycleStart(double beat) override {
        Command command(Command::SetTransportCycleStart);
        command.d = beat;
        pushCommand(command);
    }

    void setTransportCycleEnd(double beat) override {
        Command command(Command::SetTransportCycleEnd);
        command.d = beat;
        pushCommand(command);
    }

    void setTransportPosition(double beat) override {
        Command command(Command::SetTransportPosition);
        command.d = beat;
        pushCommand(command);
    }
 protected:
    virtual void pushCommand(const Command& command) = 0;
};

} // vst
