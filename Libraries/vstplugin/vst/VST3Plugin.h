#pragma once

#include "Interface.h"
#include "PluginFactory.h"
#include "Lockfree.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstautomationstate.h"
#include "pluginterfaces/vst/ivstrepresentation.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "pluginterfaces/vst/ivstcontextmenu.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "pluginterfaces/vst/ivstprefetchablesupport.h"
#include "pluginterfaces/vst/ivstphysicalui.h"
#include "pluginterfaces/vst/ivstmidilearn.h"

#include "pluginterfaces/gui/iplugview.h"

#include <atomic>
#include <climits>
#include <unordered_map>

#ifndef VST_3_7_0_VERSION
# define VST_3_7_0_VERSION 0x030700
#endif

namespace Steinberg {
namespace Vst {
// copied from public.sdk/vst/vstpresetfile.h
using ChunkID = char[4];

enum ChunkType
{
    kHeader,
    kComponentState,
    kControllerState,
    kProgramData,
    kMetaInfo,
    kChunkList,
    kNumPresetChunks
};

const ChunkID& getChunkID (ChunkType type);

} // Vst
} // Steinberg

using namespace Steinberg;

#if !SMTG_PLATFORM_64 && defined(__WINE__)
// There is an important ABI difference between 32-bit Linux
// (gcc, winegcc) and MSVC/MinGW regarding struct layout:
// The former uses 4 byte alignment for 64-bit data types,
// like double or int64_t, while the latter uses 8 bytes.
//
// This is not a problem if the data structures are carefully
// designed (i.e. large data members first), but the VST SDK
// doesn't bother and instead wrongly relies on #pragma pack
// (which only specifies the *minimum* alignment).
//
// If a struct has a different layout, we can't safely pass it to
// a Windows plugin from our Linux/winelib host.
// Our solution is to use our own structs with additional padding
// members to match the expected struct layout.
//
// NOTE: we could also compile the wine host with -malign-double
// but this can have unexpected consequences when interfacing
// with existing Linux code.
//
// Afaict, the VST2 SDK is not affected. 64-bit integers are not
// used anywhere and only few structs have double members:
// * VstTimeInfo: all doubles come come first.
// * VstOfflineTask and VstAudioFile: problematic, but not used.
//
// The VST3 SDK, on the other hand, *is* affected:
// Broken:
// * Vst::ProcessSetup
// * Vst::AudioBusBuffers
// * Vst::ProcessContext
// Ok:
// * Vst::ParameterInfo
// * Vst::Event
// Probably fine, but not used (yet):
// * Vst::NoteExpressionValueDescription
// * Vst::NoteExpressionValueEvent
// * Vst::NoteExpressionTypeInfo

namespace vst3 {

// Vst::ProcessSetup
struct ProcessSetup {
    int32 processMode;
    int32 symbolicSampleSize;
    int32 maxSamplesPerBlock;
    int32 padding;
    Vst::SampleRate sampleRate;
} __attribute__((packed, aligned(8) ));

static_assert(sizeof(ProcessSetup) == 24,
              "unexpected size for ProcessSetup");

// Vst::AudioBusBuffers
struct AudioBusBuffers {
    int32 numChannels;
    int32 padding1;
    uint64 silenceFlags;
    union
    {
        Vst::Sample32** channelBuffers32;
        Vst::Sample64** channelBuffers64;
    };
    int32 padding2;
} __attribute__((packed, aligned(8) ));

static_assert(sizeof(AudioBusBuffers) == 24,
              "unexpected size for AudioBusBuffers");

struct ProcessContext
{
    uint32 state;
    int32 padding1;
    double sampleRate;
    Vst::TSamples projectTimeSamples;
    int64 systemTime;
    Vst::TSamples continousTimeSamples;
    Vst::TQuarterNotes projectTimeMusic;
    Vst::TQuarterNotes barPositionMusic;
    Vst::TQuarterNotes cycleStartMusic;
    Vst::TQuarterNotes cycleEndMusic;
    double tempo;
    int32 timeSigNumerator;
    int32 timeSigDenominator;
    Vst::Chord chord;
    int32 smpteOffsetSubframes;
    Vst::FrameRate frameRate;
    int32 samplesToNextClock;
    int32 padding2;
} __attribute__((packed, aligned(8) ));

static_assert(sizeof(ProcessContext) == 112,
              "unexpected size for ProcessContext");

// Vst::ProcessData
// This one is only used to avoid casts between
// Vst::AudioBusBuffers <-> AudioBusBuffers and
// Vst::ProcessContext <-> ProcessContext
struct ProcessData
{
    int32 processMode;
    int32 symbolicSampleSize;
    int32 numSamples;
    int32 numInputs;
    int32 numOutputs;
    AudioBusBuffers* inputs;
    AudioBusBuffers* outputs;

