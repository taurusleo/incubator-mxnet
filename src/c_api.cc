/*!
 *  Copyright (c) 2015 by Contributors
 * \file c_api.cc
 * \brief C API of mxnet
 */
#include <dmlc/base.h>
#include <dmlc/logging.h>
#include <dmlc/io.h>
#include <dmlc/memory_io.h>
#include <mxnet/base.h>
#include <mxnet/narray.h>
#include <mxnet/symbolic.h>
#include <mxnet/operator.h>
#include <mxnet/io.h>
#include <mxnet/c_api.h>
#include <vector>
#include <sstream>
#include <string>
#include <mutex>
#include <memory>

// macro hanlding for threadlocal variables
#ifdef __GNUC__
  #define MX_TREAD_LOCAL __thread
#elif __STDC_VERSION__ >= 201112L
  #define  MX_TREAD_LOCAL _Thread_local
#elif defined(_MSC_VER)
  #define MX_TREAD_LOCAL __declspec(thread)
#endif

#ifndef MX_TREAD_LOCAL
#message("Warning: Threadlocal is not enabled");
#endif

using namespace mxnet;

/*! \brief entry to to easily hold returning information */
struct MXAPIThreadLocalEntry {
  /*! \brief holds last error message */
  std::string last_error;
  /*! \brief result holder for returning string */
  std::string ret_str;
  /*! \brief result holder for returning strings */
  std::vector<std::string> ret_vec_str;
  /*! \brief result holder for returning string pointers */
  std::vector<const char *> ret_vec_charp;
  /*! \brief result holder for returning handles */
  std::vector<void *> ret_handles;
  /*! \brief result holder for returning shapes */
  std::vector<TShape> arg_shapes, out_shapes, aux_shapes;
  /*! \brief result holder for returning shape dimensions */
  std::vector<mx_uint> arg_shape_ndim, out_shape_ndim, aux_shape_ndim;
  /*! \brief result holder for returning shape pointer */
  std::vector<const mx_uint*> arg_shape_data, out_shape_data, aux_shape_data;
  // helper function to setup return value of shape array
  inline static void SetupShapeArrayReturn(
      const std::vector<TShape> &shapes,
      std::vector<mx_uint> *ndim,
      std::vector<const mx_uint*> *data) {
    ndim->resize(shapes.size());
    data->resize(shapes.size());
    for (size_t i = 0; i < shapes.size(); ++i) {
      ndim->at(i) = shapes[i].ndim();
      data->at(i) = shapes[i].data();
    }
  }
};

/*!
 * \brief A threadlocal store to store threadlocal variables.
 *  Will return a thread local singleton of type T
 * \tparam T the type we like to store
 */
class MXAPIThreadLocalStore {
 public:
  /*! \brief store return entry */
  typedef MXAPIThreadLocalEntry T;
  /*! \return get a thread local singleton */
  static T* Get() {
    static MX_TREAD_LOCAL T* ptr = nullptr;
    if (ptr == nullptr) {
      ptr = new T();
      Singleton()->RegisterDelete(ptr);
    }
    return ptr;
  }

 private:
  /*! \brief constructor */
  MXAPIThreadLocalStore() {}
  /*! \brief destructor */
  ~MXAPIThreadLocalStore() {
    for (size_t i = 0; i < data_.size(); ++i) {
      delete data_[i];
    }
  }
  /*! \return singleton of the store */
  static MXAPIThreadLocalStore *Singleton() {
    static MXAPIThreadLocalStore inst;
    return &inst;
  }
  /*!
   * \brief register str for internal deletion
   * \param str the string pointer
   */
  void RegisterDelete(T *str) {
    std::unique_lock<std::mutex> lock(mutex_);
    data_.push_back(str);
    lock.unlock();
  }
  /*! \brief internal mutex */
  std::mutex mutex_;
  /*!\brief internal data */
  std::vector<T*> data_;
};

// NOTE: all functions return 0 upon success
// consider add try/catch block for user error
// handling in the future

