////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2013-2017 Dimitry Ishenko
// Contact: dimitry (dot) ishenko (at) (gee) mail (dot) com
//
// Distributed under the GNU GPL license. See the LICENSE.md file for details.

////////////////////////////////////////////////////////////////////////////////
#include "posix/error.hpp"
#include "proc/process.hpp"

#include <csignal>
#include <cstdio>
#include <ctime>
#include <streambuf>
#include <system_error>

#include <sys/wait.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
namespace pgm
{

////////////////////////////////////////////////////////////////////////////////
// Input streambuf on an open file descriptor.
//
// Uses C-style I/O to read from file.
// Supports one character putback.
//
class ifilebuf : public std::streambuf
{
public:
    ////////////////////
    explicit ifilebuf(int fd) : file_(fdopen(fd, "r"))
    {
        if(!file_) throw posix::errno_error();
        setg(&buffer_, &buffer_ + sizeof(buffer_), &buffer_ + sizeof(buffer_));
    }

protected:
    ////////////////////
    virtual int_type underflow() override
    {
        auto ch = std::fgetc(file_);
        if(ch != traits_type::eof()) { buffer_ = ch; gbump(-1); }
        return ch;
    }

    virtual std::streamsize xsgetn(char_type* s, std::streamsize n) override
    {
        auto c = 0;
        if(n > 0 && gptr() < egptr()) { *s = buffer_; gbump(1); ++c; ++s; --n; }

        return c + std::fread(s, sizeof(char_type), n, file_);
    }

    virtual int_type pbackfail(int_type ch = traits_type::eof()) override
    {
        if(eback() < gptr())
        {
            if(ch != traits_type::eof()) buffer_ = ch;
            gbump(-1);
            return 0;
        }
        else return traits_type::eof();
    }

    ////////////////////
    std::FILE* file_;
    char_type buffer_; // buffer to enable putback of one char
};

////////////////////////////////////////////////////////////////////////////////
// Input streambuf on an open file descriptor.
//
// Uses C-style I/O to read from file.
// Supports one character putback.
//
class ofilebuf : public std::streambuf
{
public:
    ////////////////////
    explicit ofilebuf(int fd) : file_(fdopen(fd, "w"))
    { if(!file_) throw posix::errno_error(); }

protected:
    ////////////////////
    virtual int sync() override { return std::fflush(file_); }

    virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override
    { return std::fwrite(s, sizeof(char_type), n, file_); }

    virtual int_type overflow(int_type ch = traits_type::eof()) override
    { return std::fputc(ch, file_); }

