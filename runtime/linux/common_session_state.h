// Will be included once by runtime/common_session_state.h, which is included
// once by translate.cxx c_unparser::emit_common_header ().

#define DEFINE_SESSION_ATOMIC(name, init)		\
	static atomic_t g_##name = ATOMIC_INIT (init);	\
	static inline atomic_t *name(void) {		\
		return &g_##name;			\
	}

DEFINE_SESSION_ATOMIC(session_state, STAP_SESSION_UNINITIALIZED);

DEFINE_SESSION_ATOMIC(error_count, 0);
DEFINE_SESSION_ATOMIC(skipped_count, 0);
DEFINE_SESSION_ATOMIC(skipped_count_lowstack, 0);
DEFINE_SESSION_ATOMIC(skipped_count_reentrant, 0);
DEFINE_SESSION_ATOMIC(skipped_count_uprobe_reg, 0);
DEFINE_SESSION_ATOMIC(skipped_count_uprobe_unreg, 0);

#undef DEFINE_SESSION_ATOMIC


#ifdef STP_ALIBI
atomic_t g_probe_alibi[STP_PROBE_COUNT];
static inline atomic_t *probe_alibi(size_t index)
{
	// Do some simple bounds-checking.  Translator-generated code
	// should never get this wrong, but better to be safe.
	index = clamp_t(size_t, index, 0, STP_PROBE_COUNT - 1);
	return &g_probe_alibi[index];
}
#endif


#ifdef STP_TIMING
Stat g_probe_timing[STP_PROBE_COUNT];
static inline Stat probe_timing(size_t index)
{
	// Do some simple bounds-checking.  Translator-generated code
	// should never get this wrong, but better to be safe.
	index = clamp_t(size_t, index, 0, STP_PROBE_COUNT - 1);
	return g_probe_timing[index];
}
Stat g_refresh_timing;
Stat g_module_init_timing;
#endif


// Globals are declared and initialized in the translator.
static struct stp_globals stp_global;

#define global(name)		(stp_global.name)
#define global_set(name, val)	(global(name) = (val))
#define global_lock(name)	(&global(name ## _lock))
#define global_lock_init(name)	stp_rwlock_init(global_lock(name))
#ifdef STP_TIMING
#define global_skipped(name)	(&global(name ## _lock_skip_count))
#define global_contended(name)	(&global(name ## _lock_contention_count))
#endif


static int stp_session_init(void)
{
	size_t i;

	// The session_state and error/skip counts have static initialization.

#ifdef STP_ALIBI
	// Initialize all the alibi counters
	for (i = 0; i < STP_PROBE_COUNT; ++i)
		atomic_set(probe_alibi(i), 0);
#endif

#ifdef STP_TIMING
	// Initialize each Stat used for timing information
	for (i = 0; i < STP_PROBE_COUNT; ++i)
		// NB: we don't check for null return here, but instead at
		// passage to probe handlers and at final printing.
	        g_probe_timing[i] = _stp_stat_init(STAT_OP_MIN, STAT_OP_MAX, STAT_OP_AVG, STAT_OP_VARIANCE, 0, NULL);
	g_refresh_timing = _stp_stat_init(STAT_OP_MIN, STAT_OP_MAX, STAT_OP_AVG, STAT_OP_VARIANCE, 0, NULL);
	g_module_init_timing = _stp_stat_init(STAT_OP_MIN, STAT_OP_MAX, STAT_OP_AVG, STAT_OP_VARIANCE, 0, NULL);
#endif

#if defined(STAPCONF_UDELAY_SIMPLE) && !defined(STAPCONF_UDELAY_SIMPLE_EXPORTED)
	// PR20516: Some s390 kernels that have udelay_simple() don't
	// have it exported. Note that we have to do this early since
	// other init routines could call udelay().
        kallsyms_udelay_simple = (void *)kallsyms_lookup_name("udelay_simple");
        if (kallsyms_udelay_simple == NULL) {
	    _stp_error("couldn't find udelay_simple");
	    return 1;
        }
#endif
	return 0;
}

static void stp_session_exit(void)
{
#ifdef STP_TIMING
	size_t i;

	// Free each Stat on error in systemtap_module_init()
	for (i = 0; i < STP_PROBE_COUNT; ++i)
		_stp_stat_del(g_probe_timing[i]);
	_stp_stat_del(g_refresh_timing);
	_stp_stat_del(g_module_init_timing);
#endif
}