/*! \brief  macro to guard beginning and end section of all functions */
#define API_BEGIN() try {
/*! \brief every function starts with API_BEGIN();
     and finishes with API_END() or API_END_HANDLE_ERROR */
#define API_END() } catch(dmlc::Error &_except_) { return MXHandleException(_except_); } return 0;
/*!
 * \brief every function starts with API_BEGIN();
 *   and finishes with API_END() or API_END_HANDLE_ERROR
 *   The finally clause contains procedure to cleanup states when an error happens.
 */
#define API_END_HANDLE_ERROR(Finalize) } catch(dmlc::Error &_except_) { Finalize; return MXHandleException(_except_); } return 0; // NOLINT(*)

/*! \brief return str message of the last error */
const char *MXGetLastError() {
  return MXAPIThreadLocalStore::Get()->last_error.c_str();
}

/*!
 * \brief handle exception throwed out
 * \param e the exception
 * \return the return value of API after exception is handled
 */
int MXHandleException(const dmlc::Error &e) {
  MXAPIThreadLocalStore::Get()->last_error = e.what();
  return -1;
}

// Internal function to get the information
// from function registry
// Used to implement MXSymbolGetAtomicSymbolInfo and MXFuncGetInfo
template<typename FunRegType>
inline int MXAPIGetFunctionRegInfo(const FunRegType *e,
                                   const char **name,
                                   const char **description,
                                   mx_uint *num_args,
                                   const char ***arg_names,
                                   const char ***arg_type_infos,
                                   const char ***arg_descriptions) {
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();

  API_BEGIN();
  *name = e->name.c_str();
  *description = e->description.c_str();
  *num_args = static_cast<mx_uint>(e->arguments.size());
  ret->ret_vec_charp.clear();
  for (size_t i = 0; i < e->arguments.size(); ++i) {
    ret->ret_vec_charp.push_back(e->arguments[i].name.c_str());
  }
  for (size_t i = 0; i < e->arguments.size(); ++i) {
    ret->ret_vec_charp.push_back(e->arguments[i].type_info_str.c_str());
  }
  for (size_t i = 0; i < e->arguments.size(); ++i) {
    ret->ret_vec_charp.push_back(e->arguments[i].description.c_str());
  }
  *arg_names = dmlc::BeginPtr(ret->ret_vec_charp);
  *arg_type_infos = dmlc::BeginPtr(ret->ret_vec_charp) + e->arguments.size();
  *arg_descriptions = dmlc::BeginPtr(ret->ret_vec_charp) + (e->arguments.size() * 2);
  API_END();
}

// NOTE: return value is added in API_END
int MXNArrayCreateNone(NArrayHandle *out) {
  API_BEGIN();
  *out = new NArray();
  API_END();
}

int MXNArrayCreateShareMem(mx_float *data,
                           mx_uint *shape,
                           mx_uint ndim,
                           NArrayHandle *out) {
  API_BEGIN();
  *out = new NArray(TBlob(data, TShape(shape, shape + ndim),
                          cpu::kDevMask), 0);
  API_END();
}

int MXNArrayCreate(const mx_uint *shape,
                   mx_uint ndim,
                   int dev_mask,
                   int dev_id,
                   int delay_alloc,
                   NArrayHandle *out) {
  API_BEGIN();
  *out = new NArray(TShape(shape, shape + ndim),
                    Context(dev_mask, dev_id),
                    delay_alloc != 0);
  API_END();
}

int MXNArrayLoadFromRawBytes(const void *buf,
                             mx_ulong size,
                             NArrayHandle *out) {
  NArray *ptr = nullptr;
  API_BEGIN();
  dmlc::MemoryFixedSizeStream strm((void*)buf, size); // NOLINT(*)
  ptr = new NArray();
  if (!ptr->Load(&strm)) {
    throw dmlc::Error("Invalid NArray serialization format");
  }
  *out = ptr;
  API_END_HANDLE_ERROR(delete ptr);
}

