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
// Author: Maxim Lifantsev (with design ideas by Sanjay Ghemawat)
//
// Heap memory leak checker (utilizes heap-profiler and pprof).
//

#ifndef BASE_HEAP_CHECKER_H__
#define BASE_HEAP_CHECKER_H__

#include <google/perftools/basictypes.h>
#include <vector>

// TODO(jandrews): rewrite this documentation
// HeapLeakChecker, a memory leak checking class.
//
// Verifies that there are no memory leaks between its
// construction and call to its *NoLeaks() or *SameHeap() member.
//
// It will dump two profiles at these two events
// (named <prefix>.<name>-beg.heap and <prefix>.<name>-end.heap
//  where <prefix> is given by --heap_profile= and <name> by our costructor)
// and will return false in case the amount of in-use memory
// is more at the time of *NoLeaks() call than
// (or respectively differs at the time of *SameHeap() from)
// what it was at the time of our construction.
// It will also in this case print a message on how to process the dumped
// profiles to locate leaks.
//
// GUIDELINE: In addition to the local heap leak checking between two arbitrary
// points in program's execution, we provide a way for overall
// whole-program heap leak checking, which is WHAT ONE SHOULD NORMALLY USE.
//
// In order to enable the recommended whole-program heap leak checking
// in the BUILD rule for your binary, just depend on "//base:heapcheck"
// Alternatively you can call your binary with e.g. "--heap_check=normal"
// as one of the *early* command line arguments.
//
// CAVEAT: Doing the following alone will not work in many cases
//   int main(int argc, char** argv) {
//     FLAGS_heap_check = "normal";
//     InitGoogle(argv[0], &argc, &argv, true);
//     <do things>
//   }
// The reason is that the program must know that it's going to be
// heap leak checking itself before construction of
// its global variables happens and before main() is executed.
// NOTE: Once "--heap_check=<smth>" is in the command line or //base:heapcheck
// is linked in, you can change the value of FLAGS_heap_check in your program
// any way you wish but before InitGoogle() exits
// (which includes any REGISTER_MODULE_INITIALIZER).
//
// GUIDELINE CONT.: Depending on the value of the FLAGS_heap_check
// -- as well as other flags of this module --
// different modifications of leak checking between different points in
// program's execution take place.
// Currently supported values from less strict to more strict are:
// "minimal", "normal", "strict", "draconian".
// The "as-is" value leaves control to the other flags of this module.
// The "local" value does not start whole-program heap leak checking
// but activates all our Disable*() methods
// for the benefit of local heap leak checking via HeapLeakChecker objects.
//
// For the case of FLAGS_heap_check == "normal"
// everything from before execution of all global variable constructors
// to normal program exit
// (namely after main() returns and after all REGISTER_HEAPCHECK_CLEANUP's
//  are executed, but before any global variable destructors are executed)
// is checked for absense of heap memory leaks.
//
// NOTE: For all but "draconian" whole-program leak check we also
// ignore all heap objects reachable (a the time of the check)
// from any global variable or any live thread stack variable
// or from any object identified by a HeapLeakChecker::IgnoreObject() call.
// The liveness check we do is not very portable and is not 100% exact
// (it might ignore real leaks occasionally
//  -- it might potentially not find some global data region to start from
//     but we consider such cases to be our bugs to fix),
// but it works in most cases and saves us from
// writing a lot of explicit clean up code.
//
// THREADS and heap leak checking: At the beginning of HeapLeakChecker's
// construction and during *NoLeaks()/*SameHeap() calls we grab a lock so that
// heap activity in other threads is paused for the time
// we are recording or analyzing the state of the heap.
// To make non whole-program heap leak check meaningful there should be
// no heap activity in other threads at the these times.
//
// For the whole-program heap leak check it is possible to have
// other threads active and working with the heap when the program exits.
//
// HINT: If you are debugging detected leaks, you can try different
// (e.g. less strict) values for FLAGS_heap_check
// to determine the cause of the reported leaks
// (see the code of HeapLeakChecker::InternalInitStart for details).
//
// GUIDELINE: Below are the preferred ways of making your (test) binary
// pass the above recommended overall heap leak check
// in the order of decreasing preference:
//
// 1. Fix the leaks if they are real leaks.
//
// 2. If you are sure that the reported leaks are not dangerous
//    and there is no good way to fix them, then you can use
//    HeapLeakChecker::DisableChecks(Up|In|At) calls (see below)
//    in the relevant modules to disable certain stack traces
//    for the purpose of leak checking.
//    You can also use HeapLeakChecker::IgnoreObject() call
//    to ignore certain leaked heap objects and everythign reachable from them.
//
// 3. If the leaks are due to some initialization in a third-party package,
//    you might be able to force that initialization before the
//    heap checking starts.
//
//    I.e. if FLAGS_heap_check == "minimal" or less strict, it is before
//    calling InitGoogle or within some REGISTER_MODULE_INITIALIZER.
//    If FLAGS_heap_check == "normal" or stricter, only
//    HeapLeakChecker::LibCPreallocate() happens before heap checking starts.
//
// CAVEAT: Most Google (test) binaries are expected to pass heap leak check
// at the FLAGS_heap_check == "normal" level.
// In certain cases reverting to FLAGS_heap_check == "minimal" level is also
// fine (provided there's no easy way to make it pass at the "normal" level).
// Making a binary pass at "strict" or "draconian" level is not necessary
// or even desirable in the numerous cases when it requires adding
// a lot of (otherwise unused) heap cleanup code to various core libraries.
//
// NOTE: the following should apply only if
//       FLAGS_heap_check == "strict" or stricter
//
// 4. If the found leaks are due to incomplete cleanup
//    in destructors of global variables,
//    extend or add those destructors
//    or use a REGISTER_HEAPCHECK_CLEANUP to do the deallocations instead
//    to avoid cleanup overhead during normal execution.
//    This type of leaks get reported when one goes
//    from "normal" to "strict" checking.
//
// NOTE: the following should apply only if
//       FLAGS_heap_check == "draconian" or stricter
//
// 5. If the found leaks are for global static pointers whose values are
//    allocated/grown (e.g on-demand) and never deallocated,
//    then you should be able to add REGISTER_HEAPCHECK_CLEANUP's
//    or appropriate destructors into these modules
//    to free those objects.
//
//
// Example of local usage (anywhere in the program) -- but read caveat below:
//
//   HeapLeakChecker heap_checker("test_foo");
//
//   { <code that exercises some foo functionality
//      that should preserve memory allocation state> }
//
//   CHECK(heap_checker.SameHeap());
//
// NOTE: One should set FLAGS_heap_check to a non-empty value e.g. "local"
// to help suppress some false leaks for these local checks.
// CAVEAT: The problem with the above example of local checking
// is that you can easily get false leak reports if the checked code
// (indirectly) causes initialization or growth of some global structures
// like caches or reused global temporaries.
// In such cases you should either
// switch to the above *preferred* whole-program checking,
// or somehow *reliably* ensure that false leaks do not happen
// in the portion of the code you are checking.
//
// IMPORTANT: One way people have been using in unit-tests
// is to run some test functionality once
// and then run it again under a HeapLeakChecker object.
// While this helped in many cases, it is not guaranteed to always work
// -- read it will someday break for some hard to debug reason.
// These tricks are no longer needed and are now DEPRECATED
// in favor of using the whole-program checking by just
// adding a dependency on //base:heapcheck.
//
// CONCLUSION: Use the preferred checking via //base:heapcheck
// in your tests even when it means fixing (or bugging someone to fix)
// the leaks in the libraries the test depends on.
//

