#include <Python.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

PyObject *eugene_win32_FindFiles(PyObject *self, PyObject *arg)
{
  if(!PyUnicode_Check(arg)) {
    PyErr_SetString(PyExc_ValueError, "FindFiles() accepts a single 'str' argument!");
    return NULL;
  }

  const char *pattern = PyUnicode_AsUTF8(arg);
  WIN32_FIND_DATAA find_data;
  HANDLE handle = FindFirstFileA(pattern, &find_data);
  if(handle == INVALID_HANDLE_VALUE) {
    PyErr_SetString(PyExc_ValueError, "no files found");
    return NULL;
  }

  PyObject *result = PyList_New(0);
  do {
    ULARGE_INTEGER last_write;
    last_write.LowPart  = find_data.ftLastWriteTime.dwLowDateTime;
    last_write.HighPart = find_data.ftLastWriteTime.dwHighDateTime;

    ULARGE_INTEGER size;
    size.LowPart  = find_data.nFileSizeLow;
    size.HighPart = find_data.nFileSizeHigh;

    long is_directory = find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;

    PyObject *file_data = PyDict_New();

    PyObject *cFileName       = PyUnicode_FromString(find_data.cFileName);
    PyObject *ftLastWriteTime = PyLong_FromUnsignedLongLong(last_write.QuadPart);
    PyObject *nFileSize       = PyLong_FromUnsignedLongLong(size.QuadPart);
    PyObject *bIsDirectory    = PyBool_FromLong(is_directory);

    PyDict_SetItemString(file_data, "cFileName", cFileName);
    PyDict_SetItemString(file_data, "ftLastWriteTime", ftLastWriteTime);
    PyDict_SetItemString(file_data, "nFileSize", nFileSize);
    PyDict_SetItemString(file_data, "bIsDirectory", bIsDirectory);

    PyList_Append(result, file_data);

    Py_XDECREF(file_data);
    Py_XDECREF(cFileName);
    Py_XDECREF(ftLastWriteTime);
    Py_XDECREF(nFileSize);
    Py_XDECREF(bIsDirectory);
  } while(FindNextFileA(handle, &find_data));

  FindClose(handle);

  return result;
}

char p_local_date[256];
char p_local_time[256];
PyObject *eugene_win32_GetDateTimeFormat(PyObject *self, PyObject *arg)
{
  if(!PyLong_Check(arg)) {
    PyErr_SetString(PyExc_ValueError, "GetDateTimeFormat() accepts a single 'int' arguemnt!");
    return NULL;
  }

  ULARGE_INTEGER time;
  time.QuadPart = PyLong_AsUnsignedLongLong(arg);

  FILETIME ft;
  ft.dwLowDateTime  = time.LowPart;
  ft.dwHighDateTime = time.HighPart;

  SYSTEMTIME st;

  FileTimeToLocalFileTime(&ft, &ft);
  FileTimeToSystemTime(&ft, &st);
  GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE,
    &st, NULL, p_local_date, sizeof(p_local_date));
  GetTimeFormatA(LOCALE_USER_DEFAULT, 0,
    &st, NULL, p_local_time, sizeof(p_local_time));

  PyObject *datetime = PyUnicode_FromFormat("%s %s", p_local_date, p_local_time);

  return datetime;
}

/*
 * List of functions to add to eugene_win32 in exec_eugene_win32().
 */
static PyMethodDef eugene_win32_functions[] = {
    {
      "FindFiles",
      (PyCFunction)eugene_win32_FindFiles, METH_O,
      "win32 FindFirstFile()"
    },

    {
      "GetDateTimeFormat",
      (PyCFunction)eugene_win32_GetDateTimeFormat, METH_O,
      "win32 Get<Date/Time>Format()"
    },

    { NULL, NULL, 0, NULL } /* marks end of array */
};

/*
 * Initialize eugene_win32. May be called multiple times, so avoid
 * using static state.
 */
int exec_eugene_win32(PyObject *module)
{
    PyModule_AddFunctions(module, eugene_win32_functions);

    PyModule_AddStringConstant(module, "__author__", "bruneron");
    PyModule_AddStringConstant(module, "__version__", "1.0.0");

    return 0; /* success */
}

/*
 * Documentation for eugene_win32.
 */
PyDoc_STRVAR(eugene_win32_doc, "Win32 interface for Eugene");


static PyModuleDef_Slot eugene_win32_slots[] = {
    { Py_mod_exec, exec_eugene_win32 },
    { 0, NULL }
};

static PyModuleDef eugene_win32_def = {
    PyModuleDef_HEAD_INIT,
    "eugene_win32",
    eugene_win32_doc,
    0,              /* m_size */
    NULL,           /* m_methods */
    eugene_win32_slots,
    NULL,           /* m_traverse */
    NULL,           /* m_clear */
    NULL,           /* m_free */
};

PyMODINIT_FUNC PyInit_eugene_win32() {
    return PyModuleDef_Init(&eugene_win32_def);
}