int MXNArraySaveRawBytes(NArrayHandle handle,
                         mx_ulong *out_size,
                         const char **out_buf) {
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  ret->ret_str.resize(0);
  dmlc::MemoryStringStream strm(&ret->ret_str);
  static_cast<NArray*>(handle)->Save(&strm);
  *out_size = ret->ret_str.length();
  *out_buf = ret->ret_str.c_str();
  API_END();
}

int MXNArraySyncCopyFromCPU(NArrayHandle handle,
                            const mx_float *data,
                            size_t size) {
  API_BEGIN();
  static_cast<NArray*>(handle)->SyncCopyFromCPU(data, size);
  API_END();
}

int MXNArraySyncCopyToCPU(NArrayHandle handle,
                          mx_float *data,
                          size_t size) {
  API_BEGIN();
  static_cast<NArray*>(handle)->SyncCopyToCPU(data, size);
  API_END();
}

int MXNArrayWaitToRead(NArrayHandle handle) {
  API_BEGIN();
  static_cast<NArray*>(handle)->WaitToRead();
  API_END();
}

int MXNArrayWaitToWrite(NArrayHandle handle) {
  API_BEGIN();
  static_cast<NArray*>(handle)->WaitToWrite();
  API_END();
}

const int kMXAPINArrayListMagic = 0x112;

int MXNArrayListSave(const char* fname,
                     mx_uint num_args,
                     NArrayHandle* args,
                     const char** keys) {
  API_BEGIN();
  std::vector<NArray> data(num_args);
  std::vector<std::string> names;
  for (mx_uint i = 0; i < num_args; ++i) {
    data[i] = *static_cast<NArray*>(args[i]);
  }
  if (keys != nullptr) {
    names.resize(num_args);
    for (mx_uint i = 0; i < num_args; ++i) {
      names[i] = keys[i];
    }
  }
  std::unique_ptr<dmlc::Stream> fo(dmlc::Stream::Create(fname, "w"));
  uint64_t header = kMXAPINArrayListMagic, reserved = 0;
  fo->Write(&header, sizeof(header));
  fo->Write(&reserved, sizeof(reserved));
  fo->Write(data);
  fo->Write(names);
  API_END();
}

int MXNArrayListLoad(const char* fname,
                     mx_uint *out_size,
                     NArrayHandle** out_arr,
                     mx_uint *out_name_size,
                     const char*** out_names) {
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  ret->ret_vec_str.clear();
  API_BEGIN();
  std::vector<NArray> data;
  std::vector<std::string> &names = ret->ret_vec_str;
  std::unique_ptr<dmlc::Stream> fi(dmlc::Stream::Create(fname, "r"));
  uint64_t header, reserved;
  CHECK(fi->Read(&header))
      << "Invalid NArray file format";
  CHECK(fi->Read(&reserved))
      << "Invalid NArray file format";
  CHECK(header == kMXAPINArrayListMagic)
      << "Invalid NArray file format";
  CHECK(fi->Read(&data))
      << "Invalid NArray file format";
  CHECK(fi->Read(&names))
      << "Invalid NArray file format";
  CHECK(names.size() == 0 || names.size() == data.size())
      << "Invalid NArray file format";
  ret->ret_handles.resize(data.size());
  for (size_t i = 0; i < data.size(); ++i) {
    NArray *ptr = new NArray();
    *ptr = data[i];
    ret->ret_handles[i] = ptr;
  }
  ret->ret_vec_charp.resize(names.size());
  for (size_t i = 0; i < names.size(); ++i) {
    ret->ret_vec_charp[i] = names[i].c_str();
  }
  *out_size = static_cast<mx_uint>(data.size());
  *out_arr = dmlc::BeginPtr(ret->ret_handles);
  *out_name_size = static_cast<mx_uint>(names.size());
  *out_names = dmlc::BeginPtr(ret->ret_vec_charp);
  API_END();
}

int MXNArrayWaitAll() {
  API_BEGIN();
  DAGEngine::Get()->WaitForAll();
  API_END();
}

