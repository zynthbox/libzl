#pragma once

#include <QObject>
#include <QList>
#include <QVariant>

using namespace std;

namespace juce {
    class MidiBuffer;
}
struct ClipCommand;
struct TimerCommand;
class ClipAudioSource;
class SyncTimerPrivate;
class SyncTimer : public QObject {
  // HighResolutionTimer facade
  Q_OBJECT
  Q_PROPERTY(quint64 bpm READ getBpm WRITE setBpm NOTIFY bpmChanged)
  Q_PROPERTY(quint64 scheduleAheadAmount READ scheduleAheadAmount NOTIFY scheduleAheadAmountChanged)
public:
  explicit SyncTimer(QObject *parent = nullptr);
  void addCallback(void (*functionPtr)(int));
  void removeCallback(void (*functionPtr)(int));
  void queueClipToStart(ClipAudioSource *clip);
  void queueClipToStop(ClipAudioSource *clip);
  void queueClipToStartOnChannel(ClipAudioSource *clip, int midiChannel);
  void queueClipToStopOnChannel(ClipAudioSource *clip, int midiChannel);
  void start(int bpm);
  void stop();
  void stopClip(ClipAudioSource *clip);
  int getInterval(int bpm);
  /**
   * \brief Convert a number of subbeat to seconds, given a specific bpm rate
   * @note The number of subbeats is relative to the multiplier (so a multiplier of 32 would give you 128 beats for a note)
   * @param bpm The number of beats per minute used as the basis of the calculation
   * @param beats The number of subbeats to convert to a duration in seconds
   * @return A floating point precision amount of seconds for the given number of subbeats at the given bpm rate
   */
  Q_INVOKABLE float subbeatCountToSeconds(quint64 bpm, quint64 beats) const;
  /**
   * \brief Convert an amount of seconds to the nearest number of subbeats, given a specific bpm rate
   * @note The number of subbeats is relative to the multiplier (so a multiplier of 32 would give you 128 beats for a note)
   * @param bpm The number of beats per minute used as the basis of the calculation
   * @param seconds The number of seconds to convert to an amount of subbeats
   * @return The number of beats that most closely matches the given number of seconds at the given bpm rate
   */
  Q_INVOKABLE quint64 secondsToSubbeatCount(quint64 bpm, float seconds) const;
  /**
   * \brief The timer's beat multiplier (that is, the number of subbeats per quarter note)
   * @return The number of subbeats per quarter note
   */
  Q_INVOKABLE int getMultiplier();
  /**
   * \brief The timer's current bpm rate
   * @return The number of beats per minute currently used as the basis for the timer's operation
   */
  Q_INVOKABLE quint64 getBpm() const;
  /**
   * \brief Sets the timer's bpm rate
   * @param bpm The bpm you wish the timer to operate at
   */
  Q_INVOKABLE void setBpm(quint64 bpm);
  Q_SIGNAL void bpmChanged();

  /**
   * \brief Returns the number of timer ticks you should schedule midi events for to ensure they won't get missed
   * To ensure that jack doesn't miss one of your midi notes, you should schedule at least this many ticks ahead
   * when you are inserting midi notes into the schedule. The logic is that this is the amount of ticks which will
   * fit inside the length of buffer jack uses.
   * If you are working out yourself, the formula for working out the full buffer length (latency) would be:
   * (Frames [or buffer]/Sample Rate) * Period = Theoretical (or Math-derived) Latency in ms
   * and you will want one more than will fit inside that period (so that if you end up with exactly the right
   * conditions, you will have enough to schedule a note on both the first and last frame of a single buffer)
   * @return The number of ticks you should schedule midi notes ahead for
   */
  Q_INVOKABLE quint64 scheduleAheadAmount() const;
  Q_SIGNAL void scheduleAheadAmountChanged();
  /**
   * \brief The current beat, where that makes useful sense
   * @returns An integer from 0 through 128
   */
  int beat() const;
  /**
   * \brief The number of ticks since the timer was most recently started
   * @returns The number of times the timer has fired since it was most recently started
   */
  Q_INVOKABLE quint64 cumulativeBeat() const;

  /**
   * \brief Used only for playback purposes, for synchronising the sampler synth loop playback
   * In short - you probably don't need this, unless you need to sync specifically with jack's internal playback position
   * (which is the most recent tick for stuff put into a jack buffer)
   * @returns The internal jack playback position in timer ticks
   */
  quint64 jackPlayhead() const;
  /**
   * \brief Used for playback purposes, for synchronising the sampler synth loop playback
   * In short - you probably don't need this, unless you need to sync specifically with jack's internal playback position
   * (which is the usecs position of the jack playhead)
   * @returns The internal jack playback position in usecs
   */
  quint64 jackPlayheadUsecs() const;
  /**
   * \brief The current length of a subbeat in microseconds (as used by jack)
   * @return The current length of a subbeat in microseconds
   */
  quint64 jackSubbeatLengthInMicroseconds() const;

