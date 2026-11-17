#pragma once
// The rehearsal recorder. Two tracks, the dry bass and what goes to the amp, written as a
// 24-bit stereo WAV next to a JSON file of markers (song changes, mode swaps, Keeps, rig
// swaps). It starts by itself when you play, keeping two seconds from before the first
// note, and a take ends after a quiet minute. The room can come from the phone's mic: the
// phone records while REC is on and sends its file afterwards (saved beside the take).
//
// The audio thread only copies into a ring (push, wait-free). A writer thread does the
// rest: listening for playing, writing the file, updating its header every few seconds so
// a pulled plug still leaves a usable take, and clearing the oldest takes when space runs
// low. Everything is counted in samples, so it tests at full speed.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ks
{
class Recorder
{
public:
    struct Take { std::string name; double seconds = 0; int64_t bytes = 0; int64_t modified = 0; };

    explicit Recorder (std::string folder);
    ~Recorder();

    void setRate (double sampleRate);                           // from Engine::prepare
    void push (const float* dry, const float* out, int n);      // audio thread
    void setEnabled (bool on);                                  // Record when I play
    void stopTake();                                            // the phone's Stop this take
    void mark (const std::string& what);                        // a song change, a Keep...

    bool recording() const { return active.load(); }
    double takeSeconds() const;
    double quietSeconds() const;                                // since the bass last played, recording or not
    float takePeakDb() const { return peakDb.load(); }
    bool clipped() const { return clip.load(); }
    std::string takeName() const;                               // "" when not recording
    int marks() const { return markCount.load(); }
    std::vector<std::pair<double, std::string>> recentMarks (size_t n) const;   // seconds into the take
    std::string firstSong (const std::string& take) const;                      // from the take's markers

    std::vector<Take> takes() const;                            // newest first
    int64_t freeBytes() const;
    std::string folderPath() const { return folder; }

    // the phone's room recording for a take, sent in pieces after it ends
    bool addRoom (const std::string& take, int seq, bool last, const std::vector<uint8_t>& data, const std::string& ext, double startsAt = -1);

    // settings
    static constexpr double kPreRollSeconds = 2.0, kQuietSeconds = 60.0, kMaxTakeHours = 3.5;
    static constexpr float kPlayThreshold = 0.01f;              // about -40 dBFS on the dry bass
    int64_t keepFreeBytes = (int64_t) 5 << 30;                  // clear old takes below 5 GB free

    void flushForTests();                                       // wait until the writer has caught up

private:
    void run();
    void open (int64_t startFrame);
    void close();
    void writeHeader();
    void housekeeping();

    std::string folder;
    std::atomic<int64_t> lastSignal { -1 };
    std::atomic<double> sr { 48000 };
    std::atomic<bool> enabled { true }, active { false }, stopRequest { false }, quit { false }, clip { false };
    std::atomic<float> peakDb { -100 };

    // ring: interleaved dry, out
    std::vector<float> ring;
    std::atomic<int64_t> written { 0 };   // frames pushed, ever
    int64_t readFrame = 0;                // frames handled by the writer

    // the take
    std::unique_ptr<std::fstream> file;
    std::string name;
    int64_t takeStart = 0, frames = 0, lastLoud = 0, lastHeader = 0;
    mutable std::mutex lock;
    std::vector<std::pair<int64_t, std::string>> pendingMarks, takeMarks;
    std::atomic<int> markCount { 0 };
    std::atomic<int64_t> framesNow { 0 };

    std::thread writer;
    std::mutex wake;
    std::condition_variable cv;
    std::atomic<int64_t> caughtUp { 0 };
};
} // namespace ks
