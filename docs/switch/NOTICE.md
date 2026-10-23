# Source origin and notices

New probe/validation/host-check code is marked CC0-1.0, matching Dusklight's
root dedication. No code or assets from a game disc are used in the probe.
The example API/lifetime reference was `deko_basic` shipped in
`switch-examples 20260201-1` inside the pinned toolchain image. The probe is an
independently authored fixture, not the original example or a game port.

This candidate distributes source fixtures only, not an NRO or installed
devkitPro archives. Toolchain and installed archive reference identities are
recorded in `source-pins.json`; they are not rebuilt-source equivalence claims.
Any later binary distribution needs its own runtime compliance review, including
GCC's Runtime Library Exception and Newlib/libstdc++ notices where applicable.

## deko3d

Notice verified in `devkitPro/deko3d` at both source reference `373bfd4…` and
tag `v0.5.0`. The probe links the image's deko3d `0.5.0-1` validation archive;
that archive was not rebuilt from the reference tip.

```text
Copyright (C) 2018-2020 fincs

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you
   must not claim that you wrote the original software. If you use
   this software in a product, an acknowledgment in the product
   documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and
   must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source
   distribution.
```

## libnx

Notice retrieved from `switchbrew/libnx` tag v4.12.0,
commit `7644c9b26099aa2d2145bc72a21ee24190e92085`, `LICENSE.md`.

```text
Copyright 2017-2018 libnx Authors

Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

## Audited, not imported into the game

- Dusklight and the old Switch fork have root CC0 dedication. This does not
  override third-party rights or establish the origin of the old adapted
  disc-loader implementation.
- Aurora and Borealis are MIT; their notices remain in their unchanged
  submodules. SDL is zlib; googletest is BSD-3-Clause. Host checks use dedicated
  checkouts with their notices intact.
- This integration fixture does not include Dawn/Tint or UAM implementation
  patches. Those have separate component-specific terms; the root dedication
  does not replace third-party licenses.
- WiiCompiled-Switch's root is GPL-3.0-only. Its helper scripts were studied,
  not copied into this CC0 work.
- SwitchWaker's root is MIT, but substantial renderer/cache files are MPL-2.0;
  its vendored deko3d/uam/Dawn dependencies have additional terms. The direct
  renderer was studied, not transplanted.
- No proprietary game files, generated proprietary data, private shader caches,
  console captures, credentials or remote publication are part of this work.