    Vst::IParameterChanges* inputParameterChanges;
    Vst::IParameterChanges* outputParameterChanges;
    Vst::IEventList* inputEvents;
    Vst::IEventList* outputEvents;
    ProcessContext* processContext;
};

static_assert(sizeof(ProcessData) == 48,
              "unexpected size for ProcessData");

} // vst3

#else // 32-bit Wine

namespace vst3 {
using ProcessSetup = Vst::ProcessSetup;
using AudioBusBuffers = Vst::AudioBusBuffers;
using ProcessData = Vst::ProcessData;
using ProcessContext = Vst::ProcessContext;
} // vst3

// verify struct sizes

// these structs generally differ between 32-bit and 64-bit:
# if SMTG_PLATFORM_64
static_assert(sizeof(Vst::ProcessData) == 80,
              "unexpected size for Vst::ProcessData");
# else
static_assert(sizeof(Vst::ProcessData) == 48,
              "unexpected size for Vst::ProcessData");
# endif

// these structs are only different on x86 Linux/macOS (x86 System V):
# if SMTG_CPU_X86 && !SMTG_OS_WINDOWS
static_assert(sizeof(Vst::ProcessSetup) == 20,
              "unexpected size for Vst::ProcessSetup");
static_assert(sizeof(Vst::AudioBusBuffers) == 16,
              "unexpected size for Vst::AudioBusBuffers");
static_assert(sizeof(Vst::ProcessContext) == 104,
              "unexpected size for Vst::ProcessContext");
# else // amd64, win32, arm and arm64
static_assert(sizeof(Vst::ProcessSetup) == 24,
              "unexpected size for Vst::ProcessSetup");
static_assert(sizeof(Vst::AudioBusBuffers) == 24,
              "unexpected size for Vst::AudioBusBuffers");
static_assert(sizeof(Vst::ProcessContext) == 112,
              "unexpected size for Vst::ProcessContext");
# endif

#endif // 32-bit Wine


#define MY_IMPLEMENT_QUERYINTERFACE(Interface)                            \
tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override  \
{                                                                         \
    QUERY_INTERFACE (_iid, obj, FUnknown::iid, Interface)                 \
    QUERY_INTERFACE (_iid, obj, Interface::iid, Interface)                \
    *obj = nullptr;                                                       \
    return kNoInterface;                                                  \
}

#define DUMMY_REFCOUNT_METHODS                     \
uint32 PLUGIN_API addRef() override { return 1; }  \
uint32 PLUGIN_API release() override { return 1; } \


#define MY_REFCOUNT_METHODS(BaseClass) \
uint32 PLUGIN_API addRef() override { return BaseClass::addRef (); } \
uint32 PLUGIN_API release()override { return BaseClass::release (); }


namespace vst {

class VST3Factory final : public PluginFactory {
 public:
    VST3Factory(const std::string& path, bool probe);
    ~VST3Factory();
    // probe a single plugin
    PluginDesc::const_ptr probePlugin(int id) const override;
    // create a new plugin instance
    IPlugin::ptr create(const std::string& name, bool editor) const override;
 private:
    void doLoad();
    IPtr<IPluginFactory> factory_;
    // TODO dllExit
    // subplugins
    PluginDesc::SubPluginList subPlugins_;
    std::unordered_map<std::string, int> subPluginMap_;
};

//----------------------------------------------------------------------

#ifndef USE_MULTI_POINT_AUTOMATION
#define USE_MULTI_POINT_AUTOMATION 0
#endif

#if USE_MULTI_POINT_AUTOMATION
class ParamValueQueue: public Vst::IParamValueQueue {
 public:
    static const int maxNumPoints = 64;

    ParamValueQueue();

    MY_IMPLEMENT_QUERYINTERFACE(Vst::IParamValueQueue)
    DUMMY_REFCOUNT_METHODS

