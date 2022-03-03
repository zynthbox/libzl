
#pragma once

#include <QObject>
#include <QCoreApplication>
#include <memory>

struct ClipCommand;
class ClipAudioSource;
class SamplerSynthPrivate;
namespace tracktion_engine {
    class Engine;
}
class SamplerSynth : public QObject
{
    Q_OBJECT
public:
    static SamplerSynth *instance();

    explicit SamplerSynth(QObject *parent = nullptr);
    ~SamplerSynth() override;

    void initialize(tracktion_engine::Engine *engine);

    void registerClip(ClipAudioSource *clip);
    void unregisterClip(ClipAudioSource *clip);

    void handleClipCommand(ClipCommand *clipCommand);

private:
    std::unique_ptr<SamplerSynthPrivate> d;
};