  /**
   * \brief Schedule an audio clip to have one or more commands run on it on the next tick of the timer
   * If a command with the associated clip is already scheduled at the position and the given midiNote you're attempting to schedule it into,
   * this function will change the existing to match any new settings (that is, things marked to be done on the command
   * will be marked to be done on the existing command).
   * @note This function will take ownership of the command, and you should expect it to no longer exist after (especially if the above happens)
   * @note If you want the clip to loop (or not), set this on the clip itself along with the other clip properties
   * @param clip The audio clip command you wish to fire on at the specified time
   * @param delay A delay in number of timer ticks counting from the current position
   */
  void scheduleClipCommand(ClipCommand *clip, quint64 delay);
  /**
    * \brief Fired whenever a scheduled clip command has been sent to SamplerSynth
    * @param clipCommand The clip command which has just been sent to SamplerSynth
    */
  Q_SIGNAL void clipCommandSent(ClipCommand *clipCommand);

  /**
   * \brief Schedule a playback command into the playback schedule to be sent with the given delay
   * @note This function will take ownership of the command, and you should expect it to no longer exist after
   * @param delay A delay in number of timer ticks counting from the current position (cumulativeBeat)
   * @param operation A number signifying the operation to schedule (see TimerCommand::Operation)
   * @param parameter1 An integer optionally used by the command's handler to perform its work
   * @param parameter2 A second integer optionally used by the command's handler to perform its work
   * @param parameter2 A third integer optionally used by the command's handler to perform its work
   * @param variantParameter A QVariant used by the parameter's handler, if an integer is insufficient
   */
  void scheduleTimerCommand(quint64 delay, int operation, int parameter1 = 0, int parameter2 = 0, int parameter3 = 0, const QVariant &variantParameter = QVariant());

  /**
   * \brief Schedule a playback command into the playback schedule to be sent with the given delay
   * Scheduled commands will be fired on the step, unless the timer is stopped, at which point they
   * will be deleted and no longer be used. Unlike clip commands, they will not be combined, and instead
   * are simply added to the end of the command list for the given step.
   * @note This function will take ownership of the command, and you should expect it to no longer exist after
   * @param delay A delay in number of timer ticks counting from the current position (cumulativeBeat)
   * @param command A TimerCommand instance to be executed at the given time
   */
  void scheduleTimerCommand(quint64 delay, TimerCommand* command);

  /**
   * \brief Emitted when a timer command is found in the schedule
   *
   * @note This is called from the jack process call, and must complete in an extremely short amount of time
   * If you cannot guarantee a quick operation, use a queued connection
   */
  Q_SIGNAL void timerCommand(TimerCommand *command);

  /**
   * \brief Schedule a note message to be sent on the next tick of the timer
   * @note This is not thread-safe in itself - when the timer is running, don't call this function outside of a callback
   * @param midiNote The note you wish to change the state of
   * @param midiChannel The channel you wish to change the given note on
   * @param setOn Whether or not you are turning the note on
   * @param velocity The velocity of the note (only matters if you're turning it on)
   * @param duration An optional duration for on notes (0 means don't schedule a release, higher will schedule an off at the durationth beat from the start of the note)
   * @param delay A delay in numbers of timer ticks counting from the current position
   */
  void scheduleNote(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity, quint64 duration, quint64 delay);

  /**
   * \brief Schedule a buffer of midi messages (the Juce type) to be sent with the given delay
   * @note This is not thread-safe in itself - when the timer is running, don't call this function outside of a callback
   * @param buffer The buffer that you wish to add to the schedule
   * @param delay The delay (if any) you wish to add
   */
  void scheduleMidiBuffer(const juce::MidiBuffer& buffer, quint64 delay);

  /**
   * \brief Send a note message immediately (ensuring it goes through the step sequencer output)
   * @param midiNote The note you wish to change the state of
   * @param midiChannel The channel you wish to change the given note on
   * @param setOn Whether or not you are turning the note on
   * @param velocity The velocity of the note (only matters if you're turning it on)
   */
  void sendNoteImmediately(unsigned char midiNote, unsigned char midiChannel, bool setOn, unsigned char velocity);

  /**
   * \brief Send a set of midi messages out immediately (ensuring they go through the step sequencer output)
   * @param buffer The buffer that you wish to send out immediately
   */
  void sendMidiBufferImmediately(const juce::MidiBuffer& buffer);

  bool timerRunning();
  Q_SIGNAL void timerRunningChanged();

  Q_SIGNAL void addedHardwareInputDevice(const QString &deviceName, const QString &humanReadableName);
  Q_SIGNAL void removedHardwareInputDevice(const QString &deviceName, const QString &humanReadableName);
private:
  SyncTimerPrivate *d = nullptr;
};