    void setParameterId(Vst::ParamID id);
    Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return values_.size(); }
    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, Vst::ParamValue& value) override;
    tresult PLUGIN_API addPoint (int32 sampleOffset, Vst::ParamValue value, int32& index) override;
 protected:
    struct Value {
        Value(Vst::ParamValue v, int32 offset) : value(v), sampleOffset(offset) {}
        Vst::ParamValue value;
        int32 sampleOffset;
    };
    std::vector<Value> values_;
    Vst::ParamID id_ = Vst::kNoParamId;
};
#else
class ParamValueQueue: public Vst::IParamValueQueue {
 public:
    MY_IMPLEMENT_QUERYINTERFACE(Vst::IParamValueQueue)
    DUMMY_REFCOUNT_METHODS

    void setParameterId(Vst::ParamID id){
        id_ = id;
    }
    Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, Vst::ParamValue& value) override {
        sampleOffset = sampleOffset_;
        value = value_;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint (int32 sampleOffset, Vst::ParamValue value, int32& index) override {
        sampleOffset_ = sampleOffset;
        value_ = value;
        index = 0;
        return kResultOk;
    }
 protected:
    Vst::ParamID id_ = Vst::kNoParamId;
    int32 sampleOffset_;
    Vst::ParamValue value_;
};
#endif

//----------------------------------------------------------------------
class ParameterChanges: public Vst::IParameterChanges {
 public:
    MY_IMPLEMENT_QUERYINTERFACE(Vst::IParameterChanges)
    DUMMY_REFCOUNT_METHODS

    void setMaxNumParameters(int n){
        parameterChanges_.resize(n);
    }
    int32 PLUGIN_API getParameterCount() override {
        return useCount_;
    }
    Vst::IParamValueQueue* PLUGIN_API getParameterData(int32 index) override;
    Vst::IParamValueQueue* PLUGIN_API addParameterData(const Vst::ParamID& id, int32& index) override;
    void clear(){
        useCount_ = 0;
    }
 protected:
    std::vector<ParamValueQueue> parameterChanges_;
    int useCount_ = 0;
};

//--------------------------------------------------------------------------------

class EventList : public Vst::IEventList {
 public:
    static const int maxNumEvents = 64;

    EventList();
    ~EventList();

    MY_IMPLEMENT_QUERYINTERFACE(Vst::IEventList)
    DUMMY_REFCOUNT_METHODS

    int32 PLUGIN_API getEventCount() override;
    tresult PLUGIN_API getEvent(int32 index, Vst::Event& e) override;
    tresult PLUGIN_API addEvent (Vst::Event& e) override;
    void addSysexEvent(const SysexEvent& event);
    void clear();
 protected:
    std::vector<Vst::Event> events_;
    std::vector<std::string> sysexEvents_;
};

//--------------------------------------------------------------------------------------------------------

class VST3Plugin final :
        public IPlugin,
        public Vst::IComponentHandler,
        public Vst::IConnectionPoint,
    #if VST_VERSION >= VST_3_7_0_VERSION
        public Vst::IProgress,
    #endif
        public IPlugFrame
    #if SMTG_OS_LINUX
        , public Linux::IRunLoop
    #endif
{
 public:
    VST3Plugin(IPtr<IPluginFactory> factory, int which, IFactory::const_ptr f,
               PluginDesc::const_ptr desc, bool editor);
    ~VST3Plugin();

    tresult PLUGIN_API queryInterface (const TUID _iid, void** obj) override {
        QUERY_INTERFACE (_iid, obj, FUnknown::iid, Vst::IComponentHandler)
        QUERY_INTERFACE (_iid, obj, Vst::IComponentHandler::iid, Vst::IComponentHandler)
        QUERY_INTERFACE (_iid, obj, IPlugFrame::iid, IPlugFrame)
    #if SMTG_OS_LINUX
        QUERY_INTERFACE (_iid, obj, Linux::IRunLoop::iid, Linux::IRunLoop)
    #endif
        *obj = nullptr;
        return kNoInterface;
    }
    DUMMY_REFCOUNT_METHODS

    // IComponentHandler
    tresult PLUGIN_API beginEdit(Vst::ParamID id) override;
    tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue value) override;
    tresult PLUGIN_API endEdit(Vst::ParamID id) override;
    tresult PLUGIN_API restartComponent(int32 flags) override;

    // IConnectionPoint
    tresult PLUGIN_API connect(Vst::IConnectionPoint* other) override;
    tresult PLUGIN_API disconnect(Vst::IConnectionPoint* other) override;
    tresult PLUGIN_API notify(Vst::IMessage* message) override;

    // IProgress
#if VST_VERSION >= VST_3_7_0_VERSION
    tresult PLUGIN_API start(ProgressType type, const tchar *description, ID& id) override;
    tresult PLUGIN_API update(ID id, Vst::ParamValue value) override;
    tresult PLUGIN_API finish(ID id) override;
