# Contains common procs used by onthefly testcases.

# Wave-4 prune: default smoke keeps correctness cases (finish, iter
# 1-2, one timer interval, one mild stress) and skips the long
# stress matrix (us-scale, profile/hardcore/max). Restore with:
#   ONTHEFLY_FULL=1 make installcheck RUNTESTFLAGS="*onthefly.exp"
proc onthefly_full_p {} {
   global env
   if {[info exists env(ONTHEFLY_FULL)] \
	   && $env(ONTHEFLY_FULL) != "" \
	   && $env(ONTHEFLY_FULL) != "0"} {
      return 1
   }
   return 0
}

# Return 1 if this subtest should run under the default smoke set.
proc onthefly_smoke_keep_p {subtest} {
   if {[onthefly_full_p]} { return 1 }
   # Dyninst sanity (hrtimer only)
   if {[string match "dyninst_*" $subtest]} { return 1 }
   # Finish-at-start (0 toggles) + short toggle sequences
   if {[string match "otf_finish_*" $subtest]} { return 1 }
   if {[string match "*_iter_1" $subtest]} { return 1 }
   if {[string match "*_iter_2" $subtest]} { return 1 }
   # One representative small-interval valid test per driver family
   if {$subtest eq "otf_timer_10ms" || $subtest eq "otf_timer_100ms"} {
      return 1
   }
   # One mild stress (not us-scale / hardcore / max / profile storms)
   if {$subtest eq "otf_stress_2ms_iter_50" \
	   || $subtest eq "otf_stress_5ms_iter_50" \
	   || $subtest eq "otf_stress_10ms_iter_50"} {
      return 1
   }
   return 0
}

proc onthefly_maybe_skip {subtest} {
   global test
   if {[onthefly_smoke_keep_p $subtest]} { return 0 }
   untested "$test - $subtest (onthefly smoke; set ONTHEFLY_FULL=1)"
   return 1
}

# Returns true if this subtest targets dyninst
proc is_dyninst_subtest {subtest} {
   return [string match dyninst_* $subtest]
}

# Checks if the output matches the expected pattern. The 'patterns' arg is a
# list of lines which are matched against the output using [string match], so
# globby chars are allowed.
proc is_valid_output {output patterns} {

   # i represents the index of the last pattern matched in 'patterns' list
   set i -1
   foreach line [split $output "\n"] {

      # Passed all patterns
      if {$i >= [llength $patterns]} {
         verbose -log "no more patterns to match against"
         return 0
      }

      # Matches the next pattern?
      set next_pattern [lindex $patterns [expr $i + 1]]
      if {[string match $next_pattern $line]} {
         incr i
      } else {
          verbose -log "expected: $next_pattern"
          verbose -log "received: $line"
          # continue; $i will be too small, but we can see more errors
      }
   }

   # Check that we went through all the patterns
   if {$i >= [expr [llength $patterns] - 1]} {
      return 1
   } else {
      return 0
   }
}

# Runs the subtest with the given parameters. See make_script for SUBTEST,
# ENABLED, MAXTOGGLES, and TIMER. The 'args' parameter contains any extra
# arguments to pass to stap.
proc run_subtest {SUBTEST ENABLED MAXTOGGLES TIMER args} {
   global test

   # Prepare the script
   set script [make_script $SUBTEST $ENABLED $MAXTOGGLES $TIMER]

   # Run stap (and throw on error)
   if {[catch {run_stap $SUBTEST $args $script} stapout]} {
      verbose -log "stap error: $stapout"
      error "stap"
      return
   }

   return $stapout
}

proc cannot_test {subtest timer} {
   global test

   if {[is_dyninst_subtest $subtest] && ![dyninst_p]} {
      untested "$test - $subtest (no dyninst)"
      return 1
   }

   if {$timer < 1000 && ![hrtimer_p]} {
      untested "$test - $subtest (no hrtimer)"
      return 1
   }

   return 0
}

# Same as run_subtest, except that the output is then checked for validity
proc run_subtest_valid {subtest start_enabled max_toggles timer args} {
   global test
   if {[onthefly_maybe_skip $subtest]} { return }
   if {[cannot_test $subtest $timer]} { return }

   if {[catch {eval run_subtest $subtest $start_enabled \
                                $max_toggles $timer $args} output]} {
      fail "$test - $subtest ($output)"
      return
   }

   # Prepare the pattern
   set pattern [make_pattern $subtest $start_enabled $max_toggles]

   # Check that the output is valid
   if {![is_valid_output $output $pattern]} {
      fail "$test - $subtest (invalid output)"
      return
   }

   pass "$test - $subtest (valid output)"
}

# Same as run_subtest, except that errors are caught and cause FAILs. If
# run_subtest returns without errors, we PASS.
proc run_subtest_stress {subtest start_enabled max_toggles timer args} {
   global test
   if {[onthefly_maybe_skip $subtest]} { return }
   if {[cannot_test $subtest $timer]} { return }

   # We don't care about warnings/handler errors
   set args "$args --suppress-handler-errors -w"

   if {[catch {eval run_subtest $subtest $start_enabled \
                                $max_toggles $timer $args} output]} {
      fail "$test - $subtest ($output)"
      return
   }

   pass "$test - $subtest (survived)"
}

