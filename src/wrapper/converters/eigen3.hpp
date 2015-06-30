#pragma once
#include "support/uref.hpp"

#include <boost/python/to_python_converter.hpp>
#include <boost/python/tuple.hpp>
#include <boost/python/cast.hpp>

#define NPY_NO_DEPRECATED_API NPY_1_8_API_VERSION
#include <numpy/ndarrayobject.h>

#include <cstdint>

namespace bp = boost::python;


/// type map: C++ type to numpy type
template<class T> struct dtype;
template<> struct dtype<bool> { static constexpr auto value = NPY_BOOL; };
template<> struct dtype<float> { static constexpr auto value = NPY_FLOAT; };
template<> struct dtype<double> { static constexpr auto value = NPY_DOUBLE; };
template<> struct dtype<std::complex<float>> { static constexpr auto value = NPY_CFLOAT; };
template<> struct dtype<std::complex<double>> { static constexpr auto value = NPY_CDOUBLE; };
template<> struct dtype<std::int8_t> { static constexpr auto value = NPY_INT8; };
template<> struct dtype<std::int16_t> { static constexpr auto value = NPY_INT16; };
template<> struct dtype<std::int32_t> { static constexpr auto value = NPY_INT32; };
template<> struct dtype<std::int64_t> { static constexpr auto value = NPY_INT64; };
template<> struct dtype<std::uint8_t> { static constexpr auto value = NPY_UINT8; };
template<> struct dtype<std::uint16_t> { static constexpr auto value = NPY_UINT16; };
template<> struct dtype<std::uint32_t> { static constexpr auto value = NPY_UINT32; };
template<> struct dtype<std::uint64_t> { static constexpr auto value = NPY_UINT64; };

template<>
struct bp::base_type_traits<PyArrayObject> : std::true_type {};

struct denseuref_to_python {
    static PyObject* convert(const DenseURef& u) {
        int ndim = (u.rows == 1 || u.cols == 1) ? 1 : 2;

        npy_intp shape[ndim];
        if (ndim == 1) { // row or column vector
            shape[0] = u.is_row_major ? u.cols : u.rows;
        }
        else { // matrix
            shape[0] = u.rows;
            shape[1] = u.cols;
        }

        auto const type = [&]{
            switch (u.type) {
                case ScalarType::f: return NPY_FLOAT;
                case ScalarType::cf: return NPY_CFLOAT;
                case ScalarType::d: return NPY_DOUBLE;
                case ScalarType::cd: return NPY_CDOUBLE;
                case ScalarType::i8: return NPY_INT8;
                case ScalarType::i16: return NPY_INT16;
                case ScalarType::i32: return NPY_INT32;
                case ScalarType::u8: return NPY_UINT8;
                case ScalarType::u16: return NPY_UINT16;
                case ScalarType::u32: return NPY_UINT32;
                default: return NPY_VOID;
            }
        }();

        int flags = u.is_row_major ? NPY_ARRAY_CARRAY : NPY_ARRAY_FARRAY;

        // ndarray from existing data -> it does not own the data and will not delete it
        return PyArray_New(&PyArray_Type, ndim, shape, type, nullptr,
                           (void*)u.data, 0, flags, nullptr);
    }

    static const PyTypeObject* get_pytype() { return &PyArray_Type; }
};

template<class EigenType>
struct eigen3_to_numpy {
    static PyObject* convert(const EigenType& eigen_object) {
        constexpr int ndim = EigenType::IsVectorAtCompileTime ? 1 : 2;

        npy_intp shape[ndim];
        if (ndim == 1) { // row or column vector
            shape[0] = EigenType::IsRowMajor ? eigen_object.cols() : eigen_object.rows();
        }
        else { // matrix
            shape[0] = eigen_object.rows();
            shape[1] = eigen_object.cols();
        }

        using scalar_t = typename EigenType::Scalar;
        int flags = EigenType::IsRowMajor ? NPY_ARRAY_CARRAY : NPY_ARRAY_FARRAY;

        // new empty ndarray of the correct type and shape
        PyObject* array = PyArray_New(&PyArray_Type, ndim, shape, dtype<scalar_t>::value,
                                      nullptr, nullptr, 0, flags, nullptr);
        std::memcpy(
            PyArray_DATA(bp::downcast<PyArrayObject>(array)),
            eigen_object.data(),
            eigen_object.size() * sizeof(scalar_t)
        );
        return array;
    }

