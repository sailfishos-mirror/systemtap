// -*- C++ -*-
// fd_filebuf.h --- file-descriptor based streambuf
// Copyright (C) 2026 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef STAP_FD_FILEBUF_H
#define STAP_FD_FILEBUF_H

#include <streambuf>
#include <ios>
#include <vector>

#include <unistd.h>
#include <errno.h>

// Minimal stand-in for libstdc++'s __gnu_cxx::stdio_filebuf, which is
// not available when systemtap is built against libc++.  It reads from
// or writes to a raw file descriptor.  It does not take ownership of the
// descriptor; call close() to release it early (as stdio_filebuf does).
class fd_filebuf : public std::streambuf
{
public:
  fd_filebuf(int fd, std::ios_base::openmode mode)
    : fd_(fd),
      writable_((mode & std::ios_base::out) != 0),
      closed_(false),
      buffer_(4096)
  {
    if (! writable_)
      setg(&buffer_[0], &buffer_[0], &buffer_[0]);
  }

  ~fd_filebuf() { }

  void close()
  {
    if (! closed_)
      {
        ::close(fd_);
        closed_ = true;
      }
  }

protected:
  int_type underflow() override
  {
    if (gptr() < egptr())
      return traits_type::to_int_type(*gptr());
    for (;;)
      {
        ssize_t n = ::read(fd_, &buffer_[0], buffer_.size());
        if (n < 0 && errno == EINTR)
          continue;
        if (n <= 0)
          return traits_type::eof();
        setg(&buffer_[0], &buffer_[0], &buffer_[0] + n);
        return traits_type::to_int_type(*gptr());
      }
  }

  int_type overflow(int_type ch) override
  {
    if (! traits_type::eq_int_type(ch, traits_type::eof()))
      {
        char c = traits_type::to_char_type(ch);
        if (! write_all(&c, 1))
          return traits_type::eof();
      }
    return traits_type::not_eof(ch);
  }

  std::streamsize xsputn(const char* s, std::streamsize n) override
  {
    return write_all(s, n) ? n : 0;
  }

  int sync() override { return 0; }

private:
  bool write_all(const char* s, std::streamsize n)
  {
    while (n > 0)
      {
        ssize_t w = ::write(fd_, s, n);
        if (w < 0)
          {
            if (errno == EINTR)
              continue;
            return false;
          }
        s += w;
        n -= w;
      }
    return true;
  }

  int fd_;
  bool writable_;
  bool closed_;
  std::vector<char> buffer_;
};

#endif // STAP_FD_FILEBUF_H
