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

} // anonymous namespace

namespace os {

// HGLOBAL Locking/Unlocking wrapper
template <typename T>
class GLock {
public:
  GLock() = delete;
  GLock(const GLock&) = delete;
  GLock(HGLOBAL hglobal) : m_hmem(hglobal) {
    m_data = static_cast<T>(GlobalLock(m_hmem));
  }

  ~GLock() {
    GlobalUnlock(m_hmem);
  }

  operator HGLOBAL() {
    return m_hmem;
  }

  operator T () {
    return m_data;
  }

  SIZE_T size() {
    return GlobalSize(m_hmem);
  }

private:
  HGLOBAL m_hmem;
  T m_data;
};

base::paths DragDataProviderWin::getPaths()
{
  base::paths files;
  FORMATETC fmt;
  fmt.cfFormat = CF_HDROP;
  fmt.ptd = nullptr;
  fmt.dwAspect = DVASPECT_CONTENT;
  fmt.lindex = -1;
  fmt.tymed = TYMED::TYMED_HGLOBAL;
  STGMEDIUM medium;
  if (m_data->GetData(&fmt, &medium) == S_OK) {
    {
      GLock<HDROP> hdrop(medium.hGlobal);
      if (static_cast<HDROP>(hdrop)) {
        int count = DragQueryFile(hdrop, 0xFFFFFFFF, nullptr, 0);
        for (int index = 0; index < count; ++index) {
          int length = DragQueryFile(hdrop, index, nullptr, 0);
          if (length > 0) {
            std::vector<TCHAR> str(length + 1);
            DragQueryFile(hdrop, index, &str[0], str.size());
            files.push_back(base::to_utf8(&str[0]));
          }
        }
      }
    }
    ReleaseStgMedium(&medium);
  }
  return files;
}

SurfaceRef DragDataProviderWin::getImage()
{
  SurfaceRef surface = nullptr;
  clip::image_spec spec;
  clip::image img;
  SurfaceFormatData sfd;

  STGMEDIUM medium;
  medium.hGlobal = nullptr;
  FORMATETC fmt;
  fmt.ptd = nullptr;
  fmt.dwAspect = DVASPECT_CONTENT;
  fmt.lindex = -1;
  fmt.tymed = TYMED_HGLOBAL;

  UINT png_format = RegisterClipboardFormatA("PNG");
  if (png_format) {
    fmt.cfFormat = png_format;
    if (m_data->GetData(&fmt, &medium) == S_OK) {
      GLock<uint8_t*> png_handle(medium.hGlobal);
      if (clip::win::read_png(png_handle, png_handle.size(), &img, nullptr)) {
        spec = img.spec();
        goto makeSurface;
      }
    }
  }

  fmt.cfFormat = CF_DIBV5;
  if (m_data->GetData(&fmt, &medium) == S_OK) {
    GLock<BITMAPV5HEADER*> b5(medium.hGlobal);
    clip::win::BitmapInfo bi(b5);
    if (bi.to_image(img)) {
      spec = img.spec();
      goto makeSurface;
    }
  }

  fmt.cfFormat = CF_DIB;
  if (m_data->GetData(&fmt, &medium) == S_OK) {
    GLock<BITMAPINFO*> hbi(medium.hGlobal);
    clip::win::BitmapInfo bi(hbi);
    if (bi.to_image(img)) {
      spec = img.spec();
      goto makeSurface;
    }
  }

  // No suitable image format found, release medium and return.
  goto releaseMedium;

makeSurface:
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
  surface = os::instance()->makeSurface(spec.width, spec.height, sfd, (unsigned char*)img.data());

releaseMedium:
  ReleaseStgMedium(&medium);
  return surface;
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
      default:
        int namelen = GetClipboardFormatNameA(fmt.cfFormat, name, 100);
        name[namelen] = '\0';
        if (std::strcmp(name, "PNG") == 0 && type == DragDataItemType::Image)
          return true;
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

  m_position = drag_position((HWND) m_window->nativeHandle(), pt);
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
