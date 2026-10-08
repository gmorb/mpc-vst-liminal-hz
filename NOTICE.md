# Notices

**Liminal Hz for MPC OS** is a convolution reverb for Akai MPC OS devices, by the authors of NAM A2 for MPC OS.
It is not affiliated with or endorsed by TONE3000, the Neural Amp Modeler project, or Akai Professional / inMusic.

This plugin's own code is MIT-licensed (`LICENSE`). The binary also contains:

| Component | Authors | Licence | Used for |
|---|---|---|---|
| PFFFT | Julien Pommier; marton78 and contributors | BSD-like FFTPACK licence (`engine/pffft/LICENSE.txt`) | the FFTs of the convolution |
| AudioDSPTools, `Resample.h` | Steven Atkinson | MIT (`src/audiodsptools/LICENSE`) | resampling IRs to the host rate |
| nlohmann/json | Niels Lohmann | MIT (in the header) | reading TONE3000's replies |
| Titillium Web (baked into the page's images; Regular and SemiBold in art/fonts for the phone pages, OFL.txt beside them) | Accademia di Belle Arti di Urbino | SIL OFL 1.1 | page titles, phone pages |

Built with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (sd88me) at `081c247310560cc94bcdb0dd4db3a40e395cbe96` (page and plugin-list
generator, Q-Link maps, release tools). At that commit that repository states no licence of its own; its bundled
force-shadow tools are MIT (Copyright (c) 2026 sd88me).

## Trademarks
"Akai", "MPC" and "Force" are trademarks of inMusic Brands; "TONE3000" is its owner's. They are used only to say
which devices this runs on and where IRs come from.
