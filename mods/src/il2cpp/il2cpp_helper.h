#pragma once

#include <array>
#include <memory>
#include <type_traits>

#include <EASTL/span.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include "il2cpp-functions.h"
#include <il2cpp-config.h>
#include <il2cpp-api-types.h>
#include <il2cpp-class-internals.h>
#include <il2cpp-object-internals.h>

#include <utils/Il2CppHashMap.h>

#include <spdlog/spdlog.h>

#if !_WIN32
#include <syslog.h>
#include <unistd.h>
#endif

class IL2CppPropertyHelper
{
public:
  IL2CppPropertyHelper(Il2CppClass* cls, const PropertyInfo* propInfo)
  {
    this->cls      = cls;
    this->propInfo = propInfo;
  }

  bool isValidHelper() const
  {
#if DEBUG
    return true;
#else
    return this->cls != nullptr && propInfo != nullptr;
#endif
  }

  template <typename T> void SetRaw(void* _this, T& v)
  {
    if (!this->propInfo) {
      return;
    }

    const auto *set_method         = il2cpp_property_get_set_method(const_cast<PropertyInfo*>(this->propInfo));
    const auto *set_method_virtual = il2cpp_object_get_virtual_method(static_cast<Il2CppObject*>(_this), set_method);

    Il2CppException* exception = nullptr;
    void*            params[1] = {&v};

    il2cpp_runtime_invoke(set_method_virtual, _this, params, &exception);
  }

  template <typename T = void> T* GetRaw(void* _this)
  {
    if (!this->propInfo) {
      return nullptr;
    }

    const auto get_method         = il2cpp_property_get_get_method(const_cast<PropertyInfo*>(this->propInfo));
    const auto get_method_virtual = il2cpp_object_get_virtual_method(static_cast<Il2CppObject*>(_this), get_method);

    Il2CppException* exception = nullptr;
    auto             result    = il2cpp_runtime_invoke(get_method_virtual, _this, nullptr, &exception);

    if (exception) {
      return nullptr;
    }

    return reinterpret_cast<T*>(result);
  }

  template <typename T> T* Get(void* _this)
  {
    const auto r = GetRaw<Il2CppObject>(_this);
    return !r ? nullptr : static_cast<T*>(il2cpp_object_unbox(r));
  }

  template <typename T> T* GetUnboxedSelf(void* _this)
  {
    const auto r = GetRaw<Il2CppObject>(il2cpp_object_unbox(static_cast<Il2CppObject*>(_this)));
    return !r ? nullptr : static_cast<T*>(il2cpp_object_unbox(r));
  }

private:
  Il2CppClass*        cls;
  const PropertyInfo* propInfo;
};

class IL2CppFieldHelper
{
public:
  IL2CppFieldHelper(Il2CppClass* cls, FieldInfo* fieldInfo)
  {
    this->cls       = cls;
    this->fieldInfo = fieldInfo;
  }

  bool isValidHelper() const
  {
#if DEBUG
    return true;
#else
    return this->cls != nullptr && fieldInfo != nullptr;
#endif
  }

  ptrdiff_t offset() const
  {
    return this->fieldInfo->offset;
  }

private:
  Il2CppClass* cls;
  FieldInfo*   fieldInfo;
};

class IL2CppStaticFieldHelper
{
public:
  IL2CppStaticFieldHelper(Il2CppClass* cls, FieldInfo* fieldInfo)
  {
    this->cls       = cls;
    this->fieldInfo = fieldInfo;
  }

  template <typename T> T Get() const
  {
    T v;
    il2cpp_field_static_get_value(this->fieldInfo, &v);
    return v;
  }

private:
  Il2CppClass* cls;
  FieldInfo*   fieldInfo;
};

class IL2CppClassHelper
{
public:
  IL2CppClassHelper(Il2CppClass* cls)
  {
    this->cls = cls;
  }

  template <typename T> T* New()
  {
    return reinterpret_cast<T*>(il2cpp_object_new(this->cls));
  }

  void* GetType() const
  {
    const auto obj = il2cpp_type_get_object(&this->cls->byval_arg);
    return obj;
  }

