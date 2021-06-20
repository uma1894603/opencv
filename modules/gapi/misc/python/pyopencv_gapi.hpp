#ifndef OPENCV_GAPI_PYOPENCV_GAPI_HPP
#define OPENCV_GAPI_PYOPENCV_GAPI_HPP

#ifdef HAVE_OPENCV_GAPI

#ifdef _MSC_VER
#pragma warning(disable: 4503)  // "decorated name length exceeded"
#endif

#include <opencv2/gapi/cpu/gcpukernel.hpp>
#include <opencv2/gapi/python/python.hpp>

// NB: Python wrapper replaces :: with _ for classes
using gapi_GKernelPackage        = cv::gapi::GKernelPackage;
using gapi_GNetPackage           = cv::gapi::GNetPackage;
using gapi_ie_PyParams           = cv::gapi::ie::PyParams;
using gapi_wip_IStreamSource_Ptr = cv::Ptr<cv::gapi::wip::IStreamSource>;
using detail_ExtractArgsCallback = cv::detail::ExtractArgsCallback;
using detail_ExtractMetaCallback = cv::detail::ExtractMetaCallback;

// NB: Python wrapper generate T_U for T<U>
// This behavior is only observed for inputs
using GOpaque_bool    = cv::GOpaque<bool>;
using GOpaque_int     = cv::GOpaque<int>;
using GOpaque_double  = cv::GOpaque<double>;
using GOpaque_float   = cv::GOpaque<double>;
using GOpaque_string  = cv::GOpaque<std::string>;
using GOpaque_Point2i = cv::GOpaque<cv::Point>;
using GOpaque_Point2f = cv::GOpaque<cv::Point2f>;
using GOpaque_Size    = cv::GOpaque<cv::Size>;
using GOpaque_Rect    = cv::GOpaque<cv::Rect>;

using GArray_bool    = cv::GArray<bool>;
using GArray_int     = cv::GArray<int>;
using GArray_double  = cv::GArray<double>;
using GArray_float   = cv::GArray<double>;
using GArray_string  = cv::GArray<std::string>;
using GArray_Point2i = cv::GArray<cv::Point>;
using GArray_Point2f = cv::GArray<cv::Point2f>;
using GArray_Size    = cv::GArray<cv::Size>;
using GArray_Rect    = cv::GArray<cv::Rect>;
using GArray_Scalar  = cv::GArray<cv::Scalar>;
using GArray_Mat     = cv::GArray<cv::Mat>;
using GArray_GMat    = cv::GArray<cv::GMat>;
using GArray_Prim    = cv::GArray<cv::gapi::wip::draw::Prim>;

// FIXME: Python wrapper generate code without namespace std,
// so it cause error: "string wasn't declared"
// WA: Create using
using std::string;