// A macro to declare module heap check cleanup tasks
// (they run only if we are doing heap leak checking.)
// Use
//  public:
//   void Class::HeapCleanup();
// if you need to do heap check cleanup on private members of a class.
#define REGISTER_HEAPCHECK_CLEANUP(name, body)  \
  namespace { \
  void heapcheck_cleanup_##name() { body; } \
  static HeapCleaner heapcheck_cleaner_##name(&heapcheck_cleanup_##name); \
  }

// A class that exists solely to run its destructor.  This class should not be
// used directly, but instead by the REGISTER_HEAPCHECK_CLEANUP macro above.
class HeapCleaner {
 public:
  typedef void (*void_function)(void);
  HeapCleaner(void_function f);
  static void RunHeapCleanups();
 private:
  static std::vector<void_function>* heap_cleanups_;
};

class HeapLeakChecker {
 public:  // Non-static functions for starting and doing leak checking.

  // Start checking and name the leak check performed.
  // The name is used in naming dumped profiles
  // and needs to be unique only within your binary.
  // It must also be a string that can be a part of a file name,
  // in particular not contain path expressions.
  explicit HeapLeakChecker(const char *name);

  // Return true iff the heap does not have more objects allocated
  // w.r.t. its state at the time of our construction.
  // This does full pprof heap change checking and reporting.
  // To detect tricky leaks it depends on correct working pprof implementation
  // referred by FLAGS_heap_profile_pprof.
  // (By 'tricky leaks' we mean a change of heap state that e.g. for SameHeap
  //  preserves the number of allocated objects and bytes
  //  -- see TestHeapLeakCheckerTrick in heap-checker_unittest.cc --
  //  and thus is not detected by BriefNoLeaks.)
  // CAVEAT: pprof will do no checking over stripped binaries
  // (our automatic test binaries are stripped)
  bool NoLeaks() { return DoNoLeaks(false, true, true); }