#endif

    // IPlugFrame
    tresult PLUGIN_API resizeView (IPlugView* view, ViewRect* newSize) override;

#if SMTG_OS_LINUX
    // IRunLoop
    tresult PLUGIN_API registerEventHandler (Linux::IEventHandler* handler,
                                             Linux::FileDescriptor fd) override;
    tresult PLUGIN_API unregisterEventHandler (Linux::IEventHandler* handler) override;
    tresult PLUGIN_API registerTimer (Linux::ITimerHandler* handler,
                                      Linux::TimerInterval milliseconds) override;
    tresult PLUGIN_API unregisterTimer (Linux::ITimerHandler* handler) override;
#endif

    const PluginDesc& info() const override { return *info_; }
    PluginDesc::const_ptr getInfo() const { return info_; }

    void setupProcessing(double sampleRate, int maxBlockSize,
                         ProcessPrecision precision, ProcessMode mode) override;
    void process(ProcessData& data) override;
    void suspend() override;
    void resume() override;
    void setBypass(Bypass state) override;
    void setNumSpeakers(int *input, int numInputs, int *output, int numOutputs) override;
    int getLatencySamples() override;

    void setListener(IPluginListener* listener) override {
        listener_ = listener;
    }

    void setTempoBPM(double tempo) override;
    void setTimeSignature(int numerator, int denominator) override;
    void setTransportPlaying(bool play) override;
    void setTransportRecording(bool record) override;
    void setTransportAutomationWriting(bool writing) override;
    void setTransportAutomationReading(bool reading) override;
    void setTransportCycleActive(bool active) override;
    void setTransportCycleStart(double beat) override;
    void setTransportCycleEnd(double beat) override;
    void setTransportPosition(double beat) override;
    double getTransportPosition() const override;

    void sendMidiEvent(const MidiEvent& event) override;
    void sendSysexEvent(const SysexEvent& event) override;

    void setParameter(int index, float value, int sampleOffset = 0) override;
    bool setParameter(int index, std::string_view str, int sampleOffset = 0) override;
    float getParameter(int index) const override;
    size_t getParameterString(int index, ParamStringBuffer& buffer) const override;

    void setProgram(int program) override;
    void setProgramName(std::string_view name) override;
    int getProgram() const override;
    std::string getProgramName() const override;
    std::string getProgramNameIndexed(int index) const override;

    void readProgramFile(const std::string& path) override;
    void readProgramData(const char *data, size_t size) override;
    void writeProgramFile(const std::string& path) override;
    void writeProgramData(std::string& buffer) override;
    void readBankFile(const std::string& path) override;
    void readBankData(const char *data, size_t size) override;
    void writeBankFile(const std::string& path) override;
    void writeBankData(std::string& buffer) override;

    void openEditor(void *window) override;
    void closeEditor() override;
    bool getEditorRect(Rect& rect) const override;
    void updateEditor() override;
    void checkEditorSize(int &width, int &height) const override;
    void resizeEditor(int width, int height) override;

    IWindow *getWindow() const override {
        return window_.get();
    }

    void handleUIParamChange(Vst::ParamID id, Vst::ParamValue value);
 private:
    int getNumParameters() const;
    int getNumPrograms() const;
    bool checkEditor();
    bool checkEditorResizable();
    bool hasPrecision(ProcessPrecision precision) const;
    bool hasTail() const;
    int getTailSize() const;
    bool hasBypass() const;

    template<typename T>
    void doProcess(ProcessData& inData);
    template<typename T>
    void bypassProcess(ProcessData& inData, vst3::ProcessData& data,
                       Bypass state, bool ramp);
    void handleEvents();
    void handleOutputParameterChanges();
    void sendMessage(Vst::IMessage* msg);
    void doSetParameter(Vst::ParamID, float value, int32 sampleOffset = 0);
    void doSetProgram(int program);
    void setCacheParameter(int index, float value, bool notify);
    void updateParameterCache();
    void createViewLazy(bool nullOk = false);

    // NB: factory_ must be the first member, so it gets destructed last!
    IFactory::const_ptr factory_; // keep alive!
    PluginDesc::const_ptr info_;

    IPtr<Vst::IComponent> component_;
    IPtr<Vst::IEditController> controller_;
    FUnknownPtr<Vst::IAudioProcessor> processor_;
    // audio
    vst3::ProcessContext context_;
    // automation
    static constexpr uint32_t kAutomationStateChanged = 0x80000000;
    std::atomic<uint32_t> automationState_{0};
    // bypass
    Bypass bypass_ = Bypass::Off;
    Bypass lastBypass_ = Bypass::Off;
    bool bypassSilent_ = false; // check if we can stop processing
    ProcessMode mode_ = ProcessMode::Realtime;
    // midi
    EventList inputEvents_;
    EventList outputEvents_;
    // parameters
    ParameterChanges inputParamChanges_;
    ParameterChanges outputParamChanges_;

    // The parameter cache has two main purposes:
    // 1. cache the parameter, so that the host can easily retrieve it
    // from the audio thread with getParameter()
    // 2. allow the UI to update parameters. Although  we *could* use
    // paramChangesToGui for this purpose, this would be tricky regarding
    // memory management, because we do not know in advance how large
    // the queue should be.
    std::unique_ptr<std::atomic<float>[]> paramCache_;
    // This is an atomic bitset vector which is used to tell the UI thread
    // which parameters have changed. We do not need another atomic variable
    // to indicate whether *any* parameter has changed; instead, we can simply
    // loop over the vector and check if any bin is not 0.
    // (For example, a plugin with 500 parameters would only have 8 bins.)
    static constexpr size_t paramCacheBits = sizeof(size_t) * CHAR_BIT;
    std::unique_ptr<std::atomic<size_t>[]> paramCacheBins_;
    size_t numParamCacheBins_ = 0;

    struct ParamChange {
        ParamChange() : index(0), id(0), value(0) {}
        ParamChange(int32_t _index, Vst::ParamID _id, Vst::ParamValue _value)
            : index(_index), id(_id), value(_value) {}
        int32_t index;
        Vst::ParamID id;
        Vst::ParamValue value;
    };
    UnboundedMPSCQueue<ParamChange> paramChangesFromGui_;
    // programs
    int program_ = 0;
    // UI
    bool editorOpen_ = false;
    uint32_t uniqueId_ = 0;
    IPtr<IPlugView> view_;
    IWindow::ptr window_;
    IPluginListener* listener_ = nullptr;
};

