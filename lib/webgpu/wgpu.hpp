#include <webgpu/webgpu.h>

namespace wgpu {
template <typename Derived, typename CType>
struct ObjectBase {
  ObjectBase() = default;
  ObjectBase(CType handle) : mHandle(handle) {}
  ~ObjectBase() { Reset(); }

  ObjectBase(ObjectBase const& other) : ObjectBase(other.Get()) {}
  Derived& operator=(ObjectBase const& other) {
    if (&other != this) {
      if (mHandle) {
        Derived::WGPURelease(mHandle);
      }
      mHandle = other.mHandle;
      if (mHandle) {
        Derived::WGPUReference(mHandle);
      }
    }
    return static_cast<Derived&>(*this);
  }

  ObjectBase(ObjectBase&& other) noexcept {
    mHandle = other.mHandle;
    other.mHandle = 0;
  }
  Derived& operator=(ObjectBase&& other) noexcept {
    if (&other != this) {
      if (mHandle) {
        Derived::WGPURelease(mHandle);
      }
      mHandle = other.mHandle;
      other.mHandle = nullptr;
    }
    return static_cast<Derived&>(*this);
  }

  ObjectBase(std::nullptr_t) {}
  Derived& operator=(std::nullptr_t) {
    if (mHandle != nullptr) {
      Derived::WGPURelease(mHandle);
      mHandle = nullptr;
    }
    return static_cast<Derived&>(*this);
  }

  bool operator==(std::nullptr_t) const { return mHandle == nullptr; }
  bool operator!=(std::nullptr_t) const { return mHandle != nullptr; }

  explicit operator bool() const { return mHandle != nullptr; }
  operator CType() { return mHandle; }
  [[nodiscard]] CType Get() const { return mHandle; }
  CType Release() {
    CType result = mHandle;
    mHandle = 0;
    return result;
  }
  void Reset() {
    if (mHandle) {
      Derived::WGPURelease(mHandle);
      mHandle = nullptr;
    }
  }

protected:
  CType mHandle = nullptr;
};

class Texture : public ObjectBase<Texture, WGPUTexture> {
public:
  using ObjectBase::ObjectBase;
  using ObjectBase::operator=;

private:
  friend ObjectBase<Texture, WGPUTexture>;
  static void WGPUReference(WGPUTexture handle) { wgpuTextureReference(handle); }
  static void WGPURelease(WGPUTexture handle) { wgpuTextureRelease(handle); }
};

class TextureView : public ObjectBase<TextureView, WGPUTextureView> {
public:
  using ObjectBase::ObjectBase;
  using ObjectBase::operator=;

private:
  friend ObjectBase<TextureView, WGPUTextureView>;
  static void WGPUReference(WGPUTextureView handle) { wgpuTextureViewReference(handle); }
  static void WGPURelease(WGPUTextureView handle) { wgpuTextureViewRelease(handle); }
};

class Sampler : public ObjectBase<Sampler, WGPUSampler> {
public:
  using ObjectBase::ObjectBase;
  using ObjectBase::operator=;

private:
  friend ObjectBase<Sampler, WGPUSampler>;
  static void WGPUReference(WGPUSampler handle) { wgpuSamplerReference(handle); }
  static void WGPURelease(WGPUSampler handle) { wgpuSamplerRelease(handle); }
};
} // namespace wgpu
