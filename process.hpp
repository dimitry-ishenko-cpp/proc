////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2013-2017 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#ifndef PGM_PROCESS_HPP
#define PGM_PROCESS_HPP

////////////////////////////////////////////////////////////////////////////////
#include <chrono>
#include <csignal>
#include <fstream>
#include <functional>
#include <memory>
#include <utility>

#include <unistd.h> // pid_t

////////////////////////////////////////////////////////////////////////////////
namespace pgm
{

////////////////////////////////////////////////////////////////////////////////
class ofilebuf;
class ifilebuf;

enum state
{
    not_started,
    running,
    stopped,
    exited,
    signaled,
};

////////////////////////////////////////////////////////////////////////////////
// Execute function in a child process.
//
// Provides C++-style streams (cin, cout, cerr) attached
// to the process' stdin, stdout and stderr streams.
//
class process
{
public:
    ////////////////////
    using id = pid_t;

    ////////////////////
    process();
    process(const process&) = delete;
    process(process&&) noexcept;

    template<typename Fn, typename... Args>
    explicit process(Fn&&, Args&&...);

    ~process() noexcept;

    process& operator=(const process&) = delete;
    process& operator=(process&&) noexcept;

    void swap(process&) noexcept;

    ////////////////////
    bool joinable() const noexcept { return id_; }
    explicit operator bool() const noexcept { return joinable(); }

    // get process id
    id get_id() const noexcept { return id_; }

    // get process state, exit code & signal
    pgm::state state();
    int code() const noexcept { return code_; }
    int signal() const noexcept { return signal_; }

    // detach process
    void detach() noexcept;

    // wait for process to finish execution
    void join();

    template<typename Rep, typename Period>
    bool try_join_for(const std::chrono::duration<Rep, Period>&);

    template<typename Clock, typename Duration>
    bool try_join_until(const std::chrono::time_point<Clock, Duration>&);

    ////////////////////
    // send signal to process
    void raise(int);
    void terminate() { raise(SIGTERM); }
    void kill() { raise(SIGKILL); }

    ////////////////////
    std::ofstream cin;
    std::ifstream cout, cerr;

private:
    ////////////////////
    process(std::function<int()>&&);

    ////////////////////
    id id_ = 0;

    enum state state_ = not_started;
    int code_ = -1;
    int signal_ = -1;

    void update(int status);

    using nsec = std::chrono::nanoseconds;
    bool try_join_for_(const nsec&);

    ////////////////////
    std::unique_ptr<ofilebuf> fbi_;
    std::unique_ptr<ifilebuf> fbo_, fbe_;
};

////////////////////////////////////////////////////////////////////////////////
template<typename Fn, typename... Args>
process::process(Fn&& fn, Args&&... args) :
    process(std::function<int()>(
        std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...))
    )
{ }

////////////////////////////////////////////////////////////////////////////////
template<typename Rep, typename Period>
inline bool
process::try_join_for(const std::chrono::duration<Rep, Period>& time)
{ return try_join_for_(std::chrono::duration_cast<nsec>(time)); }

////////////////////////////////////////////////////////////////////////////////
template<typename Clock, typename Duration>
bool process::try_join_until(const std::chrono::time_point<Clock, Duration>& tp)
{
    auto now = Clock::now();
    return try_join_for(tp - (tp < now ? tp : now));
}

////////////////////////////////////////////////////////////////////////////////
inline void swap(process& lhs, process& rhs) noexcept { lhs.swap(rhs); }

////////////////////////////////////////////////////////////////////////////////
}

////////////////////////////////////////////////////////////////////////////////
#endif // PROC_PROCESS_HPP
