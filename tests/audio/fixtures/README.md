# Authored audio regression banks

`banks/Master.bank` and `banks/Master.strings.bank` are test data generated on 2026-10-31
by FMOD Studio 2.03.14 from the repository's `Editor/LuxSampleProject/Assets/Audio/SampleProject`
and `tests/audio/AuthorFixture.js`. They contain the SurfaceHit/SurfaceMotion and music events,
parameters and buses described in `tests/audio/README.md`. They contain no SDK libraries or code.

The recipe copies the sample authoring project to a disposable directory before editing it.
To regenerate, run Python from the repository root with FMOD Studio's command-line application
on PATH (or its path in `FMODSTUDIOCLI`):

```python
import importlib.util
from pathlib import Path
import shutil
spec = importlib.util.spec_from_file_location("audio_runner", "tests/audio/run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
output = Path("bin/audio-fixture-regeneration").resolve()
output.mkdir(parents=True, exist_ok=False)
banks = runner.fixture(output)
for name in ("Master.bank", "Master.strings.bank"):
    shutil.copy2(banks / name, Path("tests/audio/fixtures/banks") / name)
```

FMOD assigns fresh IDs during generation, so regenerated files need not have the same bytes.
Validate regenerated banks by running the complete audio suite before committing them.
The fixture uses the sample project's existing source audio; no audio backend is replaced.