namespace cv
{
namespace detail
{

class PyObjectHolder
{
public:
    PyObjectHolder(PyObject* o, bool owner = true);
    PyObject* get() const;

private:
    class Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace detail
} // namespace cv

class cv::detail::PyObjectHolder::Impl
{
public:
    Impl(PyObject* object, bool owner);
    PyObject* get() const;
    ~Impl();

private:
    PyObject* m_object;
};

cv::detail::PyObjectHolder::Impl::Impl(PyObject* object, bool owner)
    : m_object(object)
{
    // NB: Become an owner of that PyObject.
    // Need to store this and get access
    // after the caller which provide the object is out of range.
    if (owner)
    {
        // NB: Impossible take ownership if object is NULL.
        GAPI_Assert(object);
        Py_INCREF(m_object);
    }
}

cv::detail::PyObjectHolder::Impl::~Impl()
{
    // NB: If NULL was set, don't decrease counter.
    if (m_object)
    {
        Py_DECREF(m_object);
    }
}

PyObject* cv::detail::PyObjectHolder::Impl::get() const
{
    return m_object;
}

cv::detail::PyObjectHolder::PyObjectHolder(PyObject* object, bool owner)
        : m_impl(new cv::detail::PyObjectHolder::Impl{object, owner})
{
}

PyObject* cv::detail::PyObjectHolder::get() const
{
    return m_impl->get();
}

template<>
PyObject* pyopencv_from(const cv::detail::PyObjectHolder& v)
{
    PyObject* o = cv::util::any_cast<cv::detail::PyObjectHolder>(v).get();
    Py_INCREF(o);
    return o;
}

// #FIXME: It's possible to write pyopencv_from function for every variant.
template <>
PyObject* pyopencv_from(const cv::gapi::wip::draw::Prim& prim)
{
    return NULL;
}

template<>
bool pyopencv_to(PyObject* obj, cv::gapi::wip::draw::Prim& value, const ArgInfo& info)
{
    cv::gapi::wip::draw::Rect rect;
    if (pyopencv_to_safe(obj, rect, info))
    {
        value = rect;
        return true;
    }
    return false;
}

template<>
PyObject* pyopencv_from(const cv::GArg& value)
{
    GAPI_Assert(value.kind != cv::detail::ArgKind::GOBJREF);
#define HANDLE_CASE(T, O) case cv::detail::OpaqueKind::CV_##T:  \
    {                                                           \
        return pyopencv_from(value.get<O>());                   \
    }

#define UNSUPPORTED(T) case cv::detail::OpaqueKind::CV_##T: break
    switch (value.opaque_kind)
    {
        HANDLE_CASE(BOOL,      bool);
        HANDLE_CASE(INT,       int);
        HANDLE_CASE(DOUBLE,    double);
        HANDLE_CASE(FLOAT,     float);
        HANDLE_CASE(STRING,    std::string);
        HANDLE_CASE(POINT,     cv::Point);
        HANDLE_CASE(POINT2F,   cv::Point2f);
        HANDLE_CASE(SIZE,      cv::Size);
        HANDLE_CASE(RECT,      cv::Rect);
        HANDLE_CASE(SCALAR,    cv::Scalar);
        HANDLE_CASE(MAT,       cv::Mat);
        HANDLE_CASE(UNKNOWN,   cv::detail::PyObjectHolder);
        HANDLE_CASE(DRAW_PRIM, cv::gapi::wip::draw::Prim);
        UNSUPPORTED(UINT64);
#undef HANDLE_CASE
#undef UNSUPPORTED
    }
    util::throw_error(std::logic_error("Unsupported kernel input type"));
}

template<>
bool pyopencv_to(PyObject* obj, cv::GArg& value, const ArgInfo& info)
{
    value = cv::GArg(cv::detail::PyObjectHolder(obj));
    return true;
}

template <>
bool pyopencv_to(PyObject* obj, std::vector<GCompileArg>& value, const ArgInfo& info)
{
    return pyopencv_to_generic_vec(obj, value, info);
}

template <>
PyObject* pyopencv_from(const std::vector<GCompileArg>& value)
{
    return pyopencv_from_generic_vec(value);
}

template <>
bool pyopencv_to(PyObject* obj, GRunArgs& value, const ArgInfo& info)
{
    return pyopencv_to_generic_vec(obj, value, info);
}

template<>
PyObject* pyopencv_from(const cv::detail::OpaqueRef& o)
{
    switch (o.getKind())
    {
        case cv::detail::OpaqueKind::CV_BOOL      : return pyopencv_from(o.rref<bool>());
        case cv::detail::OpaqueKind::CV_INT       : return pyopencv_from(o.rref<int>());
        case cv::detail::OpaqueKind::CV_DOUBLE    : return pyopencv_from(o.rref<double>());
        case cv::detail::OpaqueKind::CV_FLOAT     : return pyopencv_from(o.rref<float>());
        case cv::detail::OpaqueKind::CV_STRING    : return pyopencv_from(o.rref<std::string>());
        case cv::detail::OpaqueKind::CV_POINT     : return pyopencv_from(o.rref<cv::Point>());
        case cv::detail::OpaqueKind::CV_POINT2F   : return pyopencv_from(o.rref<cv::Point2f>());
        case cv::detail::OpaqueKind::CV_SIZE      : return pyopencv_from(o.rref<cv::Size>());
        case cv::detail::OpaqueKind::CV_RECT      : return pyopencv_from(o.rref<cv::Rect>());
        case cv::detail::OpaqueKind::CV_UNKNOWN   : return pyopencv_from(o.rref<cv::GArg>());
        case cv::detail::OpaqueKind::CV_DRAW_PRIM : return pyopencv_from(o.rref<cv::gapi::wip::draw::Prim>());
        case cv::detail::OpaqueKind::CV_UINT64    : break;
        case cv::detail::OpaqueKind::CV_SCALAR    : break;
        case cv::detail::OpaqueKind::CV_MAT       : break;
    }

    PyErr_SetString(PyExc_TypeError, "Unsupported GOpaque type");
    return NULL;
};

template <>
PyObject* pyopencv_from(const cv::detail::VectorRef& v)
{
    switch (v.getKind())
    {
        case cv::detail::OpaqueKind::CV_BOOL      : return pyopencv_from_generic_vec(v.rref<bool>());
        case cv::detail::OpaqueKind::CV_INT       : return pyopencv_from_generic_vec(v.rref<int>());
        case cv::detail::OpaqueKind::CV_DOUBLE    : return pyopencv_from_generic_vec(v.rref<double>());
        case cv::detail::OpaqueKind::CV_FLOAT     : return pyopencv_from_generic_vec(v.rref<float>());
        case cv::detail::OpaqueKind::CV_STRING    : return pyopencv_from_generic_vec(v.rref<std::string>());
        case cv::detail::OpaqueKind::CV_POINT     : return pyopencv_from_generic_vec(v.rref<cv::Point>());
        case cv::detail::OpaqueKind::CV_POINT2F   : return pyopencv_from_generic_vec(v.rref<cv::Point2f>());
        case cv::detail::OpaqueKind::CV_SIZE      : return pyopencv_from_generic_vec(v.rref<cv::Size>());
        case cv::detail::OpaqueKind::CV_RECT      : return pyopencv_from_generic_vec(v.rref<cv::Rect>());
        case cv::detail::OpaqueKind::CV_SCALAR    : return pyopencv_from_generic_vec(v.rref<cv::Scalar>());
        case cv::detail::OpaqueKind::CV_MAT       : return pyopencv_from_generic_vec(v.rref<cv::Mat>());
        case cv::detail::OpaqueKind::CV_UNKNOWN   : return pyopencv_from_generic_vec(v.rref<cv::GArg>());
        case cv::detail::OpaqueKind::CV_DRAW_PRIM : return pyopencv_from_generic_vec(v.rref<cv::gapi::wip::draw::Prim>());
        case cv::detail::OpaqueKind::CV_UINT64    : break;
    }

    PyErr_SetString(PyExc_TypeError, "Unsupported GArray type");
    return NULL;
}

template <>
PyObject* pyopencv_from(const GRunArg& v)
{
    switch (v.index())
    {
        case GRunArg::index_of<cv::Mat>():
            return pyopencv_from(util::get<cv::Mat>(v));

        case GRunArg::index_of<cv::Scalar>():
            return pyopencv_from(util::get<cv::Scalar>(v));

        case GRunArg::index_of<cv::detail::VectorRef>():
            return pyopencv_from(util::get<cv::detail::VectorRef>(v));

        case GRunArg::index_of<cv::detail::OpaqueRef>():
            return pyopencv_from(util::get<cv::detail::OpaqueRef>(v));
    }

    PyErr_SetString(PyExc_TypeError, "Failed to unpack GRunArgs");
    return NULL;
}

template<>
PyObject* pyopencv_from(const GRunArgs& value)
{
    size_t i, n = value.size();

    // NB: It doesn't make sense to return list with a single element
    if (n == 1)
    {
        PyObject* item = pyopencv_from(value[0]);
        if(!item)
        {
            return NULL;
        }
        return item;
    }

    PyObject* list = PyList_New(n);
    for(i = 0; i < n; ++i)
    {
        PyObject* item = pyopencv_from(value[i]);
        if(!item)
        {
            Py_DECREF(list);
            PyErr_SetString(PyExc_TypeError, "Failed to unpack GRunArgs");
            return NULL;
        }
        PyList_SetItem(list, i, item);
    }

    return list;
}

template<>
bool pyopencv_to(PyObject* obj, GMetaArgs& value, const ArgInfo& info)
{
    return pyopencv_to_generic_vec(obj, value, info);
}

template<>
PyObject* pyopencv_from(const GMetaArgs& value)
{
    return pyopencv_from_generic_vec(value);
}

template <typename T>
void pyopencv_to_with_check(PyObject* from, T& to, const std::string& msg = "")
{
    if (!pyopencv_to(from, to, ArgInfo("", false)))
    {
        cv::util::throw_error(std::logic_error(msg));
    }
}

template <typename T>
void pyopencv_to_generic_vec_with_check(PyObject* from,
                                        std::vector<T>& to,
                                        const std::string& msg = "")
{
    if (!pyopencv_to_generic_vec(from, to, ArgInfo("", false)))
    {
        cv::util::throw_error(std::logic_error(msg));
    }
}

template <typename T>
static PyObject* extract_proto_args(PyObject* py_args, PyObject* kw)
{
    using namespace cv;

    GProtoArgs args;
    Py_ssize_t size = PyTuple_Size(py_args);
    args.reserve(size);
    for (int i = 0; i < size; ++i)
    {
        PyObject* item = PyTuple_GetItem(py_args, i);
        if (PyObject_TypeCheck(item, reinterpret_cast<PyTypeObject*>(pyopencv_GScalar_TypePtr)))
        {
            args.emplace_back(reinterpret_cast<pyopencv_GScalar_t*>(item)->v);
        }
        else if (PyObject_TypeCheck(item, reinterpret_cast<PyTypeObject*>(pyopencv_GMat_TypePtr)))
        {
            args.emplace_back(reinterpret_cast<pyopencv_GMat_t*>(item)->v);
        }
        else if (PyObject_TypeCheck(item, reinterpret_cast<PyTypeObject*>(pyopencv_GOpaqueT_TypePtr)))
        {
            args.emplace_back(reinterpret_cast<pyopencv_GOpaqueT_t*>(item)->v.strip());
        }
        else if (PyObject_TypeCheck(item, reinterpret_cast<PyTypeObject*>(pyopencv_GArrayT_TypePtr)))
        {
            args.emplace_back(reinterpret_cast<pyopencv_GArrayT_t*>(item)->v.strip());
        }
        else
        {
            PyErr_SetString(PyExc_TypeError, "Unsupported type for cv.GIn()/cv.GOut()");
            return NULL;
        }
    }

    return pyopencv_from<T>(T{std::move(args)});
}

static PyObject* pyopencv_cv_GIn(PyObject* , PyObject* py_args, PyObject* kw)
{
    return extract_proto_args<GProtoInputArgs>(py_args, kw);
}

static PyObject* pyopencv_cv_GOut(PyObject* , PyObject* py_args, PyObject* kw)
{
    return extract_proto_args<GProtoOutputArgs>(py_args, kw);
}

static cv::detail::OpaqueRef extract_opaque_ref(PyObject* from, cv::detail::OpaqueKind kind)
{
#define HANDLE_CASE(T, O) case cv::detail::OpaqueKind::CV_##T:  \
{                                                               \
    O obj{};                                                    \
    pyopencv_to_with_check(from, obj, "Failed to obtain " # O); \
    return cv::detail::OpaqueRef{std::move(obj)};               \
}
#define UNSUPPORTED(T) case cv::detail::OpaqueKind::CV_##T: break
    switch (kind)
    {
        HANDLE_CASE(BOOL,    bool);
        HANDLE_CASE(INT,     int);
        HANDLE_CASE(DOUBLE,  double);
        HANDLE_CASE(FLOAT,   float);
        HANDLE_CASE(STRING,  std::string);
        HANDLE_CASE(POINT,   cv::Point);
        HANDLE_CASE(POINT2F, cv::Point2f);
        HANDLE_CASE(SIZE,    cv::Size);
        HANDLE_CASE(RECT,    cv::Rect);
        HANDLE_CASE(UNKNOWN, cv::GArg);
        UNSUPPORTED(UINT64);
        UNSUPPORTED(SCALAR);
        UNSUPPORTED(MAT);
        UNSUPPORTED(DRAW_PRIM);
#undef HANDLE_CASE
#undef UNSUPPORTED
    }
    util::throw_error(std::logic_error("Unsupported type for GOpaqueT"));
}

static cv::detail::VectorRef extract_vector_ref(PyObject* from, cv::detail::OpaqueKind kind)
{
#define HANDLE_CASE(T, O) case cv::detail::OpaqueKind::CV_##T:                        \
{                                                                                     \
    std::vector<O> obj;                                                               \
    pyopencv_to_generic_vec_with_check(from, obj, "Failed to obtain vector of " # O); \
    return cv::detail::VectorRef{std::move(obj)};                                     \
}
#define UNSUPPORTED(T) case cv::detail::OpaqueKind::CV_##T: break
    switch (kind)
    {
        HANDLE_CASE(BOOL,      bool);
        HANDLE_CASE(INT,       int);
        HANDLE_CASE(DOUBLE,    double);
        HANDLE_CASE(FLOAT,     float);
        HANDLE_CASE(STRING,    std::string);
        HANDLE_CASE(POINT,     cv::Point);
        HANDLE_CASE(POINT2F,   cv::Point2f);
        HANDLE_CASE(SIZE,      cv::Size);
        HANDLE_CASE(RECT,      cv::Rect);
        HANDLE_CASE(SCALAR,    cv::Scalar);
        HANDLE_CASE(MAT,       cv::Mat);
        HANDLE_CASE(UNKNOWN,   cv::GArg);
        HANDLE_CASE(DRAW_PRIM, cv::gapi::wip::draw::Prim);
        UNSUPPORTED(UINT64);
#undef HANDLE_CASE
#undef UNSUPPORTED
    }
    util::throw_error(std::logic_error("Unsupported type for GArrayT"));
}

static cv::GRunArg extract_run_arg(const cv::GTypeInfo& info, PyObject* item)
{
    switch (info.shape)
    {
        case cv::GShape::GMAT:
        {
            // NB: In case streaming it can be IStreamSource or cv::Mat
            if (PyObject_TypeCheck(item,
                        reinterpret_cast<PyTypeObject*>(pyopencv_gapi_wip_IStreamSource_TypePtr)))
            {
                cv::gapi::wip::IStreamSource::Ptr source =
                    reinterpret_cast<pyopencv_gapi_wip_IStreamSource_t*>(item)->v;
                return source;
            }
            cv::Mat obj;
            pyopencv_to_with_check(item, obj, "Failed to obtain cv::Mat");
            return obj;
        }
        case cv::GShape::GSCALAR:
        {
            cv::Scalar obj;
            pyopencv_to_with_check(item, obj, "Failed to obtain cv::Scalar");
            return obj;
        }
        case cv::GShape::GOPAQUE:
        {
            return extract_opaque_ref(item, info.kind);
        }
        case cv::GShape::GARRAY:
        {
            return extract_vector_ref(item, info.kind);
        }
        case cv::GShape::GFRAME:
        {
            // NB: Isn't supported yet.
            break;
        }
    }

    util::throw_error(std::logic_error("Unsupported output shape"));
}

static cv::GRunArgs extract_run_args(const cv::GTypesInfo& info, PyObject* py_args)
{
    cv::GRunArgs args;
    Py_ssize_t tuple_size = PyTuple_Size(py_args);
    args.reserve(tuple_size);

    for (int i = 0; i < tuple_size; ++i)
    {
        args.push_back(extract_run_arg(info[i], PyTuple_GetItem(py_args, i)));
    }

    return args;
}

static cv::GMetaArg extract_meta_arg(const cv::GTypeInfo& info, PyObject* item)
{
    switch (info.shape)
    {
        case cv::GShape::GMAT:
        {
            cv::Mat obj;
            pyopencv_to_with_check(item, obj, "Failed to obtain cv::Mat");
            return cv::GMetaArg{cv::descr_of(obj)};
        }
        case cv::GShape::GSCALAR:
        {
            cv::Scalar obj;
            pyopencv_to_with_check(item, obj, "Failed to obtain cv::Scalar");
            return cv::GMetaArg{cv::descr_of(obj)};
        }
        case cv::GShape::GARRAY:
        {
            return cv::GMetaArg{cv::empty_array_desc()};
        }
        case cv::GShape::GOPAQUE:
        {
            return cv::GMetaArg{cv::empty_gopaque_desc()};
        }
        case cv::GShape::GFRAME:
        {
            // NB: Isn't supported yet.
            break;
        }
    }
    util::throw_error(std::logic_error("Unsupported output shape"));
}

static cv::GMetaArgs extract_meta_args(const cv::GTypesInfo& info, PyObject* py_args)
{
    cv::GMetaArgs metas;
    Py_ssize_t tuple_size = PyTuple_Size(py_args);
    metas.reserve(tuple_size);

    for (int i = 0; i < tuple_size; ++i)
    {
        metas.push_back(extract_meta_arg(info[i], PyTuple_GetItem(py_args, i)));
    }

    return metas;
}

static cv::GRunArgs run_py_kernel(cv::detail::PyObjectHolder kernel,
                                  const cv::gapi::python::GPythonContext &ctx)
{
    const auto& ins      = ctx.ins;
    const auto& in_metas = ctx.in_metas;
    const auto& out_info = ctx.out_info;

    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    cv::GRunArgs outs;
    try
    {
        int in_idx = 0;
        // NB: Doesn't increase reference counter (false),
        // because PyObject already have ownership.
        // In case exception decrement reference counter.
        cv::detail::PyObjectHolder args(PyTuple_New(ins.size()), false);
        for (size_t i = 0; i < ins.size(); ++i)
        {
            // NB: If meta is monostate then object isn't associated with G-TYPE.
            if (cv::util::holds_alternative<cv::util::monostate>(in_metas[i]))
            {
                PyTuple_SetItem(args.get(), i, pyopencv_from(ins[i]));
                continue;
            }

            switch (in_metas[i].index())
            {
                case cv::GMetaArg::index_of<cv::GMatDesc>():
                    PyTuple_SetItem(args.get(), i, pyopencv_from(ins[i].get<cv::Mat>()));
                    break;
                case cv::GMetaArg::index_of<cv::GScalarDesc>():
                    PyTuple_SetItem(args.get(), i, pyopencv_from(ins[i].get<cv::Scalar>()));
                    break;
                case cv::GMetaArg::index_of<cv::GOpaqueDesc>():
                    PyTuple_SetItem(args.get(), i, pyopencv_from(ins[i].get<cv::detail::OpaqueRef>()));
                    break;
                case cv::GMetaArg::index_of<cv::GArrayDesc>():
                    PyTuple_SetItem(args.get(), i, pyopencv_from(ins[i].get<cv::detail::VectorRef>()));
                    break;
                case cv::GMetaArg::index_of<cv::GFrameDesc>():
                    util::throw_error(std::logic_error("GFrame isn't supported for custom operation"));
                    break;
            }
            ++in_idx;
        }
        // NB: Doesn't increase reference counter (false).
        // In case PyObject_CallObject return NULL, do nothing in destructor.
        cv::detail::PyObjectHolder result(
                PyObject_CallObject(kernel.get(), args.get()), false);

        if (PyErr_Occurred()) {
            PyErr_PrintEx(0);
            PyErr_Clear();
            throw std::logic_error("Python kernel failed with error!");
        }
        // NB: In fact it's impossible situation, becase errors were handled above.
        GAPI_Assert(result.get() && "Python kernel returned NULL!");

        outs = out_info.size() == 1 ? cv::GRunArgs{extract_run_arg(out_info[0], result.get())}
                                    : extract_run_args(out_info, result.get());
    }
    catch (...)
    {
        PyGILState_Release(gstate);
        throw;
    }
    PyGILState_Release(gstate);

    return outs;
}

static GMetaArg get_meta_arg(PyObject* obj)
{
    if (PyObject_TypeCheck(obj,
                reinterpret_cast<PyTypeObject*>(pyopencv_GMatDesc_TypePtr)))
    {
        return cv::GMetaArg{reinterpret_cast<pyopencv_GMatDesc_t*>(obj)->v};
    }
    else if (PyObject_TypeCheck(obj,
                reinterpret_cast<PyTypeObject*>(pyopencv_GScalarDesc_TypePtr)))
    {
        return cv::GMetaArg{reinterpret_cast<pyopencv_GScalarDesc_t*>(obj)->v};
    }
    else if (PyObject_TypeCheck(obj,
                reinterpret_cast<PyTypeObject*>(pyopencv_GArrayDesc_TypePtr)))
    {
        return cv::GMetaArg{reinterpret_cast<pyopencv_GArrayDesc_t*>(obj)->v};
    }
    else if (PyObject_TypeCheck(obj,
                reinterpret_cast<PyTypeObject*>(pyopencv_GOpaqueDesc_TypePtr)))
    {
        return cv::GMetaArg{reinterpret_cast<pyopencv_GOpaqueDesc_t*>(obj)->v};
    }
    else
    {
        util::throw_error(std::logic_error("Unsupported output meta type"));
    }
}

static cv::GMetaArgs get_meta_args(PyObject* tuple)
{
    size_t size = PyTuple_Size(tuple);

    cv::GMetaArgs metas;
    metas.reserve(size);
    for (size_t i = 0; i < size; ++i)
    {
        metas.push_back(get_meta_arg(PyTuple_GetItem(tuple, i)));
    }

    return metas;
}

static GMetaArgs run_py_meta(cv::detail::PyObjectHolder out_meta,
                            const cv::GMetaArgs         &meta,
                            const cv::GArgs             &gargs) {
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    cv::GMetaArgs out_metas;
    try
    {
        // NB: Doesn't increase reference counter (false),
        // because PyObject already have ownership.
        // In case exception decrement reference counter.
        cv::detail::PyObjectHolder args(PyTuple_New(meta.size()), false);
        size_t idx = 0;
        for (auto&& m : meta)
        {
            switch (m.index())
            {
                case cv::GMetaArg::index_of<cv::GMatDesc>():
                    PyTuple_SetItem(args.get(), idx, pyopencv_from(cv::util::get<cv::GMatDesc>(m)));
                    break;
                case cv::GMetaArg::index_of<cv::GScalarDesc>():
                    PyTuple_SetItem(args.get(), idx, pyopencv_from(cv::util::get<cv::GScalarDesc>(m)));
                    break;
                case cv::GMetaArg::index_of<cv::GArrayDesc>():
                    PyTuple_SetItem(args.get(), idx, pyopencv_from(cv::util::get<cv::GArrayDesc>(m)));
                    break;
                case cv::GMetaArg::index_of<cv::GOpaqueDesc>():
                    PyTuple_SetItem(args.get(), idx, pyopencv_from(cv::util::get<cv::GOpaqueDesc>(m)));
                    break;
                case cv::GMetaArg::index_of<cv::util::monostate>():
                    PyTuple_SetItem(args.get(), idx, pyopencv_from(gargs[idx]));
                    break;
                case cv::GMetaArg::index_of<cv::GFrameDesc>():
                    util::throw_error(std::logic_error("GFrame isn't supported for custom operation"));
                    break;
            }
            ++idx;
        }
        // NB: Doesn't increase reference counter (false).
        // In case PyObject_CallObject return NULL, do nothing in destructor.
        cv::detail::PyObjectHolder result(
                PyObject_CallObject(out_meta.get(), args.get()), false);

        if (PyErr_Occurred()) {
            PyErr_PrintEx(0);
            PyErr_Clear();
            throw std::logic_error("Python outMeta failed with error!");
        }
        // NB: In fact it's impossible situation, becase errors were handled above.
        GAPI_Assert(result.get() && "Python outMeta returned NULL!");

        out_metas = PyTuple_Check(result.get()) ? get_meta_args(result.get())
                                                : cv::GMetaArgs{get_meta_arg(result.get())};
    }
    catch (...)
    {
        PyGILState_Release(gstate);
        throw;
    }
    PyGILState_Release(gstate);

    return out_metas;
}

static PyObject* pyopencv_cv_gapi_kernels(PyObject* , PyObject* py_args, PyObject*)
{
    using namespace cv;
    gapi::GKernelPackage pkg;
    Py_ssize_t size = PyTuple_Size(py_args);

    for (int i = 0; i < size; ++i)
    {
        PyObject* user_kernel = PyTuple_GetItem(py_args, i);

        PyObject* id_obj = PyObject_GetAttrString(user_kernel, "id");
        if (!id_obj) {
            PyErr_SetString(PyExc_TypeError,
                    "Python kernel should contain id, please use cv.gapi.kernel to define kernel");
            return NULL;
        }

        PyObject* out_meta = PyObject_GetAttrString(user_kernel, "outMeta");
        if (!out_meta) {
            PyErr_SetString(PyExc_TypeError,
                    "Python kernel should contain outMeta, please use cv.gapi.kernel to define kernel");
            return NULL;
        }

        PyObject* run  = PyObject_GetAttrString(user_kernel, "run");
        if (!run) {
            PyErr_SetString(PyExc_TypeError,
                    "Python kernel should contain run, please use cv.gapi.kernel to define kernel");
            return NULL;
        }

        std::string id;
        if (!pyopencv_to(id_obj, id, ArgInfo("id", false)))
        {
            PyErr_SetString(PyExc_TypeError, "Failed to obtain string");
            return NULL;
        }

        using namespace std::placeholders;
        gapi::python::GPythonFunctor f(id.c_str(),
                std::bind(run_py_meta  , cv::detail::PyObjectHolder{out_meta}, _1, _2),
                std::bind(run_py_kernel, cv::detail::PyObjectHolder{run}    , _1));
        pkg.include(f);
    }
    return pyopencv_from(pkg);
}

static PyObject* pyopencv_cv_gapi_networks(PyObject*, PyObject* py_args, PyObject*)
{
    using namespace cv;
    gapi::GNetPackage pkg;
    Py_ssize_t size = PyTuple_Size(py_args);
    for (int i = 0; i < size; ++i)
    {
        gapi_ie_PyParams params;
        PyObject* item = PyTuple_GetItem(py_args, i);
        if (pyopencv_to(item, params, ArgInfo("PyParams", false)))
        {
            pkg += gapi::networks(params);
        }
    }
    return pyopencv_from(pkg);
}

static PyObject* pyopencv_cv_gapi_op(PyObject* , PyObject* py_args, PyObject*)
{
    using namespace cv;
    Py_ssize_t size = PyTuple_Size(py_args);
    std::string id;
    if (!pyopencv_to(PyTuple_GetItem(py_args, 0), id, ArgInfo("id", false)))
    {
        PyErr_SetString(PyExc_TypeError, "Failed to obtain: operation id must be a string");
        return NULL;
    }
    PyObject* outMeta = PyTuple_GetItem(py_args, 1);

    cv::GArgs args;
    for (int i = 2; i < size; i++)
    {
        PyObject* item = PyTuple_GetItem(py_args, i);
        if (PyObject_TypeCheck(item,
                    reinterpret_cast<PyTypeObject*>(pyopencv_GMat_TypePtr)))
        {
            args.emplace_back(reinterpret_cast<pyopencv_GMat_t*>(item)->v);
        }
        else if (PyObject_TypeCheck(item,
                           reinterpret_cast<PyTypeObject*>(pyopencv_GScalar_TypePtr)))
        {
            args.emplace_back(reinterpret_cast<pyopencv_GScalar_t*>(item)->v);
        }
        else if (PyObject_TypeCheck(item,
                           reinterpret_cast<PyTypeObject*>(pyopencv_GOpaqueT_TypePtr)))
        {
            auto&& arg = reinterpret_cast<pyopencv_GOpaqueT_t*>(item)->v.arg();
#define HC(T, K) case cv::GOpaqueT::Storage:: index_of<cv::GOpaque<T>>(): \
            args.emplace_back(cv::util::get<cv::GOpaque<T>>(arg));        \
            break;                                                        \

            SWITCH(arg.index(), GOPAQUE_TYPE_LIST_G, HC)
#undef HC
        }
        else if (PyObject_TypeCheck(item,
                           reinterpret_cast<PyTypeObject*>(pyopencv_GArrayT_TypePtr)))
        {
            auto&& arg = reinterpret_cast<pyopencv_GArrayT_t*>(item)->v.arg();
#define HC(T, K) case cv::GArrayT::Storage:: index_of<cv::GArray<T>>(): \
            args.emplace_back(cv::util::get<cv::GArray<T>>(arg));       \
            break;                                                      \

            SWITCH(arg.index(), GARRAY_TYPE_LIST_G, HC)
#undef HC
        }
        else
        {
            args.emplace_back(cv::GArg(cv::detail::PyObjectHolder{item}));
        }
    }

    cv::GKernel::M outMetaWrapper = std::bind(run_py_meta,
                                              cv::detail::PyObjectHolder{outMeta},
                                              std::placeholders::_1,
                                              std::placeholders::_2);
    return pyopencv_from(cv::gapi::wip::op(id, outMetaWrapper, std::move(args)));
}

static PyObject* pyopencv_cv_gin(PyObject*, PyObject* py_args, PyObject*)
{
    cv::detail::PyObjectHolder holder{py_args};
    auto callback = cv::detail::ExtractArgsCallback{[=](const cv::GTypesInfo& info)
        {
            PyGILState_STATE gstate;
            gstate = PyGILState_Ensure();

            cv::GRunArgs args;
            try
            {
                args = extract_run_args(info, holder.get());
            }
            catch (...)
            {
                PyGILState_Release(gstate);
                throw;
            }
            PyGILState_Release(gstate);
            return args;
        }};

    return pyopencv_from(callback);
}

static PyObject* pyopencv_cv_descr_of(PyObject*, PyObject* py_args, PyObject*)
{
    Py_INCREF(py_args);
    auto callback = cv::detail::ExtractMetaCallback{[=](const cv::GTypesInfo& info)
        {
            PyGILState_STATE gstate;
            gstate = PyGILState_Ensure();

            cv::GMetaArgs args;
            try
            {
                args = extract_meta_args(info, py_args);
            }
            catch (...)
            {
                PyGILState_Release(gstate);
                throw;
            }
            PyGILState_Release(gstate);
            return args;
        }};
    return pyopencv_from(callback);
}

template<typename T>
struct PyOpenCV_Converter<cv::GArray<T>>
{
    static PyObject* from(const cv::GArray<T>& p)
    {
        return pyopencv_from(cv::GArrayT(p));
    }
    static bool to(PyObject *obj, cv::GArray<T>& value, const ArgInfo& info)
    {
        if (PyObject_TypeCheck(obj, reinterpret_cast<PyTypeObject*>(pyopencv_GArrayT_TypePtr)))
        {
            auto& array = reinterpret_cast<pyopencv_GArrayT_t*>(obj)->v;
            try {
                value = cv::util::get<cv::GArray<T>>(array.arg());
            } catch (...) {
                return false;
            }
            return true;
        }
        return false;
    }
};

template<typename T>
struct PyOpenCV_Converter<cv::GOpaque<T>>
{
    static PyObject* from(const cv::GOpaque<T>& p)
    {
        return pyopencv_from(cv::GOpaqueT(p));
    }
    static bool to(PyObject *obj, cv::GOpaque<T>& value, const ArgInfo& info)
    {
        if (PyObject_TypeCheck(obj, reinterpret_cast<PyTypeObject*>(pyopencv_GOpaqueT_TypePtr)))
        {
            auto& opaque = reinterpret_cast<pyopencv_GOpaqueT_t*>(obj)->v;
            try {
                value = cv::util::get<cv::GOpaque<T>>(opaque.arg());
            } catch (...) {
                return false;
            }
            return true;
        }
        return false;
    }
};


// extend cv.gapi methods
#define PYOPENCV_EXTRA_METHODS_GAPI \
  {"kernels", CV_PY_FN_WITH_KW(pyopencv_cv_gapi_kernels), "kernels(...) -> GKernelPackage"}, \
  {"networks", CV_PY_FN_WITH_KW(pyopencv_cv_gapi_networks), "networks(...) -> GNetPackage"}, \
  {"__op", CV_PY_FN_WITH_KW(pyopencv_cv_gapi_op), "__op(...) -> retval\n"},


#endif  // HAVE_OPENCV_GAPI
#endif  // OPENCV_GAPI_PYOPENCV_GAPI_HPP
