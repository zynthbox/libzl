
#pragma once

#include <QObject>
#include <QCoreApplication>

struct ClipCommand;
class ClipAudioSource;
class SamplerSynthPrivate;
class SyncTimerPrivate;
namespace tracktion_engine {
    class Engine;
}
class SamplerSynth : public QObject
{
    Q_OBJECT
    friend class SyncTimerPrivate;
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
     * @note You should likely not be using this - schedule commands into SyncTimer unless you have a reason
     * @param clipCommand The command you wish to act on
     */
    void handleClipCommand(ClipCommand *clipCommand);

    /**
     * \brief SamplerSynth's CPU load as estimated by JackD
     * @return a float, from 0 through 1, describing the current CPU load
     */
    float cpuLoad() const;

protected:
    // Some stuff to ensure SyncTimer can operate with sufficient speed
    void handleClipCommand(ClipCommand* clipCommand, quint64 currentTick);

private:
    SamplerSynthPrivate *d{nullptr};
};
