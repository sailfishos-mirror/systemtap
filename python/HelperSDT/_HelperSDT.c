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


// We pull in some public Python headers (with -g) so the helper .so carries
// DWARF for common types. For deep internals (_PyInterpreterFrame etc.) we
// now rely on the target libpython's own DWARF (see tapset comments).
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
#if PY_MINOR_VERSION >= 11
#include <cpython/longintrepr.h>
#else
#include <longintrepr.h>
#endif
PyLongObject _dummy_long;
// Ensure PyCodeObject debuginfo is available (defined via Python.h on all
// supported versions; do not include cpython/code.h directly on 3.9/3.10).
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

// We no longer define synthetic _stp_* copies of Python internal structs here.
// Instead, the tapset uses @cast(..., "RealInternalType", "/usr/lib64/libpython3.NN.so")
// (or just the type name inside a python process probe context) to get the layout
// directly from the target libpython's DWARF. This gives an exact match for the
// probed python's internals without hand-maintained duplicates.
//
// The helper .so is still required for the SDT markers themselves.

// Public PyDictValues dummy kept in case some tapset code still casts to it
// for older compatibility. Internal header structs are no longer duplicated here.
PyDictValues _dummy_dictvalues;

PyHeapTypeObject _dummy_heaptype;

// Expose the exact Python minor version. The tapset (python3.stp) can use
// this for any remaining version-specific behavior, but most internal type
// layouts now come directly from the target libpython's DWARF via @cast(..., "Type", "libpython...").
const int _stp_python3_minor_version = PY_MINOR_VERSION;

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
        STAP_PROBE3(PROVIDER, Init, stap_module, fptr, _stp_python3_minor_version);
    }
    return module;
}