int MXNArrayFree(NArrayHandle handle) {
  API_BEGIN();
  delete static_cast<NArray*>(handle);
  API_END();
}

int MXNArrayGetShape(NArrayHandle handle,
                     mx_uint *out_dim,
                     const mx_uint **out_pdata) {
  API_BEGIN();
  NArray *arr = static_cast<NArray*>(handle);
  if (!arr->is_none()) {
    const TShape &s = arr->shape();
    *out_dim = s.ndim();
    *out_pdata = s.data();
  } else {
    *out_dim = 0;
  }
  API_END();
}

int MXNArrayGetData(NArrayHandle handle,
                    mx_float **out_pdata) {
  API_BEGIN();
  NArray *arr = static_cast<NArray*>(handle);
  if (!arr->is_none()) {
    CHECK(arr->ctx().dev_mask == cpu::kDevMask)
        << "MXNArrayGetData can only be called for NArray on CPU";
    const TBlob &b = arr->data();
    CHECK(b.CheckContiguous());
    *out_pdata = b.FlatTo2D<cpu, mx_float>().dptr_;
  } else {
    *out_pdata = nullptr;
  }
  API_END();
}

int MXNArrayGetContext(NArrayHandle handle,
                       int *out_dev_mask,
                       int *out_dev_id) {
  API_BEGIN();
  NArray *arr = static_cast<NArray*>(handle);
  if (!arr->is_none()) {
    const Context &ctx = arr->ctx();
    *out_dev_mask = ctx.dev_mask;
    *out_dev_id = ctx.dev_id;
  } else {
    *out_dev_mask = 0;
    *out_dev_id = 0;
  }
  API_END();
}

int MXListFunctions(mx_uint *out_size,
                    FunctionHandle **out_array) {
  API_BEGIN();
  auto &vec = dmlc::Registry<NArrayFunctionReg>::List();
  *out_size = static_cast<mx_uint>(vec.size());
  *out_array = (FunctionHandle*)(dmlc::BeginPtr(vec));  //  NOLINT(*)
  API_END();
}

int MXGetFunction(const char *name,
                  FunctionHandle *out) {
  API_BEGIN();
  *out = dmlc::Registry<NArrayFunctionReg>::Find(name);
  API_END();
}

int MXFuncGetInfo(FunctionHandle fun,
                  const char **name,
                  const char **description,
                  mx_uint *num_args,
                  const char ***arg_names,
                  const char ***arg_type_infos,
                  const char ***arg_descriptions) {
  return MXAPIGetFunctionRegInfo(static_cast<const NArrayFunctionReg *>(fun),
                                 name, description, num_args,
                                 arg_names, arg_type_infos, arg_descriptions);
}

int MXFuncDescribe(FunctionHandle fun,
                   mx_uint *num_use_vars,
                   mx_uint *num_scalars,
                   mx_uint *num_mutate_vars,
                   int *type_mask) {
  API_BEGIN();
  auto *f = static_cast<const NArrayFunctionReg*>(fun);
  *num_use_vars = f->num_use_vars;
  *num_scalars = f->num_scalars;
  *num_mutate_vars = f->num_mutate_vars;
  *type_mask = f->type_mask;
  API_END();
}

int MXFuncInvoke(FunctionHandle fun,
                 NArrayHandle *use_vars,
                 mx_float *scalar_args,
                 NArrayHandle *mutate_vars) {
  API_BEGIN();
  auto *f = static_cast<const NArrayFunctionReg*>(fun);
  f->body((NArray**)(use_vars),  //  NOLINT(*)
          scalar_args,
          (NArray**)(mutate_vars));  //  NOLINT(*)
  API_END();
}

//--------------------------------------------
// Part 3: symbolic configuration generation
//--------------------------------------------

