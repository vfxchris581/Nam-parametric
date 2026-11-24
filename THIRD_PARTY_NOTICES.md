# Third-party notices

| Component | Use | License |
|---|---|---|
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (Steven Atkinson) | NAM model loading and real-time inference; fetched at build time, pinned commit | MIT |
| [parametric-nam](https://github.com/jojopas/parametric-nam) (Murphy Research, LLC) | The `ParametricWaveNet` file format (SPEC v1.1). `ParametricEngine/ParametricWaveNet.{h,cpp}` is adapted from its reference loader. `Resources/fixtures/panama_demo*` and `plain.nam` are its test fixtures. | MIT |
| [PANAMA](https://github.com/ETH-DISCO/PANAMA) (ETH Zürich) | Origin of the `panama_demo` fixture (converted demo checkpoint); its conditioning scheme is what the format encodes. No PANAMA code is included. | MIT |
| `fixtures/plain.nam` | NeuralAmpModelerCore example WaveNet, via parametric-nam | MIT |
| [Eigen](https://eigen.tuxfamily.org) 5.0.1 | Linear algebra (NAM core and the trainer); fetched at build time; built with `EIGEN_MPL2_ONLY` | MPL-2.0 |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON (bundled in NeuralAmpModelerCore) | MIT |
| [JUCE](https://juce.com) 8 | Desktop app, audio I/O, convolution | AGPLv3 or commercial JUCE licence |

JUCE is dual-licensed. Distributing this application under the AGPLv3 means its source must
remain available under the AGPLv3; a closed-source distribution needs a commercial JUCE licence.

TONEX is a trademark of IK Multimedia. This project is independent, is not affiliated with IK
Multimedia, and uses none of its code, models, graphics or assets.

## MIT licence text (NeuralAmpModelerCore, parametric-nam, PANAMA)

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Copyright (c) 2023 Steven Atkinson · Copyright (c) 2026 Murphy Research, LLC · PANAMA authors, ETH Zürich
