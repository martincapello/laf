// LAF OS Library
// Copyright (C) 2024  Igara Studio S.A.
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#include "os/window.h"
#include "os/win/dnd.h"
#include "os/system.h"

#include "clip/clip.h"
#include "clip/clip_win.h"

#include <shlobj.h>

namespace {

DWORD as_dropeffect(const os::DropOperation op)
{
  DWORD effect = DROPEFFECT_NONE;
  if (static_cast<int>(op) & static_cast<int>(os::DropOperation::Copy))
    effect |= DROPEFFECT_COPY;

  if (static_cast<int>(op) & static_cast<int>(os::DropOperation::Move))
    effect |= DROPEFFECT_MOVE;

  if (static_cast<int>(op) & static_cast<int>(os::DropOperation::Link))
    effect |= DROPEFFECT_LINK;

  return effect;
}

os::DropOperation as_dropoperation(DWORD pdwEffect)
{
  int op = 0;
  if (pdwEffect & DROPEFFECT_COPY)
    op |= static_cast<int>(os::DropOperation::Copy);

  if (pdwEffect & DROPEFFECT_MOVE)
    op |= static_cast<int>(os::DropOperation::Move);

  if (pdwEffect & DROPEFFECT_LINK)
    op |= static_cast<int>(os::DropOperation::Link);

  return static_cast<os::DropOperation>(op);
}


gfx::Point drag_position(HWND hwnd, POINTL& pt)
{
  ScreenToClient(hwnd, (LPPOINT) &pt);
  return gfx::Point(pt.x, pt.y);
}

// HGLOBAL Locking/Unlocking wrapper
template<typename T>
class GLocked {
public:
  GLocked() = delete;
  GLocked(const GLocked&) = delete;
  GLocked(HGLOBAL hglobal) : m_hmem(hglobal) {
    m_data = static_cast<T>(GlobalLock(m_hmem));
  }

  ~GLocked() { GlobalUnlock(m_hmem); }

  operator HGLOBAL() { return m_hmem; }

  operator T() { return m_data; }

  bool operator==(std::nullptr_t) const { return m_data == nullptr; }
  bool operator!=(std::nullptr_t) const { return m_data != nullptr; }

  SIZE_T size() { return GlobalSize(m_hmem); }

private:
  HGLOBAL m_hmem;
  T m_data;
};

class DataWrapper {
public:
  DataWrapper() = delete;
  DataWrapper(const DataWrapper&) = delete;
  DataWrapper(IDataObject* data) : m_data(data) {}

  ~DataWrapper() { release(); }

  template<typename T>
  GLocked<T> get(CLIPFORMAT cfmt) {
    release();

    FORMATETC fmt;
    fmt.cfFormat = cfmt;
    fmt.ptd = nullptr;
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED::TYMED_HGLOBAL;
    m_result = m_data->GetData(&fmt, &m_medium);
    if (m_result != S_OK)
      return nullptr;

    return GLocked<T>(m_medium.hGlobal);
  }

private:
  void release() {
    if (m_result == S_OK) {
      ReleaseStgMedium(&m_medium);
      m_result = -1L;
    }
  }

  IDataObject* m_data = nullptr;
  STGMEDIUM m_medium;
  HRESULT m_result = -1L;
};

} // anonymous namespace

