/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion_engine
{

static void getPossibleInputDeviceNames (Engine& e,
                                         StringArray& s, StringArray& a,
                                         BigInteger& hasAudio,
                                         BigInteger& hasMidi)
{
    auto& dm = e.getDeviceManager();

    for (int i = 0; i < dm.getNumInputDevices(); ++i)
    {
        if (auto in = dm.getInputDevice (i))
        {
            if (! in->isEnabled())
                continue;

            if (dynamic_cast<MidiInputDevice*> (in) != nullptr)
                hasMidi.setBit (s.size(), true);
            else
                hasAudio.setBit (s.size(), true);

            s.add (in->getName());
            a.add (in->getAlias());
        }
    }
}

static void getPossibleOutputDeviceNames (Engine& e,
                                          StringArray& s, StringArray& a,
                                          BigInteger& hasAudio,
                                          BigInteger& hasMidi)
{
    auto& dm = e.getDeviceManager();

    for (int i = 0; i < dm.getNumOutputDevices(); ++i)
    {
        if (auto out = dm.getOutputDeviceAt (i))
        {
            if (! out->isEnabled())
                continue;

            if (auto m = dynamic_cast<MidiOutputDevice*> (out))
            {
                if (m->isConnectedToExternalController())
                    continue;

                hasMidi.setBit (s.size(), true);
            }
            else
            {
                hasAudio.setBit (s.size(), true);
            }

            s.add (out->getName());
            a.add (out->getAlias());
        }
    }
}


//==============================================================================
InsertPlugin::InsertPlugin (PluginCreationInfo info)  : Plugin (info)
{
    auto um = getUndoManager();
    name.referTo (state, IDs::name, um);
    inputDevice.referTo (state, IDs::inputDevice, um);
    outputDevice.referTo (state, IDs::outputDevice, um);
    manualAdjustMs.referTo (state, IDs::manualAdjustMs, um);

    updateDeviceTypes();
}

InsertPlugin::~InsertPlugin()
{
    notifyListenersOfDeletion();
}

//==============================================================================
const char* InsertPlugin::xmlTypeName ("insert");

String InsertPlugin::getName()                                               { return name.get().isNotEmpty() ? name : TRANS("Insert Plugin"); }
String InsertPlugin::getPluginType()                                         { return xmlTypeName; }
String InsertPlugin::getShortName (int)                                      { return TRANS("Insert"); }
double InsertPlugin::getLatencySeconds()                                     { return latencySeconds; }
void InsertPlugin::getChannelNames (StringArray*, StringArray*)              {}
bool InsertPlugin::takesAudioInput()                                         { return true; }
bool InsertPlugin::takesMidiInput()                                          { return true; }
bool InsertPlugin::canBeAddedToClip()                                        { return false; }
bool InsertPlugin::needsConstantBufferSize()                                 { return true; }

void InsertPlugin::initialise (const PluginInitialisationInfo& info)
{
    {
        const ScopedLock sl (bufferLock);
        sendBuffer.resize ({ 2u, (choc::buffer::FrameCount) info.blockSizeSamples });
        sendBuffer.clear();

        returnBuffer.resize ({ 2u, (choc::buffer::FrameCount) info.blockSizeSamples });
        returnBuffer.clear();
    }

    initialiseWithoutStopping (info);
}

void InsertPlugin::initialiseWithoutStopping (const PluginInitialisationInfo& info)
{
    // This latency number is from trial and error, may need more testing
    latencySeconds = manualAdjustMs / 1000.0 + (double)info.blockSizeSamples / info.sampleRate;
}

void InsertPlugin::deinitialise()
{
    const ScopedLock sl (bufferLock);

    sendBuffer = {};
    returnBuffer = {};

    sendMidiBuffer.clear();
    returnMidiBuffer.clear();
}

void InsertPlugin::applyToBuffer (const PluginRenderContext& fc)
{
    CRASH_TRACER
    const ScopedLock sl (bufferLock);

    // Fill send buffer with data
    if (sendDeviceType == audioDevice && fc.destBuffer != nullptr)
    {
        copyIntersection (sendBuffer, toBufferView (*fc.destBuffer)
                                        .fromFrame ((choc::buffer::FrameCount) fc.bufferStartSample));
    }
    else if (sendDeviceType == midiDevice && fc.bufferForMidiMessages != nullptr)
    {
        sendMidiBuffer.clear();
        sendMidiBuffer.mergeFromAndClear (*fc.bufferForMidiMessages);
    }

    // Clear the context buffers
    if (fc.bufferForMidiMessages != nullptr)
        fc.bufferForMidiMessages->clear();

    if (fc.destBuffer != nullptr)
        fc.destBuffer->clear (fc.bufferStartSample, fc.bufferNumSamples);

    if (sendDeviceType == noDevice)
        return;

    // Copy the return buffer into the context
    if (returnDeviceType == audioDevice && fc.destBuffer != nullptr)
    {
        copyIntersection (toBufferView (*fc.destBuffer)
                            .fromFrame ((choc::buffer::FrameCount) fc.bufferStartSample),
                          returnBuffer);
    }
    else if (returnDeviceType == midiDevice && fc.bufferForMidiMessages != nullptr)
    {
        fc.bufferForMidiMessages->mergeFromAndClear (returnMidiBuffer);
    }
}

String InsertPlugin::getSelectableDescription()
{
    return TRANS("Insert Plugin");
}

void InsertPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    if (v.hasProperty (IDs::name))
        name = v.getProperty (IDs::name).toString();

    if (v.hasProperty (IDs::outputDevice))
        outputDevice = v.getProperty (IDs::outputDevice).toString();

    if (v.hasProperty (IDs::inputDevice))
        inputDevice = v.getProperty (IDs::inputDevice).toString();

    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();
}

