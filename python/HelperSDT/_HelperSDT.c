// systemtap python SDT marker C module
// Copyright (C) 2016-2020 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include <Python.h>
#include <sys/sdt.h>
#include <stdlib.h>


// PR25841: ensure that the libHelperSDT.so file contains debuginfo
// for the tapset helper functions, so they don't have to look into libpython*
#include <frameobject.h>
// python 3.11 removed direct access to PyFrameObject members
// https://docs.python.org/3.11/whatsnew/3.11.html#c-api-changes
#if PY_MINOR_VERSION < 11
PyFrameObject _dummy_frame;
#else
PyFrameObject *_dummy_frame;
#endif
#include <object.h>
PyVarObject _dummy_var;
#include <dictobject.h>
PyDictObject _dummy_dict;
#include <listobject.h>
PyListObject _dummy_list;
#include <tupleobject.h>
PyTupleObject _dummy_tuple;
#include <unicodeobject.h>
PyUnicodeObject _dummy_unicode;

PyASCIIObject _dummy_ascii;
PyCompactUnicodeObject _dummy_compactunicode;
// PyStringObject _dummy_string;
#include <bytesobject.h>
PyBytesObject _dummy_bytes;
#include <longobject.h>
PyLongObject _dummy_long;
#include <cpython/code.h>
// Ensure PyCodeObject debuginfo is available
PyCodeObject _dummy_code_obj;

/* This is internal to libpython. */
#if PY_MINOR_VERSION == 6  /* python 3.6 */
typedef Py_ssize_t (*dict_lookup_func)(PyDictObject *mp, PyObject *key, Py_hash_t hash, PyObject ***value_addr,
					 Py_ssize_t *hashpos);
typedef struct {
  Py_hash_t me_hash;
  PyObject *me_key;
  PyObject *me_value;
} PyDictKeyEntry;
PyDictKeyEntry _dummy_dictkeyentry;
struct _dictkeysobject {
  Py_ssize_t dk_refcnt;
  Py_ssize_t dk_size;
  dict_lookup_func dk_lookup;
  Py_ssize_t dk_usable;
  Py_ssize_t dk_nentries;
  char dk_indices[];  /* char is required to avoid strict aliasing. */
};

#elif PY_MINOR_VERSION == 7  /* python 3.7 */
typedef Py_ssize_t (*dict_lookup_func)(PyDictObject *mp, PyObject *key, Py_hash_t hash, PyObject **value_addr);
typedef struct {
  Py_hash_t me_hash;
  PyObject *me_key;
  PyObject *me_value;
} PyDictKeyEntry;
PyDictKeyEntry _dummy_dictkeyentry;
struct _dictkeysobject {
  Py_ssize_t dk_refcnt;
  Py_ssize_t dk_size;
  dict_lookup_func dk_lookup;
  Py_ssize_t dk_usable;
  Py_ssize_t dk_nentries;
  char dk_indices[];
};

/* This is internal to libpython. */
#elif PY_MINOR_VERSION == 9 || PY_MINOR_VERSION == 10  /* python 3.9 / 3.10 */
typedef Py_ssize_t (*dict_lookup_func)(PyDictObject *mp, PyObject *key, Py_hash_t hash, PyObject ***value_addr,
					 Py_ssize_t *hashpos);
typedef struct {
  Py_hash_t me_hash;
  PyObject *me_key;
  PyObject *me_value;
} PyDictKeyEntry;
PyDictKeyEntry _dummy_dictkeyentry;
struct _dictkeysobject {
  Py_ssize_t dk_refcnt;
  Py_ssize_t dk_size;
  dict_lookup_func dk_lookup;
  Py_ssize_t dk_usable;
  Py_ssize_t dk_nentries;
  char dk_indices[];  /* char is required to avoid strict aliasing. */
};

#elif PY_MINOR_VERSION >= 11  /* python 3.11+ */
/*
 * PyDictObject [...,PyDictKeysObject ma_keys,...]
 * PyDictKeysObject [..,dk_log2_size,dk_kind,...]
 * PyDictKeyEntry [me_hash,me_key,me_value]
 */

typedef struct {
  Py_hash_t me_hash;
  PyObject *me_key;
  PyObject *me_value;
} PyDictKeyEntry;
PyDictKeyEntry _dummy_dictkeyentry;
typedef struct {
    PyObject *me_key;
    PyObject *me_value;
} PyDictUnicodeEntry;
PyDictUnicodeEntry _dummy_dictunicodeentry;
struct _dictkeysobject {
  Py_ssize_t dk_refcnt;
  uint8_t dk_log2_size;
  uint8_t dk_log2_index_bytes;
  uint8_t dk_kind;
  uint32_t dk_version;
  Py_ssize_t dk_usable;
  Py_ssize_t dk_nentries;
  char dk_indices[];  /* char is required to avoid strict aliasing. */
};

