#include "WavMetadataHelper.h"
#include "JUCEHeaders.h"

#include <iostream>

using namespace std;

void WavMetadataHelper::readMetadataFromWav(const char *file) {
    cerr << "Reading metadata from file " << file << endl;

    AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    AudioFormatReader* reader = formatManager.createReaderFor(File(file));
    if (reader) {
        for (String key : reader->metadataValues.getAllKeys()) {
            cerr << "## Metadata Key: " + key + ", value: " + reader->metadataValues.getValue (key, "unknown") << endl;
        }
    } else {
        cerr << "Reader is null. Exiting." << endl;
    }
}

void WavMetadataHelper::writeMetadataToWav(const char *file) {
    cerr << "Writing metadata to file " << file << endl;

    StringPairArray params;
    params.set("ZYN_META_1", "value1");
    params.set("ZYN_META_2", "value2");
    params.set("ZYN_META_3", "value3");
    params.set("ZYN_META_4", "value4");
    params.set("ZYN_META_5", "value5");
    params.set(WavAudioFormat::riffInfoArtist, "Zynthiloops");

    WavAudioFormat wavFormat;
    wavFormat.replaceMetadataInFile(File(file), &params);
}