    ////////////////////
    std::FILE* file_;
};

////////////////////////////////////////////////////////////////////////////////
namespace
{

using fd_pipe = int[2];
static constexpr auto rd = 0;
static constexpr auto wr = 1;

// open pipe
void open(fd_pipe fp)
{
    if(::pipe(fp)) throw posix::errno_error();
}

// close pipe
void close(fd_pipe fp) noexcept
{
    ::close(fp[rd]);
    ::close(fp[wr]);
}

// connect write end of the pipe
// and close read end
void write_to(fd_pipe fp, int fd)
{
    if(::dup2(fp[wr], fd) == -1) throw posix::errno_error();
    ::close(fp[rd]);
}

// connect read end of the pipe
// and close write end
void read_from(fd_pipe fp, int fd)
{
    if(::dup2(fp[rd], fd) == -1) throw posix::errno_error();
    ::close(fp[wr]);
}

// create ofilebuf on write end of the pipe
// and close read end
auto ofilebuf_from(fd_pipe fp)
{
    ::close(fp[rd]);
    return new ofilebuf(fp[wr]);
}

// create ofilebuf on read end of the pipe
// and close write end
auto ifilebuf_from(fd_pipe fp)
{
    ::close(fp[wr]);
    return new ifilebuf(fp[rd]);
}

}

////////////////////////////////////////////////////////////////////////////////
process::process(std::function<int()>&& fn)
{
    fd_pipe fpo, fpi, fpe;
    try
    {
        open(fpo);
        open(fpi);
        open(fpe);

        id_ = id(::fork());
        if(native_handle() == -1) throw posix::errno_error();

        ////////////////////
        // child
        if(native_handle() == 0)
        {
            write_to (fpo, STDOUT_FILENO);
            read_from(fpi, STDIN_FILENO );
            write_to (fpe, STDERR_FILENO);

            int code;
            try { code = fn(); } catch(...) { code = EXIT_FAILURE; }

            std::exit(code);
        }

        ////////////////////
        // parent
        else
        {
            fbo_.reset(ifilebuf_from(fpo));
            cout.basic_ios::rdbuf(fbo_.get());

            fbi_.reset(ofilebuf_from(fpi));
            cin.basic_ios::rdbuf(fbi_.get());

            fbe_.reset(ifilebuf_from(fpe));
            cerr.basic_ios::rdbuf(fbe_.get());

            state_ = running;
        }
    }
    catch(...)
    {
        close(fpo);
        close(fpi);
        close(fpe);

        throw;
    }
}

////////////////////////////////////////////////////////////////////////////////
// defined here to enable unique_ptrs with
// incomplete type (ifilebuf and ofilebuf)
process::process() { }
process::process(process&& rhs) noexcept
{
    if(joinable()) std::terminate();
    swap(rhs);
}

process::~process() noexcept { if(joinable()) std::terminate(); }

process& process::operator=(process&& rhs) noexcept
{
    if(joinable()) std::terminate();
    swap(rhs); return *this;
}

////////////////////////////////////////////////////////////////////////////////
void process::swap(process& rhs) noexcept
{
    using std::swap;
    swap(id_    , rhs.id_    );
    swap(state_ , rhs.state_ );
    swap(code_  , rhs.code_  );
    swap(signal_, rhs.signal_);
    swap(fbi_   , rhs.fbi_   );
    swap(fbo_   , rhs.fbo_   );
    swap(fbe_   , rhs.fbe_   );

    cout.basic_ios::rdbuf(fbo_.get());
    cin.basic_ios::rdbuf(fbi_.get());
    cerr.basic_ios::rdbuf(fbe_.get());
}

////////////////////////////////////////////////////////////////////////////////
state process::state()
{
    while(state_ == running || state_ == stopped)
    {
        int status;
        auto pid = ::waitpid(native_handle(), &status, WNOHANG);
        if(pid == -1)
        {
            posix::errno_error error;
            if(error.code() == std::errc::no_child_process)
            {
                // This could happen for the following reasons:
                // 1. process was never started;
                // 2. SA_NOCLDWAIT is set or SIGCHLD is set to SIG_IGN.
                // In case of (2) we don't know if the process exited
                // normally or due to a signal, so set it to not_started.
                state_ = not_started;
            }
            else throw error;
        }
        else if(pid == 0) break; // no change
        else if(pid == native_handle()) update(status);
    }

    return state_;
}

////////////////////////////////////////////////////////////////////////////////
void process::detach() noexcept { id_ = id(); state_ = not_started; }

////////////////////////////////////////////////////////////////////////////////
void process::join()
{
    if(!joinable()) throw std::system_error(posix::errc::invalid_argument);
    if(get_id() == this_process::get_id())
        throw std::system_error(posix::errc::resource_deadlock_would_occur);

    while(state_ == running || state_ == stopped)
    {
        int status;
        auto pid = ::waitpid(native_handle(), &status, 0);
        if(pid == -1)
        {
            posix::errno_error error;
            if(error.code() == std::errc::no_child_process)
            {
                state_ = not_started; // see process::state()
            }
            else throw error;
        }
        else if(pid == native_handle()) update(status);
    }
}

////////////////////////////////////////////////////////////////////////////////
bool process::try_join_for_(const nsec& time)
{
    if(!joinable()) throw std::system_error(posix::errc::invalid_argument);
    if(get_id() == this_process::get_id())
        throw std::system_error(posix::errc::resource_deadlock_would_occur);

    state(); // update state
    if(state_ != running && state_ != stopped) return true;

    ////////////////////
    struct guard
    {
        guard() { before = std::signal(SIGCHLD, [](int){ }); }
       ~guard() { std::signal(SIGCHLD, before); }
    private:
        void(*before)(int);
    }
    signal;

    ////////////////////
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(time);
    timespec tv { sec.count(), (time - sec).count() };

    while(::nanosleep(&tv, &tv) == -1)
    {
        posix::errno_error error;
        if(error.code() == std::errc::interrupted)
        {
            state(); // update state_
            if(state_ != running && state_ != stopped) return true;
        }
        else throw error;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////
void process::raise(int signal)
{
    if(!joinable()) throw std::system_error(posix::errc::invalid_argument);

    if(int code = ::kill(native_handle(), signal))
    {
        posix::errno_error error;
        if(error.code() != std::errc::no_such_process) throw error;
    }
}

////////////////////////////////////////////////////////////////////////////////
void process::update(int status)
{
    if(WIFEXITED(status))
    {
        state_ = exited;
        code_ = WEXITSTATUS(status);
        id_ = id();
    }
    else if(WIFSIGNALED(status))
    {
        state_ = signaled;
        signal_ = WTERMSIG(status);
        id_ = id();
    }
    else if(WIFSTOPPED(status))
    {
        state_ = stopped;
        signal_ = WSTOPSIG(status);
    }
}

////////////////////////////////////////////////////////////////////////////////
namespace this_process
{

////////////////////////////////////////////////////////////////////////////////
process::id get_id() noexcept { return process::id(::getpid()); }
process::id parent_id() noexcept { return process::id(::getppid()); }

////////////////////////////////////////////////////////////////////////////////
}

////////////////////////////////////////////////////////////////////////////////
}