int MXSymbolListAtomicSymbolCreators(mx_uint *out_size,
                                     AtomicSymbolCreator **out_array) {
  API_BEGIN();
  auto &vec = dmlc::Registry<OperatorPropertyReg>::List();
  *out_size = static_cast<mx_uint>(vec.size());
  *out_array = (AtomicSymbolCreator*)(dmlc::BeginPtr(vec));  //  NOLINT(*)
  API_END();
}

int MXSymbolGetAtomicSymbolName(AtomicSymbolCreator creator,
                                const char **out) {
  API_BEGIN();
  OperatorPropertyReg *e = static_cast<OperatorPropertyReg *>(creator);
  *out = e->name.c_str();
  API_END();
}

int MXSymbolGetAtomicSymbolInfo(AtomicSymbolCreator creator,
                                const char **name,
                                const char **description,
                                mx_uint *num_args,
                                const char ***arg_names,
                                const char ***arg_type_infos,
                                const char ***arg_descriptions,
                                const char **key_var_num_args) {
  OperatorPropertyReg *e = static_cast<OperatorPropertyReg *>(creator);
  *key_var_num_args = e->key_var_num_args.c_str();
  return MXAPIGetFunctionRegInfo(e, name, description, num_args,
                                 arg_names, arg_type_infos, arg_descriptions);
}

int MXSymbolCreateAtomicSymbol(AtomicSymbolCreator creator,
                               int num_param,
                               const char **keys,
                               const char **vals,
                               SymbolHandle *out) {
  Symbol *s = new Symbol();
  OperatorProperty *op = nullptr;

  API_BEGIN();
  OperatorPropertyReg *e = static_cast<OperatorPropertyReg *>(creator);
  op = e->body();
  std::vector<std::pair<std::string, std::string> > kwargs;
  for (int i = 0; i < num_param; ++i) {
    kwargs.push_back({std::string(keys[i]), std::string(vals[i])});
  }
  op->Init(kwargs);
  *s = Symbol::Create(op);
  *out = s;
  API_END_HANDLE_ERROR(delete s; delete op);
}

int MXSymbolCreateVariable(const char *name, SymbolHandle *out) {
  Symbol *s = new Symbol();
  API_BEGIN();
  *s = Symbol::CreateVariable(name);
  *out = s;
  API_END_HANDLE_ERROR(delete s);
}

int MXSymbolCreateGroup(mx_uint num_symbols,
                        SymbolHandle *symbols,
                        SymbolHandle *out) {
  Symbol *s = new Symbol();
  Symbol **sym_arr = (Symbol**)symbols; // NOLINT(*)
  API_BEGIN();
  std::vector<Symbol> syms;
  for (mx_uint i = 0; i < num_symbols; ++i) {
    syms.push_back(*sym_arr[i]);
  }
  *s = Symbol::CreateGroup(syms);
  *out = s;
  API_END_HANDLE_ERROR(delete s);
}

int MXSymbolFree(SymbolHandle symbol) {
  API_BEGIN();
  delete static_cast<Symbol*>(symbol);
  API_END();
}

int MXSymbolCopy(SymbolHandle symbol, SymbolHandle *out) {
  Symbol *s = new Symbol();
  API_BEGIN();
  *s = static_cast<const Symbol*>(symbol)->Copy();
  *out = s;
  API_END_HANDLE_ERROR(delete s);
}

int MXSymbolPrint(SymbolHandle symbol, const char **out_str) {
  Symbol *s = static_cast<Symbol*>(symbol);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  std::ostringstream os;
  s->Print(os);
  ret->ret_str = os.str();
  *out_str = (ret->ret_str).c_str();
  API_END();
}

int MXSymbolListArguments(SymbolHandle symbol,
                          mx_uint *out_size,
                          const char ***out_str_array) {
  Symbol *s = static_cast<Symbol*>(symbol);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  ret->ret_vec_str = std::move(s->ListArguments());
  ret->ret_vec_charp.clear();
  for (size_t i = 0; i < ret->ret_vec_str.size(); ++i) {
    ret->ret_vec_charp.push_back(ret->ret_vec_str[i].c_str());
  }
  *out_size = static_cast<mx_uint>(ret->ret_vec_charp.size());
  *out_str_array = dmlc::BeginPtr(ret->ret_vec_charp);
  API_END();
}

