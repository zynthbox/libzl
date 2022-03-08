
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
    tracktion_engine::Engine *engine() const;

    void registerClip(ClipAudioSource *clip);
    void unregisterClip(ClipAudioSource *clip);

    /**
     * \brief This function will act on the given command (play, stop, set clip settings, etc)
     * @note This will take ownership of the command and handle its deletion once the command has been completed
     * @param clipCommand The command you wish to act on
     */
    void handleClipCommand(ClipCommand *clipCommand);

private:
    std::unique_ptr<SamplerSynthPrivate> d;
};