void InsertPlugin::updateDeviceTypes()
{
    CRASH_TRACER
    TRACKTION_ASSERT_MESSAGE_THREAD

    StringArray devices, aliases;
    BigInteger hasAudio, hasMidi;

    auto setDeviceType = [] (DeviceType& deviceType, BigInteger& audio, BigInteger& midi, int index)
    {
        if (audio[index])       deviceType = audioDevice;
        else if (midi[index])   deviceType = midiDevice;
        else                    deviceType = noDevice;
    };

    getPossibleInputDeviceNames (engine, devices, aliases, hasAudio, hasMidi);
    setDeviceType (returnDeviceType, hasAudio, hasMidi, devices.indexOf (inputDevice.get()));

    getPossibleOutputDeviceNames (engine, devices, aliases, hasAudio, hasMidi);
    setDeviceType (sendDeviceType, hasAudio, hasMidi, devices.indexOf (outputDevice.get()));

    propertiesChanged();
    changed();
}

void InsertPlugin::getPossibleDeviceNames (Engine& e,
                                           StringArray& s, StringArray& a,
                                           BigInteger& hasAudio,
                                           BigInteger& hasMidi,
                                           bool forInput)
{
    if (forInput)
        getPossibleInputDeviceNames (e, s, a, hasAudio, hasMidi);
    else
        getPossibleOutputDeviceNames (e, s, a, hasAudio, hasMidi);
}

bool InsertPlugin::hasAudio() const       { return sendDeviceType == audioDevice  || returnDeviceType == audioDevice; }
bool InsertPlugin::hasMidi() const        { return sendDeviceType == midiDevice   || returnDeviceType == midiDevice; }

void InsertPlugin::fillSendBuffer (choc::buffer::ChannelArrayView<float>* destAudio, MidiMessageArray* destMidi)
{
    CRASH_TRACER
    const ScopedLock sl (bufferLock);
    
    if (sendDeviceType == audioDevice)
    {
        if (destAudio != nullptr)
            copyIntersection (*destAudio, sendBuffer);
    }
    else if (sendDeviceType == midiDevice)
    {
        if (destMidi != nullptr)
            destMidi->mergeFromAndClear (sendMidiBuffer);
    }
}

void InsertPlugin::fillReturnBuffer (choc::buffer::ChannelArrayView<float>* srcAudio, MidiMessageArray* srcMidi)
{
    CRASH_TRACER
    const ScopedLock sl (bufferLock);
    
    if (returnDeviceType == audioDevice)
    {
        if (srcAudio != nullptr)
            copyIntersection (returnBuffer, *srcAudio);
    }
    else if (returnDeviceType == midiDevice)
    {
        if (srcMidi != nullptr)
            returnMidiBuffer.mergeFrom (*srcMidi);
    }
}

void InsertPlugin::valueTreePropertyChanged (ValueTree& v, const juce::Identifier& i)
{
    if (v == state)
    {
        auto update = [&i] (CachedValue<String>& deviceName) -> bool
        {
            if (i != deviceName.getPropertyID())
                return false;

            deviceName.forceUpdateOfCachedValue();
            return true;
        };

        if (update (outputDevice) || update (inputDevice))
            updateDeviceTypes();
    }

    Plugin::valueTreePropertyChanged (v, i);
}

}