    static const PyTypeObject* get_pytype() { return &PyArray_Type; }
};

/**
 Helper function that will construct an eigen vector or matrix
 */
template<class EigenType, int ndim, bool const_size> struct construct_eigen;

template<class EigenType>
struct construct_eigen<EigenType, 1, false> {
    static void exec(void* storage, PyArrayObject* ndarray, npy_intp const* shape) {
        new (storage) EigenType(shape[0]);
        auto& v = *static_cast<EigenType*>(storage);

        auto data = static_cast<typename EigenType::Scalar*>(PyArray_DATA(ndarray));
        std::copy_n(data, v.size(), v.data());
    }
};

template<class EigenType>
struct construct_eigen<EigenType, 2, false> {
    static void exec(void* storage, PyArrayObject* ndarray, npy_intp const* shape) {
        new (storage) EigenType(shape[0], shape[1]);
        auto& v = *static_cast<EigenType*>(storage);

        auto data = static_cast<typename EigenType::Scalar*>(PyArray_DATA(ndarray));
        std::copy_n(data, v.rows() * v.cols(), v.data());
    }
};

template<class EigenType>
struct construct_eigen<EigenType, 1, true> {
    static void exec(void* storage, PyArrayObject* ndarray, npy_intp const* shape) {
        new (storage) EigenType(EigenType::Zero());
        auto& v = *static_cast<EigenType*>(storage);

        auto data = static_cast<typename EigenType::Scalar*>(PyArray_DATA(ndarray));
        std::copy_n(data, shape[0], v.data());
    }
};


template<class EigenType>
struct numpy_to_eigen3 {
    numpy_to_eigen3() {
        bp::converter::registry::insert_rvalue_converter(
            &convertible, &construct, bp::type_id<EigenType>(), &PyArray_Type
        );
    }
    
    static void* convertible(PyObject* p) {
        constexpr auto ndim = EigenType::IsVectorAtCompileTime ? 1 : 2;

        // try to make an ndarray from the python object
        auto ndarray = bp::handle<PyArrayObject>{bp::allow_null(PyArray_FROMANY(
            p, dtype<typename EigenType::Scalar>::value, ndim, ndim,
            EigenType::IsRowMajor ? NPY_ARRAY_C_CONTIGUOUS : NPY_ARRAY_F_CONTIGUOUS
        ))};
        
        if (!ndarray)
            return nullptr;
        if (EigenType::IsRowMajor && !PyArray_IS_C_CONTIGUOUS(ndarray.get()))
            return nullptr; // row major only accepts C-style array
        if (!EigenType::IsRowMajor && !PyArray_IS_F_CONTIGUOUS(ndarray.get()))
            return nullptr; // column major only accepts Fortran-style array

        return p;
    }
    
    static void construct(PyObject* p, bp::converter::rvalue_from_python_stage1_data* data) {
        // get the pointer to memory where to construct the new eigen3 object
        void* storage = ((bp::converter::rvalue_from_python_storage<EigenType>*)
                         data)->storage.bytes;
     
        constexpr int ndim = EigenType::IsVectorAtCompileTime ? 1 : 2;

        auto ndarray = bp::handle<PyArrayObject>{PyArray_FROMANY(
            p, dtype<typename EigenType::Scalar>::value, ndim, ndim,
            EigenType::IsRowMajor ? NPY_ARRAY_C_CONTIGUOUS : NPY_ARRAY_F_CONTIGUOUS
        )};
        auto shape = PyArray_SHAPE(ndarray.get());

        // in-place construct a new eigen3 object using data from the numpy array
        construct_eigen<EigenType, ndim, (EigenType::SizeAtCompileTime > 0)>::exec(
            storage, ndarray.get(), shape
        );
        
        // save the pointer to the eigen3 object for later use by boost.python
        data->convertible = storage;
    }
};

template<class EigenType>
inline void eigen3_numpy_register_type() {
    numpy_to_eigen3<EigenType>{};
    bp::to_python_converter<EigenType, eigen3_to_numpy<EigenType>>{};
}