struct Py3_object {
    long ob_refcnt;
    void *ob_type;
};
typedef struct Py3_object Py3Object;

struct _dictvalues {
    Py3Object *values[1];
};

#include <stdbool.h>
#include <stddef.h>

// Redacted from Python Include/internal/pycore_frame.h
// Support for Python 3.11+ including 3.12, 3.13, 3.14, 3.15

struct _stp_frame {
    PyObject_HEAD
    PyFrameObject *f_back;      /* previous frame, or NULL */
    struct _stp_Py3InterpreterFrame *f_frame; /* points to the frame data */
    PyObject *f_trace;          /* Trace function */
    int f_lineno;               /* Current line number. Only valid if non-zero */
    char f_trace_lines;         /* Emit per-line trace events? */
    char f_trace_opcodes;       /* Emit per-opcode trace events? */
#if PY_MINOR_VERSION >= 12
    PyObject *f_extra_locals;   /* Dict for locals set by users using f_locals, could be NULL */
    PyObject *f_locals_cache;   /* Borrowed reference for PyEval_GetLocals */
    PyObject *f_overwritten_fast_locals; /* Support for borrowed references */
#else
    char f_fast_as_locals;      /* Have the fast locals of this frame been converted to a dict? */
#endif
    /* The frame data, if this frame object owns the frame */
    PyObject *_f_frame_data[1];
};

typedef struct _stp_frame _stp_Py3FrameObject;
_stp_Py3FrameObject _dummy_stp_Py3FrameObject;

#if PY_MINOR_VERSION >= 15
// Python 3.15+ uses _PyStackRef for some fields
// _PyStackRef is a union with a uintptr_t bits field
typedef union {
    uintptr_t bits;
} _stp_PyStackRef;

struct _stp_Py3InterpreterFrame {
    _stp_PyStackRef f_executable; /* Deferred or strong reference (code object or None) */
    struct _stp_Py3InterpreterFrame *previous;
    _stp_PyStackRef f_funcobj; /* Deferred or strong reference */
    PyObject *f_globals; /* Borrowed reference */
    PyObject *f_builtins; /* Borrowed reference */
    PyObject *f_locals; /* Strong reference, may be NULL */
    void /*PyFrameObject*/ *frame_obj; /* Strong reference, may be NULL */
    void /*_Py_CODEUNIT*/ *instr_ptr; /* Instruction currently executing */
    void *stackpointer;
    uint16_t return_offset;
    char owner;
    uint8_t visited;
    /* Locals and stack */
    _stp_PyStackRef localsplus[1];
} _stp_InterpreterFrame;
#else
// Python 3.11-3.14
struct _stp_Py3InterpreterFrame {
    /* "Specials" section */
    void /*PyFunctionObject*/ *f_func; /* Strong reference */
    PyObject *f_globals; /* Borrowed reference */
    PyObject *f_builtins; /* Borrowed reference */
    PyObject *f_locals; /* Strong reference, may be NULL */
    PyCodeObject *f_code; /* Strong reference */
    void /*PyFrameObject*/ *frame_obj; /* Strong reference, may be NULL */
    /* Linkage section */
    struct _stp_Py3InterpreterFrame *previous;
    // NOTE: This is not necessarily the last instruction started in the given
    // frame. Rather, it is the code unit *prior to* the *next* instruction. For
    // example, it may be an inline CACHE entry, an instruction we just jumped
    // over, or (in the case of a newly-created frame) a totally invalid value:
    void /*_Py_CODEUNIT*/ *prev_instr;
    int stacktop;     /* Offset of TOS from localsplus  */
    bool is_entry;  // Whether this is the "root" frame for the current _PyCFrame.
    char owner;
    /* Locals and stack */
    PyObject *localsplus[1];
} _stp_InterpreterFrame;
#endif

typedef struct _stp_InterpreterFrame _stp_Py3InterpreterFrame;

#endif

#define PROVIDER HelperSDT3

