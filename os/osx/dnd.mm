// LAF OS Library
// Copyright (C) 2024  Igara Studio S.A.
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#include "base/exception.h"
#include "base/fs.h"
#include "clip/clip.h"
#include "clip/clip_osx.h"
#include "os/osx/dnd.h"
#include "os/surface_format.h"
#include "os/system.h"

#include <memory>

#ifdef __OBJC__

namespace os {

base::paths DragDataProviderOSX::getPaths()
{
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

SurfaceRef DragDataProviderOSX::getImage()
{
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

bool DragDataProviderOSX::contains(DragDataItemType type)
{
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

} // namespace os

NSDragOperation as_nsdragoperation(const os::DropOperation op)
{
  NSDragOperation nsdop;
  if (static_cast<int>(op) & static_cast<int>(os::DropOperation::Copy))
    nsdop |= NSDragOperationCopy;

  if (static_cast<int>(op) & static_cast<int>(os::DropOperation::Move))
    nsdop |= NSDragOperationMove;

  if (static_cast<int>(op) & static_cast<int>(os::DropOperation::Link))
    nsdop |= NSDragOperationLink;

  return nsdop;
}

os::DropOperation as_dropoperation(const NSDragOperation nsdop)
{
  int op = 0;
  if (nsdop & NSDragOperationCopy)
    op |= static_cast<int>(os::DropOperation::Copy);

  if (nsdop & NSDragOperationMove)
    op |= static_cast<int>(os::DropOperation::Move);

  if (nsdop & NSDragOperationLink)
    op |= static_cast<int>(os::DropOperation::Link);

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