//--------------------------------------------------------------------------------

// Steinberg's FUID class is not header-only, so we make our own.
struct FUID {
    static const int32 kStringLen = 32; // length of ASCII-encoded FUID

    FUID() {
        memset(uid, 0, sizeof(TUID));
    }
    FUID(const TUID iid) {
        memcpy(uid, iid, sizeof(TUID));
    }

    bool operator==(const TUID iid) const {
        return memcmp(uid, iid, sizeof(TUID)) == 0;
    }
    bool operator!=(const TUID iid) const {
        return !(*this == iid);
    }

    void toString(char *buffer, size_t size);

    std::string toString() {
        char buffer[33];
        toString(buffer, 33);
        return buffer;
    }

    static void fromString(const char *s, TUID tuid);

    void fromString(const char *s) {
        fromString(s, uid);
    }

    TUID uid;
};

//--------------------------------------------------------------------------------

class BaseStream : public IBStream {
 public:
    MY_IMPLEMENT_QUERYINTERFACE(IBStream)
    DUMMY_REFCOUNT_METHODS
    // IBStream
    tresult PLUGIN_API read  (void* buffer, int32 numBytes, int32* numBytesRead) override;
    tresult PLUGIN_API tell  (int64* pos) override;
    virtual const char *data() const = 0;
    virtual size_t size() const = 0;
    void setPos(int64 pos);
    int64 getPos() const;
    void rewind();
    bool writeInt32(int32 i);
    bool writeInt64(int64 i);
    bool writeChunkID(const Vst::ChunkID id);
    bool writeTUID(const TUID tuid);
    bool readInt32(int32& i);
    bool readInt64(int64& i);
    bool readChunkID(Vst::ChunkID id);
    bool readTUID(TUID tuid);
 protected:
    tresult doSeek(int64 pos, int32 mode, int64 *result, bool resize);
    template<typename T>
    bool doWrite(const T& t);
    template<typename T>
    bool doRead(T& t);
    int64_t cursor_ = 0;
};

//-----------------------------------------------------------------------------

class StreamView : public BaseStream {
 public:
    StreamView() = default;
    StreamView(const char *data, size_t size);
    void assign(const char *data, size_t size);
    // IBStream
    tresult PLUGIN_API seek  (int64 pos, int32 mode, int64* result) override;
    tresult PLUGIN_API write (void* buffer, int32 numBytes, int32* numBytesWritten) override;
    const char * data() const override { return data_; }
    size_t size() const override { return size_; }
 protected:
    const char *data_ = nullptr;
    int64_t size_ = 0;
};

