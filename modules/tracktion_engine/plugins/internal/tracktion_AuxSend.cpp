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

AuxSendPlugin::AuxSendPlugin (PluginCreationInfo info) : Plugin (info)
{
    auto um = getUndoManager();
    busNumber.referTo (state, IDs::busNum, um);
    gainLevel.referTo (state, IDs::auxSendSliderPos, um, decibelsToVolumeFaderPosition (0.0f));
    lastVolumeBeforeMute.referTo (state, IDs::lastVolumeBeforeMuteDb, um, 0.0f);

    gain = addParam ("send level", TRANS("Send level"), { 0.0f, 1.0f },
                     [] (float value)       { return Decibels::toString (volumeFaderPositionToDB (value)); },
                     [] (const String& s)   { return decibelsToVolumeFaderPosition (dbStringToDb (s)); });

    gain->attachToCurrentValue (gainLevel);

    quickParamName = "send level";
}

AuxSendPlugin::~AuxSendPlugin()
{
    notifyListenersOfDeletion();
    gain->detachFromCurrentValue();
}

bool AuxSendPlugin::shouldProcess()
{
    if (ownerTrack != nullptr)
    {
        // If this track gets disabled when muted,
        // we don't need to worry about this since
        // our plugin will be disabled anyway
        if (! ownerTrack->processAudioNodesWhileMuted())
            return true;

        return ! ownerTrack->isMuted (true);
    }
    return true;
}

const char* AuxSendPlugin::xmlTypeName = "auxsend";

String AuxSendPlugin::getName()
{
    String nm (edit.getAuxBusName (busNumber));

    if (nm.isNotEmpty())
        return "S:" + nm;

    return TRANS("Aux Send") + " #" + String (busNumber + 1);
}

String AuxSendPlugin::getShortName (int)
{
    auto nm = edit.getAuxBusName (busNumber);

    if (nm.isNotEmpty())
        return "S:" + nm;

    return TRANS("Send") + ":" + String (busNumber + 1);
}

void AuxSendPlugin::initialise (const PluginInitialisationInfo& info)
{
    lastGain = volumeFaderPositionToGain (gain->getCurrentValue());

    initialiseWithoutStopping (info);
}

void AuxSendPlugin::initialiseWithoutStopping (const PluginInitialisationInfo&)
{
    ownerTrack = getOwnerTrack();
}

void AuxSendPlugin::deinitialise()
{
}

void AuxSendPlugin::applyToBuffer (const PluginRenderContext&)
{
}

String AuxSendPlugin::getBusName()
{
    auto busName = edit.getAuxBusName (busNumber);

    if (busName.isNotEmpty())
        return busName;

    return getDefaultBusName (busNumber);
}

void AuxSendPlugin::setGainDb (float newDb)
{
    float newPos = decibelsToVolumeFaderPosition (newDb);

    if (gain->getCurrentValue() != newPos)
    {
        gain->setParameter (newPos, sendNotification);
        changed();
    }
}

void AuxSendPlugin::setMute (bool b)
{
    if (b)
    {
        lastVolumeBeforeMute = getGainDb();
        setGainDb (lastVolumeBeforeMute - 0.01f); // needed so that automation is recorded correctly
        setGainDb (-100.0f);
    }
    else
    {
        if (lastVolumeBeforeMute < -100.0f)
            lastVolumeBeforeMute = 0.0f;

        setGainDb (getGainDb() + 0.01f); // needed so that automation is recorded correctly
        setGainDb (lastVolumeBeforeMute);
    }
}

bool AuxSendPlugin::isMute()
{
    return getGainDb() <= -90.0f;
}

String AuxSendPlugin::getDefaultBusName (int index)
{
    return "Bus #" + String (index + 1);
}

StringArray AuxSendPlugin::getBusNames (Edit& ed, int maxNumBusses)
{
    StringArray buses;

    for (int i = 0; i < maxNumBusses; ++i)
    {
        auto nm = getDefaultBusName (i);

        if (ed.getAuxBusName (i).isNotEmpty())
            nm << " (" << ed.getAuxBusName (i) << ")";

        buses.add (nm);
    }

    return buses;
}

void AuxSendPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    CachedValue<float>* cvsFloat[]  = { &gainLevel, nullptr };
    CachedValue<int>* cvsInt[]      = { &busNumber, nullptr };
    copyPropertiesToNullTerminatedCachedValues (v, cvsFloat);
    copyPropertiesToNullTerminatedCachedValues (v, cvsInt);

    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();
}

}