  // Return true iff the heap does not seem to have more objects allocated
  // w.r.t. its state at the time of our construction
  // by looking at the number of objects & bytes allocated.
  // This also tries to do pprof reporting of detected leaks.
  bool QuickNoLeaks() { return DoNoLeaks(false, false, true); }

  // Return true iff the heap does not seem to have more objects allocated
  // w.r.t. its state at the time of our construction
  // by looking at the number of objects & bytes allocated.
  // This does not try to use pprof at all.
  bool BriefNoLeaks() { return DoNoLeaks(false, false, false); }

  // These are similar to their *NoLeaks counterparts,
  // but they in addition require no negative leaks,
  // i.e. the state of the heap must be exactly the same
  // as at the time of our construction.
  bool SameHeap() { return DoNoLeaks(true, true, true); }
  bool QuickSameHeap() { return DoNoLeaks(true, false, true); }
  bool BriefSameHeap() { return DoNoLeaks(true, false, false); }

  // Destructor (verifies that some *NoLeaks method has been called).
  ~HeapLeakChecker();

  // Accessors to determine various internal parameters.  These should
  // be set as early as possible.

  // If overall heap check reports found leaks via pprof.  Default: true
  static void set_heap_check_report(bool);
  // Location of pprof script.  Default: $prefix/bin/pprof
  static void set_pprof_path(const char*);
  // Location to write profile dumps.  Default: /tmp
  static void set_dump_directory(const char*);

  static bool heap_check_report();
  static const char* pprof_path();
  static const char* dump_directory();

 private:  // data

  char* name_;  // our remembered name
  size_t name_length_;  // length of the base part of name_
  int64 start_inuse_bytes_;  // bytes in use at our construction
  int64 start_inuse_allocs_;  // allocations in use at our construction

  static pid_t main_thread_pid_;  // For naming output files
  static const char* invocation_name_; // For naming output files
  static const char* invocation_path_; // For running 'pprof'
  static std::string dump_directory_; // Location to write profile dumps

 public:  // Static helpers to make us ignore certain leaks.

  // NOTE: All calls to DisableChecks* affect all later heap profile generation
  // that happens in our construction and inside of *NoLeaks().
  // They do nothing when heap leak checking is turned off.

  // CAVEAT: Disabling via all the DisableChecks* functions happens only
  // up to kMaxStackTrace (see heap-profiler.cc)
  // stack frames down from the stack frame identified by the function.
  // Hence, this disabling will stop working for very deep call stacks
  // and you might see quite wierd leak profile dumps in such cases.

  // Register 'pattern' as another variant of a regular expression to match
  // function_name, file_name:line_number, or function_address
  // of function call/return points for which allocations below them should be
  // ignored during heap leak checking.
  // (This becomes a part of pprof's '--ignore' argument.)
  // Usually this should be caled from a REGISTER_HEAPCHECK_CLEANUP
  // in the source file that is causing the leaks being ignored.
  // CAVEAT: Disabling via DisableChecksIn works only with non-strip'ped
  // binaries, but Google's automated unit tests currently run strip'ped.
  static void DisableChecksIn(const char* pattern);

  // A pair of functions to disable heap checking between them.
  // For example
  //    ...
  //    void* start_address = HeapLeakChecker::GetDisableChecksStart();
  //    <do things>
  //    HeapLeakChecker::DisableChecksToHereFrom(start_address);
  //    ...
  // will disable heap leak checking for everything that happens
  // during any execution of <do things> (including any calls from it).
  // Each such pair of function calls must be from the same function,
  // because this disabling works by remembering the range of
  // return addresses for the two calls.
  static void* GetDisableChecksStart();
  static void DisableChecksToHereFrom(void* start_address);

  // Register the function call point (address) 'stack_frames' above us for
  // which allocations below it should be ignored during heap leak checking.
  // 'stack_frames' must be >= 1 (in most cases one would use the value of 1).
  // For example
  //    void Foo() {  // Foo() should not get inlined
  //      HeapLeakChecker::DisableChecksUp(1);
  //      <do things>
  //    }
  // will disable heap leak checking for everything that happens
  // during any execution of <do things> (including any calls from it).
  // CAVEAT: If Foo() is inlined this will disable heap leak checking
  // under all processing of all functions Foo() is inlined into.
  // Hence, for potentially inlined functions, use the GetDisableChecksStart,
  // DisableChecksToHereFrom calls instead.
  // (In the above example we store and use the return addresses
  //  from Foo to do the disabling.)
  static void DisableChecksUp(int stack_frames);