namespace os {

base::paths DragDataProviderWin::getPaths()
{
  base::paths files;
  DataWrapper data(m_data);
  GLocked<HDROP> hdrop = data.get<HDROP>(CF_HDROP);
  if (hdrop != nullptr) {
    int count = DragQueryFile(hdrop, 0xFFFFFFFF, nullptr, 0);
    for (int index = 0; index < count; ++index) {
      int length = DragQueryFile(hdrop, index, nullptr, 0);
      if (length > 0) {
        // From Win32 docs: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-dragqueryfilew
        // the DragQueryFile() doesn't include the null character in its return value.
        std::vector<TCHAR> str(length + 1);
        DragQueryFile(hdrop, index, str.data(), str.size());
        files.push_back(base::to_utf8(str.data()));
      }
    }
  }
  return files;
}

SurfaceRef DragDataProviderWin::getImage()
{
  SurfaceRef surface = nullptr;
  clip::image img;

  auto makeSurface = [](const clip::image& img) {
    SurfaceFormatData sfd;
    clip::image_spec spec = img.spec();
    sfd.bitsPerPixel = spec.bits_per_pixel;
    sfd.redMask = spec.red_mask;
    sfd.greenMask = spec.green_mask;
    sfd.blueMask = spec.blue_mask;
    sfd.alphaMask = spec.alpha_mask;
    sfd.redShift = spec.red_shift;
    sfd.greenShift = spec.green_shift;
    sfd.blueShift = spec.blue_shift;
    sfd.alphaShift = spec.alpha_shift;
    sfd.pixelAlpha = PixelAlpha::kStraight;
    return os::instance()->makeSurface(spec.width, spec.height, sfd, (unsigned char*)img.data());
  };

  DataWrapper data(m_data);
  UINT png_format = RegisterClipboardFormatA("PNG");
  if (png_format) {
    GLocked<uint8_t*> png_handle = data.get<uint8_t*>(png_format);
    if (png_handle != nullptr &&
        clip::win::read_png(png_handle, png_handle.size(), &img, nullptr))
      return makeSurface(img);
  }

  GLocked<BITMAPV5HEADER*> b5 = data.get<BITMAPV5HEADER*>(CF_DIBV5);
  if (b5 != nullptr) {
    clip::win::BitmapInfo bi(b5);
    if (bi.to_image(img))
      return makeSurface(img);
  }

  GLocked<BITMAPINFO*> hbi = data.get<BITMAPINFO*>(CF_DIB);
  if (hbi != nullptr) {
    clip::win::BitmapInfo bi(hbi);
    if (bi.to_image(img))
      return makeSurface(img);
  }

  // No suitable image format found.
  return nullptr;
}

bool DragDataProviderWin::contains(DragDataItemType type)
{
  base::ComPtr<IEnumFORMATETC> formats;
  if (m_data->EnumFormatEtc(DATADIR::DATADIR_GET, &formats) != S_OK)
    return false;

  char name[101];
  FORMATETC fmt;
  while (formats->Next(1, &fmt, nullptr) == S_OK) {
    switch (fmt.cfFormat) {
      case CF_HDROP:
        if (type == DragDataItemType::Paths)
          return true;
        break;
      case CF_DIBV5:
        if (type == DragDataItemType::Image)
          return true;
        break;
      case CF_DIB:
        if (type == DragDataItemType::Image)
          return true;
        break;
      default: {
        int namelen = GetClipboardFormatNameA(fmt.cfFormat, name, 100);
        name[namelen] = '\0';
        if (std::strcmp(name, "PNG") == 0 && type == DragDataItemType::Image)
          return true;
        break;
      }
    }
  }
  return false;
}

STDMETHODIMP DragTargetAdapter::QueryInterface(REFIID riid, LPVOID* ppv)
{
  if (!ppv)
    return E_INVALIDARG;

  *ppv = nullptr;
  if (riid != IID_IDropTarget && riid != IID_IUnknown)
    return E_NOINTERFACE;

  *ppv = static_cast<IDropTarget*>(this);
  AddRef();
  return NOERROR;
}

ULONG DragTargetAdapter::AddRef()
{
  return InterlockedIncrement(&m_ref);
}

ULONG DragTargetAdapter::Release()
{
  // Decrement the object's internal counter.
  ULONG ref = InterlockedDecrement(&m_ref);
  if (0 == ref)
    delete this;

  return ref;
}

STDMETHODIMP DragTargetAdapter::DragEnter(IDataObject* pDataObj,
                                          DWORD grfKeyState,
                                          POINTL pt,
                                          DWORD* pdwEffect)
{
  if (!m_window->hasDragTarget())
    return E_NOTIMPL;

  m_data = base::ComPtr<IDataObject>(pDataObj);
  if (!m_data)
    return E_UNEXPECTED;

  m_position = drag_position((HWND)m_window->nativeHandle(), pt);
  auto ddProvider = std::make_unique<DragDataProviderWin>(m_data.get());
  DragEvent ev(m_window,
               as_dropoperation(*pdwEffect),
               m_position,
               ddProvider.get());

  m_window->notifyDragEnter(ev);

  *pdwEffect = as_dropeffect(ev.dropResult());

  return S_OK;
}

STDMETHODIMP DragTargetAdapter::DragOver(DWORD grfKeyState,
                                         POINTL pt,
                                         DWORD* pdwEffect)
{
  if (!m_window->hasDragTarget())
    return E_NOTIMPL;

  m_position = drag_position((HWND)m_window->nativeHandle(), pt);
  auto ddProvider = std::make_unique<DragDataProviderWin>(m_data.get());
  DragEvent ev(m_window,
               as_dropoperation(*pdwEffect),
               m_position,
               ddProvider.get());

  m_window->notifyDrag(ev);

  *pdwEffect = as_dropeffect(ev.dropResult());

  return S_OK;
}

STDMETHODIMP DragTargetAdapter::DragLeave(void)
{
  if (!m_window->hasDragTarget())
    return E_NOTIMPL;

  auto ddProvider = std::make_unique<DragDataProviderWin>(m_data.get());
  os::DragEvent ev(m_window,
                   DropOperation::None,
                   m_position,
                   ddProvider.get());
  m_window->notifyDragLeave(ev);

  m_data.reset();
  return S_OK;
}

STDMETHODIMP DragTargetAdapter::Drop(IDataObject* pDataObj,
                                     DWORD grfKeyState,
                                     POINTL pt,
                                     DWORD* pdwEffect)
{
  if (!m_window->hasDragTarget())
    return E_NOTIMPL;

  m_data = base::ComPtr<IDataObject>(pDataObj);
  if (!m_data)
    return E_UNEXPECTED;

  m_position = drag_position((HWND)m_window->nativeHandle(), pt);
  auto ddProvider = std::make_unique<DragDataProviderWin>(m_data.get());
  DragEvent ev(m_window,
               as_dropoperation(*pdwEffect),
               m_position,
               ddProvider.get());

  m_window->notifyDrop(ev);

  m_data = nullptr;
  *pdwEffect = as_dropeffect(ev.dropResult());
  return S_OK;
}


} // namespase os
