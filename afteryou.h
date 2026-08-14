// Single-flight / "after-you" table
// Copyright (C) 2026 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef AFTERYOU_H
#define AFTERYOU_H

#include <condition_variable>
#include <mutex>
#include <set>

// Same idea as elfutils debuginfod unique_set: a later job for an
// identical cacheable key waits for the first, then rechecks rather
// than duplicating the fill.  Acquire inserts the key; release erases
// it and wakes waiters.  Same-thread reentry on the same key deadlocks.
template<typename T>
class after_you_set
{
  std::set<T> values;
  std::mutex mtx;
  std::condition_variable cv;
public:
  void acquire (const T& value)
  {
    std::unique_lock<std::mutex> lock (mtx);
    while (values.find (value) != values.end ())
      cv.wait (lock);
    values.insert (value);
  }
  void release (const T& value)
  {
    std::lock_guard<std::mutex> lock (mtx);
    values.erase (value);
    cv.notify_all ();
  }
};

template<typename T>
class after_you_guard
{
  after_you_set<T>& table;
  T mine;
public:
  after_you_guard (after_you_set<T>& t, const T& value)
    : table (t), mine (value) { table.acquire (mine); }
  ~after_you_guard () { table.release (mine); }
  after_you_guard (const after_you_guard&) = delete;
  after_you_guard& operator= (const after_you_guard&) = delete;
};

#endif // AFTERYOU_H