//-----------------------------------------------------------------------------

class MemoryStream : public BaseStream {
 public:
    MemoryStream() = default;
    MemoryStream(const char *data, size_t size);
    tresult PLUGIN_API seek  (int64 pos, int32 mode, int64* result) override;
    tresult PLUGIN_API write (void* buffer, int32 numBytes, int32* numBytesWritten) override;
    const char * data() const override { return buffer_.data(); }
    size_t size() const override { return buffer_.size(); }
    void release(std::string& dest);
protected:
    std::string buffer_;
};

//-----------------------------------------------------------------------------

Vst::IHostApplication * getHostContext();

class HostApplication :
    public Vst::IHostApplication,
    public Vst::IPlugInterfaceSupport {
public:
    HostApplication ();
    virtual ~HostApplication ();
    tresult PLUGIN_API queryInterface(const TUID _iid, void **obj) override;
    DUMMY_REFCOUNT_METHODS

    tresult PLUGIN_API getName (Vst::String128 name) override;
    tresult PLUGIN_API createInstance (TUID cid, TUID _iid, void** obj) override;

    tresult PLUGIN_API isPlugInterfaceSupported(const TUID _iid) override;
protected:
    std::vector<std::tuple<vst::FUID, const char *, bool>> supportedInterfaces_;
};

//-----------------------------------------------------------------------------

struct HostAttribute {
    enum Type
    {
        kInteger,
        kFloat,
        kString,
        kBinary
    };
    explicit HostAttribute(int64 value) : type(kInteger) { v.i = value; }
    explicit HostAttribute(double value) : type(kFloat) { v.f = value; }
    explicit HostAttribute(const Vst::TChar* s);
    explicit HostAttribute(const char * data, uint32 n);
    HostAttribute(const HostAttribute& other) = delete; // LATER
    HostAttribute(HostAttribute&& other) noexcept;
    ~HostAttribute();
    HostAttribute& operator =(const HostAttribute& other) = delete; // LATER
    HostAttribute& operator =(HostAttribute&& other) noexcept;
    // data
    union v
    {
      int64 i;
      double f;
      Vst::TChar* s;
      char* b;
    } v;
    uint32 size = 0;
    Type type;
};

//-----------------------------------------------------------------------------

class HostObject : public FUnknown {
public:
    virtual ~HostObject() {}

    uint32 PLUGIN_API addRef() override {
        return refcount_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    uint32 PLUGIN_API release() override {
        auto res = refcount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (res == 0){
            delete this;
        }
        return res;
    }
private:
    std::atomic<int32_t> refcount_{1};
};

//-----------------------------------------------------------------------------

class HostAttributeList : public HostObject, public Vst::IAttributeList {
public:
    MY_IMPLEMENT_QUERYINTERFACE(Vst::IAttributeList)
    MY_REFCOUNT_METHODS(HostObject)

    tresult PLUGIN_API setInt (AttrID aid, int64 value) override;
    tresult PLUGIN_API getInt (AttrID aid, int64& value) override;
    tresult PLUGIN_API setFloat (AttrID aid, double value) override;
    tresult PLUGIN_API getFloat (AttrID aid, double& value) override;
    tresult PLUGIN_API setString (AttrID aid, const Vst::TChar* string) override;
    tresult PLUGIN_API getString (AttrID aid, Vst::TChar* string, uint32 size) override;
    tresult PLUGIN_API setBinary (AttrID aid, const void* data, uint32 size) override;
    tresult PLUGIN_API getBinary (AttrID aid, const void*& data, uint32& size) override;

    void print();
protected:
    HostAttribute* find(AttrID aid);
    std::unordered_map<std::string, HostAttribute> list_;
};

//-----------------------------------------------------------------------------

class HostMessage : public HostObject, public Vst::IMessage {
public:
    MY_IMPLEMENT_QUERYINTERFACE(Vst::IMessage)
    MY_REFCOUNT_METHODS(HostObject)

    const char* PLUGIN_API getMessageID () override { return messageID_.c_str(); }
    void PLUGIN_API setMessageID (const char* messageID) override { messageID_ = messageID; }
    Vst::IAttributeList* PLUGIN_API getAttributes () override;

    void print();
protected:
    std::string messageID_;
    IPtr<HostAttributeList> attributes_;
};


} // vst