static PyObject *
trace_callback(PyObject *self, PyObject *args)
{
    unsigned int what;
    PyObject *frame_obj, *arg_obj;
    char *module_name;
    unsigned int key;

    /* Parse the input tuple */
    if (!PyArg_ParseTuple(args, "IOOsI", &what, &frame_obj, &arg_obj,
			  &module_name, &key))
	return NULL;

    /* We want to name the probes with the same name as the
     * define. This is tricky, so, we'll just save the define,
     * undefine it, call the STAP_PROBE macro, then redfine it. */
    switch (what) {
    case PyTrace_CALL:
#pragma push_macro("PyTrace_CALL")
#undef PyTrace_CALL
	STAP_PROBE4(PROVIDER, PyTrace_CALL, module_name, key,
		    frame_obj, arg_obj);
#pragma pop_macro("PyTrace_CALL")
	break;
    case PyTrace_EXCEPTION:
#pragma push_macro("PyTrace_EXCEPTION")
#undef PyTrace_EXCEPTION
	STAP_PROBE4(PROVIDER, PyTrace_EXCEPTION, module_name, key,
		    frame_obj, arg_obj);
#pragma pop_macro("PyTrace_EXCEPTION")
	break;
    case PyTrace_LINE:
#pragma push_macro("PyTrace_LINE")
#undef PyTrace_LINE
	STAP_PROBE4(PROVIDER, PyTrace_LINE, module_name, key,
		    frame_obj, arg_obj);
#pragma pop_macro("PyTrace_LINE")
	break;
    case PyTrace_RETURN:
#pragma push_macro("PyTrace_RETURN")
#undef PyTrace_RETURN
	STAP_PROBE4(PROVIDER, PyTrace_RETURN, module_name, key,
		    frame_obj, arg_obj);
#pragma pop_macro("PyTrace_RETURN")
	break;
    // FIXME: What about PyTrace_C_CALL, PyTrace_C_EXCEPTION,
    // PyTrace_C_RETURN? Fold them into their non-'_C_' versions or
    // have unique probes?
    default:
	// FIXME: error/exception here?
	return NULL;
    }
    return Py_BuildValue("i", 0);
}

static PyMethodDef HelperSDT_methods[] = {
	{"trace_callback", trace_callback, METH_VARARGS,
	 "Trace callback function."},
	{NULL, NULL, 0, NULL}        /* Sentinel */
};

PyDoc_STRVAR(HelperSDT_doc,
	     "This module provides an interface for interfacing between Python tracing events and systemtap.");

//
// According to <https://docs.python.org/3/c-api/module.html>:
//
// ====
// Module state may be kept in a per-module memory area that can be
// retrieved with PyModule_GetState(), rather than in static
// globals. This makes modules safe for use in multiple
// sub-interpreters.
//
// This memory area is allocated based on m_size on module creation,
// and freed when the module object is deallocated, after the m_free
// function has been called, if present.
//
// Setting m_size to -1 means that the module does not support
// sub-interpreters, because it has global state.
//
// Setting it to a non-negative value means that the module can be
// re-initialized and specifies the additional amount of memory it
// requires for its state. Non-negative m_size is required for
// multi-phase initialization.
// ====
//
// This C module has no module state, so we'll set m_size to -1 (and
// m_slots, m_traverse, m_clear, and m_free to NULL).
//
// All state information is held by the python HelperSDT module, not
// this _HelperSDT helper C extension module.

static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "_HelperSDT",
        HelperSDT_doc,
        -1,				/* m_size */
        HelperSDT_methods,
        NULL,				/* m_slots */
        NULL,				/* m_traverse */
        NULL,				/* m_clear */
        NULL				/* m_free */
};

PyMODINIT_FUNC
PyInit__HelperSDT(void)
{
    PyObject *module;
    char *stap_module;

    module = PyModule_Create(&moduledef);
    if (module == NULL)
	return NULL;

    // Add constants for the PyTrace_* values we use.
    PyModule_AddIntMacro(module, PyTrace_CALL);
    PyModule_AddIntMacro(module, PyTrace_EXCEPTION);
    PyModule_AddIntMacro(module, PyTrace_LINE);
    PyModule_AddIntMacro(module, PyTrace_RETURN);

    // Get the systemtap module name from the environment. If we found
    // it, let systemtap know information it needs.
    stap_module = getenv("SYSTEMTAP_MODULE");
    if (stap_module) {
        // Here we force the compiler to fully resolve the function
        // pointer value by assigning it to a variable and accessing
        // it with the asm() statement. Otherwise we get a @GOTPCREL
        // reference which stap can't parse.
        void *fptr = &PyObject_GenericGetAttr;
        asm ("nop" : "=r"(fptr) : "r"(fptr));
        STAP_PROBE2(PROVIDER, Init, stap_module, fptr);
    }
    return module;
}