  bool isValidHelper() const
  {
#if DEBUG
    return true;
#else
    return this->cls != nullptr;
#endif
  }

  template <typename T = void> T* GetMethod(const char* name, const int arg_count = -1)
  {
    if (!this->cls) {
      return nullptr;
    }

    if (const auto fn = il2cpp_class_get_method_from_name(this->cls, name, arg_count); fn != nullptr) {
      return reinterpret_cast<T*>(fn->methodPointer);
    }

    return nullptr;
  }

  template <typename T = void> T* GetVirtualMethod(const char* name, const int arg_count = -1)
  {
    if (!this->cls) {
      return nullptr;
    }

    const auto fn = il2cpp_class_get_method_from_name(this->cls, name, arg_count);
    const auto get_method_virtual = il2cpp_object_get_virtual_method(reinterpret_cast<Il2CppObject*>(this), fn);

    return const_cast<T*>(get_method_virtual->methodPointer);
  }

  template <typename R, typename... Args> class InvokerMethod
  {
  public:
    using InvokeResult = std::conditional_t<std::is_void_v<R>, bool, R>;

    InvokerMethod(const MethodInfo* fn = nullptr)
        : fn(fn)
    {
    }

    explicit operator bool() const
    {
      return fn != nullptr;
    }

    InvokeResult Invoke(void* _this, Args... args) const
    {
      if (!fn) return {};

      Il2CppException* exception = nullptr;
      std::array<void*, sizeof...(Args)> params{ToParameter(args)...};
      auto* result = il2cpp_runtime_invoke(fn, _this, params.empty() ? nullptr : params.data(), &exception);

      if (exception) {
        char message[512] = {};
        il2cpp_format_exception(exception, message, sizeof(message));
        spdlog::error("Exception occurred while invoking method {}::{}: {}", fn->klass->name, fn->name, message);
        return {};
      }

      if constexpr (std::is_void_v<R>) {
        return true;
      } else if constexpr (std::is_pointer_v<R>) {
        return reinterpret_cast<R>(result);
      } else {
        if (!result) return {};
        auto* value = static_cast<R*>(il2cpp_object_unbox(result));
        return value ? *value : R{};
      }
    }

    const MethodInfo* fn;

  private:
    template <typename T> static void* ToParameter(T& value)
    {
      if constexpr (std::is_pointer_v<std::remove_cvref_t<T>>) {
        return const_cast<void*>(reinterpret_cast<const void*>(value));
      } else {
        return std::addressof(value);
      }
    }
  };

  template <typename R, typename... Args>
  InvokerMethod<R, Args...> GetInvokeMethod(const char* name, const int arg_count = sizeof...(Args))
  {
    if (!this->cls) {
      return {};
    }

    auto fn = il2cpp_class_get_method_from_name(this->cls, name, arg_count);
    return InvokerMethod<R, Args...>(fn);
  }

  const MethodInfo*
  GetMethodInfoSpecial(const char*                                                    name,
                       const std::function<bool(int param_count, const Il2CppType** param)>& arg_filter = nullptr) const
  {
    if (!this->cls) {
      return nullptr;
    }

    auto  flags = 0;
    void* iter  = nullptr;
    while (const MethodInfo* method = il2cpp_class_get_methods(this->cls, &iter)) {
      if (method->name[0] == name[0] && !strcmp(name, method->name) && ((method->flags & flags) == flags)) {
        if (!arg_filter || arg_filter(method->parameters_count, method->parameters)) {
          return method;
        }
      }
    }
    return nullptr;
  }

  template <typename R, typename... Args>
  InvokerMethod<R, Args...>
  GetInvokeMethodSpecial(const char*                                                    name,
                         const std::function<bool(int param_count, const Il2CppType** param)>& arg_filter = nullptr)
  {
    return InvokerMethod<R, Args...>(GetMethodInfoSpecial(name, arg_filter));
  }

