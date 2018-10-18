// LAF OS Library
// Copyright (C) 2018  Igara Studio S.A.
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifndef OS_SKIA_SKIA_COLOR_SPACE_INCLUDED
#define OS_SKIA_SKIA_COLOR_SPACE_INCLUDED
#pragma once

#include "base/disable_copying.h"
#include "os/color_space.h"

#include "SkColorSpace.h"
#include "SkColorSpaceXform.h"

namespace os {

class SkiaColorSpace : public ColorSpace {
public:
  SkiaColorSpace(const gfx::ColorSpacePtr& gfxcs);

  const gfx::ColorSpacePtr& gfxColorSpace() const override { return m_gfxcs; }
  sk_sp<SkColorSpace> skColorSpace() const { return m_skcs; }

private:
  gfx::ColorSpacePtr m_gfxcs;
  sk_sp<SkColorSpace> m_skcs;

  DISABLE_COPYING(SkiaColorSpace);
};

class SkiaColorSpaceConversion : public ColorSpaceConversion {
public:
  SkiaColorSpaceConversion(const os::ColorSpacePtr& srcColorSpace,
                           const os::ColorSpacePtr& dstColorSpace);

  bool isValid() const { return m_xform != nullptr; }

  bool convert(uint32_t* dst, const uint32_t* src, int n) override;

private:
  // Both pointers just to keep a reference to them
  os::ColorSpacePtr m_srcCS;
  os::ColorSpacePtr m_dstCS;

  std::unique_ptr<SkColorSpaceXform> m_xform;
};

os::ColorSpacePtr main_screen_color_space();
void list_screen_color_spaces(std::vector<os::ColorSpacePtr>& list);

} // namespace os

#endif