int MXSymbolListReturns(SymbolHandle symbol,
                        mx_uint *out_size,
                        const char ***out_str_array) {
  Symbol *s = static_cast<Symbol*>(symbol);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  ret->ret_vec_str = std::move(s->ListReturns());
  ret->ret_vec_charp.clear();
  for (size_t i = 0; i < ret->ret_vec_str.size(); ++i) {
    ret->ret_vec_charp.push_back(ret->ret_vec_str[i].c_str());
  }
  *out_size = static_cast<mx_uint>(ret->ret_vec_charp.size());
  *out_str_array = dmlc::BeginPtr(ret->ret_vec_charp);
  API_END();
}

int MXSymbolListAuxiliaryStates(SymbolHandle symbol,
                                mx_uint *out_size,
                                const char ***out_str_array) {
  Symbol *s = static_cast<Symbol*>(symbol);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  ret->ret_vec_str = std::move(s->ListAuxiliaryStates());
  ret->ret_vec_charp.clear();
  for (size_t i = 0; i < ret->ret_vec_str.size(); ++i) {
    ret->ret_vec_charp.push_back(ret->ret_vec_str[i].c_str());
  }
  *out_size = static_cast<mx_uint>(ret->ret_vec_charp.size());
  *out_str_array = dmlc::BeginPtr(ret->ret_vec_charp);
  API_END();
}

int MXSymbolCompose(SymbolHandle sym,
                    const char *name,
                    mx_uint num_args,
                    const char** keys,
                    SymbolHandle* args) {
  API_BEGIN();
  std::string s_name;
  if (name != nullptr) s_name = name;

  Symbol* s = static_cast<Symbol*>(sym);
  if (keys == nullptr && num_args != 0) {
    std::vector<Symbol> pos_args;
    for (mx_uint i = 0; i < num_args; ++i) {
      pos_args.push_back(*((Symbol*)args[i]));  //  NOLINT(*)
    }
    s->Compose(pos_args, s_name);
  } else {
    std::unordered_map<std::string, Symbol> kwargs;
    for (mx_uint i = 0; i < num_args; ++i) {
      kwargs[keys[i]] = *((Symbol*)args[i]);  //  NOLINT(*)
    }
    s->Compose(kwargs, s_name);
  }
  API_END();
}

int MXSymbolGrad(SymbolHandle sym, mx_uint num_wrt, const char** wrt, SymbolHandle* out) {
  API_BEGIN();
  Symbol* s = static_cast<Symbol*>(sym);
  std::vector<std::string> wrts(num_wrt);
  for (mx_uint i = 0; i < num_wrt; ++i) {
    wrts[i] = wrt[i];
  }
  Symbol* ret = new Symbol;
  *ret = s->Grad(wrts);
  *out = ret;
  API_END();
}