  template <typename T = void>
  T* GetMethodSpecial(const char*                                                    name,
                      const std::function<bool(int param_count, const Il2CppType** param)>& arg_filter = nullptr)
  {
    if (!this->cls) {
      return nullptr;
    }
    if (const auto info = GetMethodInfoSpecial(name, arg_filter)) {
      return reinterpret_cast<T*>(info->methodPointer);
    }
    return nullptr;
  }

  template <typename T = void> T* GetMethodFromObject(Il2CppObject* obj, const char* name)
  {
    if (!this->cls) {
      return nullptr;
    }

    for (auto i = 0; i < obj->klass->method_count; ++i) {
      const auto method = obj->klass->klass->methods[i];
      if (method->name[0] == name[0] && !strcmp(name, method->name)) {
        return reinterpret_cast<T*>(method->methodPointer);
      }
    }
    return nullptr;
  }

  const MethodInfo* GetMethodInfo(const char* name, const int arg_count = -1) const
  {
    if (!this->cls) {
      return nullptr;
    }

    return il2cpp_class_get_method_from_name(this->cls, name, arg_count);
  }

  IL2CppPropertyHelper GetProperty(const char* name) const
  {
    return IL2CppPropertyHelper{this->cls, il2cpp_class_get_property_from_name(this->cls, name)};
  }

  IL2CppFieldHelper GetField(const char* name) const
  {
    return IL2CppFieldHelper{this->cls, il2cpp_class_get_field_from_name(this->cls, name)};
  }

  IL2CppStaticFieldHelper GetStaticField(const char* name) const
  {
    return IL2CppStaticFieldHelper{this->cls, il2cpp_class_get_field_from_name(this->cls, name)};
  }

  IL2CppClassHelper GetParent(const char* name) const
  {
    if (Il2CppClass* pCls = this->cls) {
      do {
        if (pCls->name[0] == name[0] && !strcmp(name, pCls->name)) {
          return IL2CppClassHelper{pCls};
        }

        pCls = il2cpp_class_get_parent(pCls);
      } while (pCls);
    }

    return IL2CppClassHelper{nullptr};
  }

  IL2CppClassHelper GetNestedType(const char* name) const
  {
    for (int i = 0; i < this->cls->nested_type_count; ++i) {
      const auto type = this->cls->nestedTypes[i];
      if (strcmp(type->name, name) == 0) {
        return IL2CppClassHelper(type);
      }
    }

    return IL2CppClassHelper(nullptr);
  }

  Il2CppClass* get_cls() const
  {
    return this->cls;
  }

private:
  Il2CppClass* cls;
};

#define il2cpp_get_class_helper(assembly, namespacez, name) il2cpp_get_class_helper_impl(assembly, namespacez, name)

inline IL2CppClassHelper il2cpp_get_class_helper_impl(const char* assembly, const char* namespacez, const char* name)
{
  const auto domain    = il2cpp_domain_get();
  const auto assemblyT = il2cpp_domain_assembly_open(domain, assembly);
  const auto image     = il2cpp_assembly_get_image(assemblyT);

  const auto cls = il2cpp_class_from_name(image, namespacez, name);

  return IL2CppClassHelper{cls};
}

template <typename T> T* il2cpp_get_array_element(Il2CppArray* array, const size_t index)
{
  const auto n = static_cast<Il2CppArraySize*>(array);
  return static_cast<T*>(n->vector[index]);
}

extern eastl::unordered_map<Il2CppClass*, eastl::vector<uintptr_t>> tracked_objects;

template <typename T> class ObjectFinder
{
public:
  static T* Get()
  {
    auto& objects = tracked_objects[T::get_class_helper().get_cls()];
    if (objects.empty()) {
      // TODO: assert?
      return nullptr;
    }
    return reinterpret_cast<T*>(objects.back());
  }

  static eastl::span<T*> GetAll()
  {
    auto& objects = tracked_objects[T::get_class_helper().get_cls()];
    return {reinterpret_cast<T**>(objects.data()), reinterpret_cast<T**>(objects.data()) + objects.size()};
  }
};

template <typename T> T* il2cpp_resolve_icall_typed(const char* name)
{
  return reinterpret_cast<T*>(il2cpp_resolve_icall(name));
}
