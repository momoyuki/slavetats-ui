#pragma once

#include "runtime/CatalogPreviewVisualSession.h"
#include "runtime/SkeeCapabilityProbe.h"

namespace stui::runtime {

CatalogPreviewVisualBindings makeSkyrimCatalogPreviewBindings(
    skee::OverrideInterface* interface);

}  // namespace stui::runtime