int MXSymbolInferShape(SymbolHandle sym,
                       mx_uint num_args,
                       const char** keys,
                       const mx_uint *arg_ind_ptr,
                       const mx_uint *arg_shape_data,
                       mx_uint *in_shape_size,
                       const mx_uint **in_shape_ndim,
                       const mx_uint ***in_shape_data,
                       mx_uint *out_shape_size,
                       const mx_uint **out_shape_ndim,
                       const mx_uint ***out_shape_data,
                       mx_uint *aux_shape_size,
                       const mx_uint **aux_shape_ndim,
                       const mx_uint ***aux_shape_data,
                       int *complete) {
  Symbol *s = static_cast<Symbol*>(sym);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  bool succ;
  API_BEGIN();
  if (keys == nullptr && num_args != 0) {
    ret->arg_shapes.clear();
    for (mx_uint i = 0; i < num_args; ++i) {
      ret->arg_shapes.push_back(TShape(arg_shape_data + arg_ind_ptr[i],
                                       arg_shape_data + arg_ind_ptr[i+1]));
    }
    succ = s->InferShape(&(ret->arg_shapes), &(ret->out_shapes), &(ret->aux_shapes));
  } else {
    std::unordered_map<std::string, TShape> kwargs;
    for (mx_uint i = 0; i < num_args; ++i) {
      kwargs[keys[i]] = TShape(arg_shape_data + arg_ind_ptr[i],
                               arg_shape_data + arg_ind_ptr[i+1]);
    }
    succ = s->InferShape(kwargs, &(ret->arg_shapes), &(ret->out_shapes), &(ret->aux_shapes));
  }
  if (succ) {
    MXAPIThreadLocalEntry::SetupShapeArrayReturn(
        ret->arg_shapes, &(ret->arg_shape_ndim), &(ret->arg_shape_data));
    MXAPIThreadLocalEntry::SetupShapeArrayReturn(
        ret->out_shapes, &(ret->out_shape_ndim), &(ret->out_shape_data));
    MXAPIThreadLocalEntry::SetupShapeArrayReturn(
        ret->aux_shapes, &(ret->aux_shape_ndim), &(ret->aux_shape_data));
    *in_shape_size = static_cast<mx_uint>(ret->arg_shapes.size());
    *in_shape_ndim = dmlc::BeginPtr(ret->arg_shape_ndim);
    *in_shape_data = dmlc::BeginPtr(ret->arg_shape_data);
    *out_shape_size = static_cast<mx_uint>(ret->out_shapes.size());
    *out_shape_ndim = dmlc::BeginPtr(ret->out_shape_ndim);
    *out_shape_data = dmlc::BeginPtr(ret->out_shape_data);
    *aux_shape_size = static_cast<mx_uint>(ret->aux_shapes.size());
    *aux_shape_ndim = dmlc::BeginPtr(ret->aux_shape_ndim);
    *aux_shape_data = dmlc::BeginPtr(ret->aux_shape_data);
    *complete = 1;
  } else {
    *complete = 0;
  }
  API_END();
}

int MXExecutorForward(ExecutorHandle handle, bool is_train) {
  API_BEGIN();
  Executor *exec = static_cast<Executor*>(handle);
  exec->Forward(is_train);
  API_END();
}

int MXExecutorBackward(ExecutorHandle handle,
                       mx_uint len,
                       NArrayHandle *head_grads) {
  API_BEGIN();
  Executor *exec = static_cast<Executor*>(handle);
  std::vector<NArray> narrays;
  NArray **args_ptr = reinterpret_cast<NArray**>(head_grads);
  for (mx_uint i = 0; i < len; ++i) {
    narrays.push_back(*args_ptr[i]);
  }
  exec->Backward(narrays);
  API_END();
}

int MXExecutorHeads(ExecutorHandle handle,
                    mx_uint *out_size,
                    NArrayHandle **out) {
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  Executor *exec = static_cast<Executor*>(handle);
  std::vector<NArray> heads = exec->heads();
  ret->ret_handles.resize(heads.size());
  for (size_t i = 0; i < heads.size(); ++i) {
    NArray *ptr = new NArray();
    *ptr = heads[i];
    ret->ret_handles[i] = ptr;
  }
  *out_size = heads.size();
  *out = dmlc::BeginPtr(ret->ret_handles);
  API_END();
}

