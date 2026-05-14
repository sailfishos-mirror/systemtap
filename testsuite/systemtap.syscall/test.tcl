set syscall_dir ""
set current_dir ""

proc syscall_cleanup {} {
    global syscall_dir current_dir
    if {$current_dir != ""}  {
	cd $current_dir
	set current_dir ""
    }
    if {$syscall_dir != ""} {
#	puts "rm -rf $syscall_dir"
	exec rm -rf $syscall_dir
	set syscall_dir ""
    }
}

proc syscall_cleanup_and_exit {} {
#    puts "syscall_cleanup_and_exit"
    syscall_cleanup
    exit 0
}

# PR34119: don't override bgerror from elsewhere; definitely don't
# proactively run syscall_cleanup() which can remove tmp dirs while
# they're still being used.
#proc bgerror {error} {
#    puts "ERROR: $error"
#    syscall_cleanup
#}

trap {syscall_cleanup_and_exit} SIGINT

proc run_one_test {filename flags bits suite} {
    global syscall_dir current_dir test_module

    set testname [file tail [string range $filename 0 end-2]]

    # execname() returns the first 15 chars of the test exe name.
    set re_testname [string range $testname 0 14]

    if {[catch {exec mktemp -d [pwd]/staptestXXXXXX} syscall_dir]} {
	send_log "$bits-bit $testname $suite : Failed to create temporary directory: $syscall_dir"
	untested "$bits-bit $testname $suite"
	syscall_cleanup
	return
    }

    set flags "$flags additional_flags=-lrt"
    # Newer compiler have -Werror set by default, disable various warnings
    set flags "$flags additional_flags=-Wno-stringop-overflow"
    set res [target_compile $filename $syscall_dir/$testname executable $flags ]
    if { $res != "" } {
	send_log "$bits-bit $testname $suite : no corresponding devel environment found\n"
	untested "$bits-bit $testname $suite"
	syscall_cleanup
	return
    }

    # Use -R here, in case a previous staprun hangs up or leaves the
    # syscall.ko module in memory, which would block all subsequent
    # invocations.
    # DRAFT: cmd no longer used; see later wrapper + direct exec + procfs
    #        PID write for the long-running background stap instance.
    # set cmd "staprun -R ${test_module} -c $syscall_dir/${testname}"
    
    # Extract additional C flags needed to compile
    set add_flags ""
    foreach i $flags {
	if [regexp "^additional_flags=" $i] {
	    regsub "^additional_flags=" $i "" tmp
	    append add_flags " $tmp"
	}
    }

    # Extract the expected results
    # Use the preprocessor so we can ifdef tests in and out
    
    set ccmd "gcc -E -C -P $add_flags $filename"
    # XXX: but note, this will expand all system headers too!
    catch {eval exec $ccmd} output
    
    set ind 0
    foreach line [split $output "\n"] {
	if {[regsub {//staptest//} $line {} line]} {
	    set line "^$re_testname: [string trimleft $line]"

	    # We need to quote all these metacharacters
	    regsub -all {\(} $line {\\(} line
	    regsub -all {\)} $line {\\)} line
	    regsub -all {\|} $line {\|} line
	    # + and * are metacharacters, but should always be used
	    # as metacharacters in the expressions, don't escape them.
	    #regsub -all {\+} $line {\\+} line
	    #regsub -all {\*} $line {\\*} line
	    
	    # Turn '[[[[' and ']]]]' into '(' and ')' respectively.
	    # Because normally parens get quoted, this allows us to
	    # have non-quoted parens.
	    regsub -all {\[\[\[\[} $line {(} line
	    regsub -all {\]\]\]\]} $line {)} line

	    # Turn '!!!!' into '|'. Since normally pipe characters get
	    # quoted, this allows us to have non-quoted pipes.
	    regsub -all {!!!!} $line {|} line

	    regsub -all NNNN $line {[\-0-9]+} line
	    regsub -all XXXX $line {[x0-9a-fA-F]+} line
	    
	    set results($ind) $line
	    incr ind
	}
    }

    if {$ind == 0} {
	# unsupported
	syscall_cleanup
	unsupported "$bits-bit $testname $suite not supported on this arch"
	return
    }

    set current_dir [pwd]
    cd $syscall_dir

    # DRAFT speedup: instead of per-test staprun -c (which reloads module),
    # write this test's PID to the synthetic procfs file (so the already-
    # running background stap script sees it in target_pid), then exec the
    # test binary directly.  Use a short-lived bash wrapper so we know the
    # final PID in advance ($$ before exec).  Caller in syscall.exp truncates
    # the append-mode log before each test; we simply read its full (small)
    # contents afterwards.
    global stap_output_log test_module_name
    set procfs_target "/proc/systemtap/${test_module_name}/target_pid"
    # wrapper: echo our pid -> procfs then exec the real test binary
    set wrap_cmd "bash -c \"echo \\$\\$ > $procfs_target; exec ./$testname\""
    # run the wrapper (test binary stdout/stderr may be separate; stap
    # traces go to the bg log)
    catch {exec truncate -s 0 $stap_output_log}
    catch {eval exec $wrap_cmd} exe_output

    # DRAFT: poll the log until we see an exit_group line (the final
    # syscall for the test binary).  This synchronizes on actual
    # completion instead of a fixed sleep.
    set max_wait_ms 2000
    set waited 0
    while {1} {
        set output ""
        catch {
            set fh [open $stap_output_log r]
            set output [read $fh]
            close $fh
        }
        if {[regexp {exit_group} $output]} {
            break
        }
        after 100
        incr waited 100
        if {$waited > $max_wait_ms} {
            send_log "Timed out waiting for exit_group in $stap_output_log\n"
            break
        }
    }

    # final read of the completed log
    set output ""
    catch {
        set fh [open $stap_output_log r]
        set output [read $fh]
        close $fh
    }
    
    set i 0
    foreach line [split $output "\n"] {
        # send_log "Comparing $results($i) against $line"
	if {[regexp $results($i) $line]} {
	    incr i
	    if {$i >= $ind} {break}
	}
    }
    if {$i >= $ind} {
	# puts "PASS $testname"
	pass "$bits-bit $testname $suite"
    } else {
	send_log "$testname FAILED. stap trace output (via $wrap_cmd) was:"
        # add $output at end, just in case it was empty somehow; send_log "" is not happy
	send_log "\n------------------------------------------\n$output"
	send_log "\n------------------------------------------\n"
	send_log "RESULTS: (\'*\' = MATCHED EXPECTED)\n"
	set i 0
	foreach line [split $output "\n"] {
	    if {[regexp "${re_testname}: " $line]} {
		if {[regexp $results($i) $line]} {
		    send_log "*$line\n"
		    incr i
		    if {$i >= $ind} {break}
		} else {
		    send_log "$line\n"
		}
	    }
	}
	if {$i < $ind} {
	    send_log -- "--------- EXPECTED and NOT MATCHED ----------\n"
	}
	for {} {$i < $ind} {incr i} {
	    send_log "$results($i)\n"
	}
	fail "$bits-bit $testname $suite"
    }
    syscall_cleanup
    return
}