  // Same as DisableChecksUp,
  // but the function return address is given explicitly.
  static void DisableChecksAt(void* address);

  // Ignore an object at 'ptr'
  // (as well as all heap objects (transitively) referenced from it)
  // for the purposes of heap leak checking.
  // If 'ptr' does not point to an active allocated object
  // at the time of this call, it is ignored;
  // but if it does, the object must not get deleted from the heap later on;
  // it must also be not already ignored at the time of this call.
  // CAVEAT: Use one of the DisableChecks* calls instead of this if possible
  // if you want somewhat easier future heap leak check portability.
  static void IgnoreObject(void* ptr);

  // CAVEAT: DisableChecks* calls will not help you in such cases
  // when you disable only e.g. "new vector<int>", but later grow
  // this vector forcing it to allocate more memory.

  // NOTE: All calls to *IgnoreObject affect only
  // the overall whole-program heap leak check, not local checks with
  // explicit HeapLeakChecker objects.
  // They do nothing when heap leak checking is turned off.

  // Undo what an earlier IgnoreObject() call promised and asked to do.
  // At the time of this call 'ptr' must point to an active allocated object
  // that was previously registered with IgnoreObject().
  static void UnIgnoreObject(void* ptr);

  // NOTE: One of the standard uses of IgnoreObject() and UnIgnoreObject()
  //       is to ignore thread-specific objects allocated on heap.

 public:  // Initializations; to be called from main() only.

  // Full starting of recommended whole-program checking.  This runs after
  // HeapChecker::BeforeConstructors and can do initializations which may
  // depend on configuration parameters set by initialization code.
  // Valid values of heap_check type are:
  //  - "minimal"
  //  - "normal"
  //  - "strict"
  //  - "draconian"
  //  - "local"
  static void StartFromMain(const std::string& heap_check_type);

 private:  // Various helpers

  // Helper for constructors
  void Create(const char *name);
  // Helper for *NoLeaks and *SameHeap
  bool DoNoLeaks(bool same_heap, bool do_full, bool do_report);
  // Helper for DisableChecksAt
  static void DisableChecksAtLocked(void* address);
  // Helper for DisableChecksIn
  static void DisableChecksInLocked(const char* pattern);
  // Helper for DisableChecksToHereFrom
  static void DisableChecksFromTo(void* start_address,
                                  void* end_address,
                                  int max_depth);
  // Helper for DoNoLeaks to ignore all objects reachable from all live data
  static void IgnoreAllLiveObjectsLocked();
  // Helper for IgnoreAllLiveObjectsLocked to ignore all heap objects
  // reachable from currently considered live objects
  static void IgnoreLiveObjectsLocked(const char* name, const char* name2);
  // Preallocates some libc data
  static void LibCPreallocate();
  // Runs REGISTER_HEAPCHECK_CLEANUP cleanups and potentially
  // calls DoMainHeapCheck
  static void RunHeapCleanups(void);
  // Do the overall whole-program heap leak check
  static void DoMainHeapCheck();

  // Type of task for UseProcMaps
  enum ProcMapsTask { IGNORE_GLOBAL_DATA_LOCKED, DISABLE_LIBRARY_ALLOCS };
  // Read /proc/self/maps, parse it, and do the 'proc_maps_task' for each line.
  static void UseProcMaps(ProcMapsTask proc_maps_task);
  // A ProcMapsTask to disable allocations from 'library'
  // that is mapped to [start_address..end_address)
  // (only if library is a certain system library).
  static void DisableLibraryAllocs(const char* library,
                                   uint64 start_address,
                                   uint64 end_address);
  // A ProcMapsTask to ignore global data belonging to 'library'
  // mapped at 'start_address' with 'file_offset'.
  static void IgnoreGlobalDataLocked(const char* library,
                                     uint64 start_address,
                                     uint64 file_offset);

 private:

  // This gets to execute before constructors for all global objects
  static void BeforeConstructors();
  friend void HeapLeakChecker_BeforeConstructors();
  // This gets to execute after destructors for all global objects
  friend void HeapLeakChecker_AfterDestructors();

 public: // TODO(maxim): make this private and remove 'Kind'
         //              when all old clients are retired

  // Kind of checker we want to create
  enum Kind { MAIN, MAIN_DEBUG };

  // Start whole-executable checking
  // (this is public to support existing deprecated usage).
  // This starts heap profiler with a good unique name for the dumped profiles.
  // If kind == MAIN_DEBUG the checking and profiling
  // happen only in the debug compilation mode.
  explicit HeapLeakChecker(Kind kind);  // DEPRECATED

 private:
  DISALLOW_EVIL_CONSTRUCTORS(HeapLeakChecker);
};

#endif  // BASE_HEAP_CHECKER_H__
