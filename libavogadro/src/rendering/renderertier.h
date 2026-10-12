/**********************************************************************
  Renderer tier selection (gpu-rendering-pipeline design A8/D7, task 7.1).

  Three tiers: `modern` (instanced shaders, GL 3.3+), `compat` (batched
  fixed-function, GL 2.0+), `legacy` (the frozen pre-existing immediate-mode
  renderer). Which tiers exist in the binary is decided at CMake time by
  AVO_RENDERER_TIER2/TIER1/LEGACY (exported here as AVO_RENDERER_* macros).
  Which tier runs is decided at startup: the highest compiled-in tier the live
  context supports, unless the AVOGADRO_RENDERER environment override forces a
  specific one. A forced tier that is compiled out or unsupported must fail
  loudly at startup instead of silently degrading (design D7: a clear
  diagnostic, not a fallback), because silently degrading hides driver
  problems and makes bug reports unreproducible.
 *****************************************************************/

#ifndef AVOGADRO_RENDERING_RENDERERTIER_H
#define AVOGADRO_RENDERING_RENDERERTIER_H

#include <QString>

namespace Avogadro {
namespace rendering {

enum class RendererTier {
  Modern,      //!< instanced shaders, GL 3.3+ compatibility profile (tier 2)
  Compat,      //!< batched fixed-function flush, GL 2.0+ (tier 1)
  Legacy,      //!< frozen pre-existing immediate-mode renderer (tier 0)
  Unavailable  //!< a forced request that cannot be honoured
};

//! True when the tier was compiled into this binary (CMake AVO_RENDERER_*).
bool tierCompiledIn(RendererTier tier);

//! The lowest GL version the tier needs, as (major, minor). Legacy has no
//! floor: it predates the versioned-API split and runs on anything.
void tierGlFloor(RendererTier tier, int &major, int &minor);

//! Human-readable tier name for logs and diagnostics: "modern", "compat",
//! "legacy" — the same spellings AVOGADRO_RENDERER accepts.
QString tierName(RendererTier tier);

//! Parse the AVOGADRO_RENDERER override (design A8). Returns the requested
//! tier and sets \a forced when the variable names a known tier. An unknown
//! non-empty value logs a warning and is ignored (falls back to probing), so
//! a typo cannot brick the app; a *known* tier that cannot run is handled by
//! resolveRendererTier() as a hard failure instead.
RendererTier requestedRendererTier(bool &forced);

//! Whether the live GL context (must be current) meets the tier's floor.
bool contextSupportsTier(RendererTier tier);

//! Resolve the tier to run (design D7): an explicitly forced request is
//! honoured when compiled in and supported, and returns Unavailable otherwise
//! (the caller fails with a clear diagnostic). Without an override the highest
//! compiled-in tier the context supports wins; Apple defaults to the legacy
//! tier (spec "macOS uses legacy") because the modern tier is not compiled in
//! there and the compat tier is not validated on that platform.
RendererTier resolveRendererTier(bool forced, RendererTier requested);

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_RENDERERTIER_H
