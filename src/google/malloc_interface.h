// Copyright (c) 2005, Google Inc.
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// ---
// Author: Sanjay Ghemawat <opensource@google.com>
//
// Extra interfaces exported by some malloc implementations.  These
// interfaces are accessed through a virtual base class so an
// application can link against a malloc that does not implement these
// interfaces, and it will get default versions that do nothing.

#ifndef _GOOGLE_MALLOC_INTERFACE_H__
#define _GOOGLE_MALLOC_INTERFACE_H__

#include <google/perftools/config.h>
#include <stddef.h>
#include <string>

static const int kMallocHistogramSize = 64;

// The default implementations of the following routines do nothing.
class MallocInterface {
 public:
  virtual ~MallocInterface();

  // See "verify_memory.h" to see what these routines do
  virtual bool VerifyAllMemory();
  virtual bool VerifyNewMemory(void* p);
  virtual bool VerifyArrayNewMemory(void* p);
  virtual bool VerifyMallocMemory(void* p);
  virtual bool MallocMemoryStats(int* blocks, size_t* total,
                                 int histogram[kMallocHistogramSize]);

  // Get a human readable description of the current state of the malloc
  // data structures.  The state is stored as a null-terminated string
  // in a prefix of "buffer[0,buffer_length-1]".
  // REQUIRES: buffer_length > 0.
  virtual void GetStats(char* buffer, int buffer_length);

  // Get a string that contains a sample of live objects and the stack
  // traces that allocated these objects.  The format of the returned
  // string is equivalent to the output of the heap profiler and can
  // therefore be passed to "pprof".
  //
  // The generated data is *appended* to "*result".  I.e., the old
  // contents of "*result" are preserved.
  virtual void GetHeapSample(STL_NAMESPACE::string* result);

  // -------------------------------------------------------------------
  // Control operations for getting and setting malloc implementation
  // specific parameters.  Some currently useful properties:
  //
  // generic
  // -------
  // "generic.current_allocated_bytes"
  //      Number of bytes currently allocated by application
  //      This property is not writable.
  //
  // "generic.heap_size"
  //      Number of bytes in the heap ==
  //            current_allocated_bytes +
  //            fragmentation +
  //            freed memory regions
  //      This property is not writable.
  //
  // tcmalloc
  // --------
  // "tcmalloc.max_total_thread_cache_bytes"
  //      Upper limit on total number of bytes stored across all
  //      per-thread caches.  Default: 16MB.
  // 
  // "tcmalloc.current_total_thread_cache_bytes"
  //      Number of bytes used across all thread caches.
  //      This property is not writable.
  //
  // "tcmalloc.slack_bytes"
  //      Number of bytes allocated from system, but not currently
  //      in use by malloced objects.  I.e., bytes available for
  //      allocation without needing more bytes from system.
  //      This property is not writable.
  //
  // TODO: Add more properties as necessary
  // -------------------------------------------------------------------

  // Get the named "property"'s value.  Returns true if the property
  // is known.  Returns false if the property is not a valid property
  // name for the current malloc implementation.
  // REQUIRES: property != NULL; value != NULL
  virtual bool GetNumericProperty(const char* property, size_t* value);

  // Set the named "property"'s value.  Returns true if the property
  // is known and writable.  Returns false if the property is not a
  // valid property name for the current malloc implementation, or
  // is not writable.
  // REQUIRES: property != NULL
  virtual bool SetNumericProperty(const char* property, size_t value);

  // The current malloc implementation.  Always non-NULL.
  static MallocInterface* instance();

  // Change the malloc implementation.  Typically called by the
  // malloc implementation during initialization.
  static void Register(MallocInterface* implementation);

 protected:
  // Get a list of stack traces of sampled allocation points.
  // Returns a pointer to a "new[]-ed" result array.
  //
  // The state is stored as a sequence of adjacent entries
  // in the returned array.  Each entry has the following form:
  //    uintptr_t count;        // Number of objects with following trace
  //    uintptr_t size;         // Size of object
  //    uintptr_t depth;        // Number of PC values in stack trace
  //    void*     stack[depth]; // PC values that form the stack trace
  //
  // The list of entries is terminated by a "count" of 0.
  //
  // It is the responsibility of the caller to "delete[]" the returned array.
  //
  // May return NULL to indicate no results.
  //
  // This is an internal interface.  Callers should use the more
  // convenient "GetHeapSample(string*)" method defined above.
  virtual void** ReadStackTraces();
};

#endif  // _GOOGLE_MALLOC_INTERFACE_H__
