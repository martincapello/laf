// LAF OS Library
// Copyright (C) 2024  Igara Studio S.A.
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#ifndef OS_OSX_DND_H_INCLUDED
#define OS_OSX_DND_H_INCLUDED
#pragma once

#ifdef __OBJC__

#include "base/exception.h"
#include "base/fs.h"
#include "base/paths.h"
#include "clip/clip.h"
#include "clip/clip_osx.h"
#include "gfx/point.h"
#include "os/dnd.h"
#include "os/surface.h"
#include "os/surface_format.h"
#include "os/system.h"

#include <Cocoa/Cocoa.h>
#include <memory>

namespace os {
  class DragDataProviderOSX : public DragDataProvider {
  public:
    DragDataProviderOSX(NSPasteboard* pasteboard) : m_pasteboard(pasteboard) {}

  private:
    NSPasteboard* m_pasteboard;

    base::paths getPaths() override {
      base::paths files;

      if ([m_pasteboard.types containsObject:NSFilenamesPboardType]) {
        NSArray* filenames = [m_pasteboard propertyListForType:NSFilenamesPboardType];
        for (int i=0; i<[filenames count]; ++i) {
          NSString* fn = [filenames objectAtIndex: i];

          files.push_back(base::normalize_path([fn UTF8String]));
        }
      }
      return files;
    }

    SurfaceRef getImage() override {
      clip::image img;
      clip::image_spec spec;
      if (!clip::osx::get_image_from_clipboard(m_pasteboard, &img, &spec))
        return nullptr;

      os::SurfaceFormatData sfd;
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

      SurfaceRef surface = os::instance()->makeSurface(
                              spec.width, spec.height,
                              sfd, (unsigned char*) img.data());
      return surface;
    }

    bool contains(DragDataItemType type) override {
      for (NSPasteboardType t in m_pasteboard.types) {
        if (type == DragDataItemType::Paths &&
            [t isEqual: NSFilenamesPboardType])
          return true;

        if (type == DragDataItemType::Image &&
            ([t isEqual: NSPasteboardTypeTIFF] ||
             [t isEqual: NSPasteboardTypePNG]))
          return true;
      }
      return false;
    }
  };
} // namespace os

NSDragOperation as_nsdragoperation(const os::DropOperation op)
{
  NSDragOperation nsdop;
  if (op & os::DropOperation::Copy)
    nsdop |= NSDragOperationCopy;

  if (op & os::DropOperation::Move)
    nsdop |= NSDragOperationMove;

  if (op & os::DropOperation::Link)
    nsdop |= NSDragOperationLink;

  return nsdop;
}

os::DropOperation as_dropoperation(const NSDragOperation nsdop)
{
  int op = 0;
  if (nsdop & NSDragOperationCopy)
    op |= os::DropOperation::Copy;

  if (nsdop & NSDragOperationMove)
    op |= os::DropOperation::Move;

  if (nsdop & NSDragOperationLink)
    op |= os::DropOperation::Link;

  return static_cast<os::DropOperation>(op);
}

gfx::Point drag_position(id<NSDraggingInfo> sender)
{
  NSRect contentRect = [sender.draggingDestinationWindow contentRectForFrameRect:sender.draggingDestinationWindow.frame];
  return gfx::Point(
    sender.draggingLocation.x,
    contentRect.size.height - sender.draggingLocation.y);
}

#endif

#endif