int MXExecutorBind(SymbolHandle symbol_handle,
                   int dev_mask,
                   int dev_id,
                   mx_uint len,
                   NArrayHandle *in_args,
                   NArrayHandle *arg_grad_store,
                   mx_uint *grad_req_type,
                   mx_uint aux_states_len,
                   NArrayHandle *aux_states,
                   ExecutorHandle *out) {
  API_BEGIN();
  Symbol *symb = static_cast<Symbol*>(symbol_handle);
  Context ctx = Context(dev_mask, dev_id);
  NArray **in_args_ptr = reinterpret_cast<NArray**>(in_args);
  NArray **arg_grad_ptr = reinterpret_cast<NArray**>(arg_grad_store);
  NArray **aux_states_ptr = reinterpret_cast<NArray**>(aux_states);
  std::vector<NArray> in_args_vec;
  std::vector<NArray> arg_grad_vec;
  std::vector<OpReqType> grad_req_vec;
  std::vector<NArray> aux_states_vec;
  for (mx_uint i = 0; i < len; ++i) {
    in_args_vec.push_back(*(in_args_ptr[i]));
    if (arg_grad_ptr[i] == nullptr) {
      arg_grad_vec.push_back(NArray());
      grad_req_vec.push_back(kNullOp);
      LOG(INFO) << "nop";
    } else {
      LOG(INFO) << "grad=" << grad_req_type[i];
      arg_grad_vec.push_back(*(arg_grad_ptr[i]));
      grad_req_vec.push_back(static_cast<OpReqType>(grad_req_type[i]));
    }
  }
  for (mx_uint i = 0; i < aux_states_len; ++i) {
    aux_states_vec.push_back(*(aux_states_ptr[i]));
  }
  *out = Executor::Bind(*symb, ctx, in_args_vec, arg_grad_vec, grad_req_vec, aux_states_vec);
  API_END();
}

//--------------------------------------------
// Part 5: IO Interface
//--------------------------------------------
int MXListDataIters(mx_uint *out_size,
                    DataIterCreator **out_array) {
  API_BEGIN();
  auto &vec = dmlc::Registry<DataIteratorReg>::List();
  *out_size = static_cast<mx_uint>(vec.size());
  *out_array = (DataIterCreator*)(dmlc::BeginPtr(vec));  //  NOLINT(*)
  API_END();
}

int MXDataIterGetIterInfo(DataIterCreator creator,
                                const char **name,
                                const char **description,
                                mx_uint *num_args,
                                const char ***arg_names,
                                const char ***arg_type_infos,
                                const char ***arg_descriptions) {
  DataIteratorReg *e = static_cast<DataIteratorReg *>(creator);
  return MXAPIGetFunctionRegInfo(e, name, description, num_args,
                                 arg_names, arg_type_infos, arg_descriptions);
}

int MXDataIterCreateIter(DataIterCreator creator,
                               int num_param,
                               const char **keys,
                               const char **vals,
                               DataIterHandle *out) {
  IIterator<DataBatch> *iter = nullptr;
  API_BEGIN();
  DataIteratorReg *e = static_cast<DataIteratorReg *>(creator);
  iter = e->body();
  std::vector<std::pair<std::string, std::string> > kwargs;
  for (int i = 0; i < num_param; ++i) {
    kwargs.push_back({std::string(keys[i]), std::string(vals[i])});
  }
  iter->Init(kwargs);
  iter->BeforeFirst();
  *out = iter;
  API_END_HANDLE_ERROR(delete iter);
}

int MXDataIterFree(DataIterHandle handle) {
  API_BEGIN();
  delete static_cast<IIterator<DataBatch> *>(handle);
  API_END();
}

int MXDataIterBeforeFirst(DataIterHandle handle) {
  API_BEGIN();
  static_cast<IIterator<DataBatch>* >(handle)->BeforeFirst();
  API_END();
}

int MXDataIterNext(DataIterHandle handle, int *out) {
  API_BEGIN();
  *out = static_cast<IIterator<DataBatch>* >(handle)->Next();
  API_END();
}

int MXDataIterGetLabel(DataIterHandle handle, NArrayHandle *out) {
  API_BEGIN();
  DataBatch db = static_cast<IIterator<DataBatch>* >(handle)->Value();
  *out = new NArray(db.data[1], 0);
  API_END();
}

int MXDataIterGetData(DataIterHandle handle, NArrayHandle *out) {
  API_BEGIN();
  DataBatch db = static_cast<IIterator<DataBatch>* >(handle)->Value();
  *out = new NArray(db.data[0], 0);
  API_END();
}
