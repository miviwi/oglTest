#include <python/types.h>

#include <cstdarg>

namespace python {

None::None() :
  Object(Py_None)
{
  Py_INCREF(Py_None);
}

Long::Long(PyObject *object) :
  Numeric(object)
{
}

Long::Long(long l) :
  Numeric(PyLong_FromLong(l))
{
}

Long Long::from_ul(unsigned long ul)
{
  return PyLong_FromUnsignedLong(ul);
}

Long Long::from_ll(long long ll)
{
  return PyLong_FromLongLong(ll);
}

Long Long::from_ull(unsigned long long ull)
{
  return PyLong_FromUnsignedLongLong(ull);
}

Long Long::from_f(double f)
{
  return PyLong_FromDouble(f);
}

Long Long::from_sz(size_t sz)
{
  return PyLong_FromSize_t(sz);
}

Long Long::from_ssz(ssize_t ssz)
{
  return PyLong_FromSsize_t(ssz);
}

long Long::l() const
{
  return PyLong_AsLong(py());
}

unsigned long Long::ul() const
{
  return PyLong_AsUnsignedLong(py());
}

long long Long::ll() const
{
  return PyLong_AsLongLong(py());
}

unsigned long long Long::ull() const
{
  return PyLong_AsUnsignedLongLong(py());
}

double Long::f() const
{
  return PyLong_AsDouble(py());
}

size_t Long::sz() const
{
  return PyLong_AsSize_t(py());
}

ssize_t Long::ssz() const
{
  return PyLong_AsSsize_t(py());
}

Float::Float(PyObject *object) :
  Numeric(object)
{
}

Float::Float(double f) :
  Numeric(PyFloat_FromDouble(f))
{
}

long Float::l() const
{
  return (long)f();
}

unsigned long Float::ul() const
{
  return (unsigned long)f();
}

long long Float::ll() const
{
  return (long long)f();
}

unsigned long long Float::ull() const
{
  return (unsigned long long)f();
}

double Float::f() const
{
  return PyFloat_AsDouble(py());
}

size_t Float::sz() const
{
  return (size_t)f();
}

ssize_t Float::ssz() const
{
  return (ssize_t)f();
}

Boolean::Boolean(PyObject *object) :
  Object(object)
{
}

Boolean::Boolean(bool b) :
  Object(PyBool_FromLong((long)b))
{
}

bool Boolean::val() const
{
  return py() == Py_True ? true : false;
}

Unicode::Unicode(PyObject *object) :
  Object(object)
{
}

Unicode::Unicode(const char *str) :
  Object(PyUnicode_FromString(str))
{
}

Unicode::Unicode(const char *str, ssize_t sz) :
  Object(PyUnicode_FromStringAndSize(str, sz))
{
}

static char p_fmt_buf[4096];
Unicode Unicode::from_format(const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);

  int len = vsnprintf(p_fmt_buf, sizeof(p_fmt_buf), fmt, va);

  va_end(va);

  return Unicode(p_fmt_buf, len);
}

ssize_t Unicode::size() const
{
  return PyUnicode_GetLength(py());
}

std::string Unicode::str() const
{
  ssize_t sz = 0;
  const char *data = PyUnicode_AsUTF8AndSize(py(), &sz);

  return std::string(data, (size_t)sz);
}

Capsule::Capsule(PyObject *capsule) :
  Object(capsule)
{
}

Capsule::Capsule(void *ptr, const char *name) :
  Object(PyCapsule_New(ptr, name, nullptr))
{
}

void Capsule::ptr(void *p)
{
  PyCapsule_SetPointer(py(), p);
}

void *Capsule::ptr(const char *name) const
{
  return PyCapsule_GetPointer(py(), name);
}

void *Capsule::ptr() const
{
  return ptr(name());
}

void Capsule::name(const char *name)
{
  PyCapsule_SetName(py(), name);
}

const char *Capsule::name() const
{
  return PyCapsule_GetName(py());
}

void Capsule::context(void *ctx)
{
  PyCapsule_SetContext(py(), ctx);
}

void *Capsule::context() const
{
  return PyCapsule_GetContext(py());
}

Bytes::Bytes(PyObject *object) :
  Object(object)
{
}

Bytes::Bytes(const char *str) :
  Object(PyBytes_FromString(str))
{
}

Bytes::Bytes(const char *str, ssize_t sz) :
  Object(PyBytes_FromStringAndSize(str, sz))
{
}

ssize_t Bytes::size() const
{
  return PyBytes_Size(py());
}

std::string Bytes::str() const
{
  return c_str();
}

const char *Bytes::c_str() const
{
  return PyBytes_AsString(py());
}

const void *Bytes::data() const
{
  return c_str();
}

std::string Type::name() const
{
  return attr("__name__").str();
}

}