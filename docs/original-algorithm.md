# Original editing algorithm

The core comes from Matthew Tavares's `Clip.cpp`, `Clip.h`, and
`VideoCompiler.cpp` at commit `8b6c90b` (before the platform refactor).
Those files are restored at the repository root and compiled into both
`VideoTrimmer` and the headless `video_engine` through the `clip_core` library.

## Selection

`Clip::FindNotBlurry` retains the original sequential state machine:

1. Decode the video and check every `fps / 2` frames.
2. Classify a sampled frame as blurry when grayscale Laplacian variance is
   below the configured threshold (5 by default; 50 was the original value).
3. While a clear run is too short, move its starting boundary after each
   blurry sample. Short clear openings are discarded.
4. Once the run exceeds the minimum duration, keep its starting boundary.
5. Stop at the next blurry sample and use the last clear sampled endpoint.
6. If the clear run continues to EOF, retain its established boundaries.

The original default minimum is four seconds; the maximum is eight seconds.
Face-first adjustment remains optional and is limited to the selected shot.
The desktop `Clip::Create` retains its historical random centered trim. The
web/API engine instead returns the full blur-bounded shot, then applies any
requested output cap deterministically after selection so duration does not
change blur detection.

## Portability and correctness changes

- Paths use `std::filesystem`; no Windows-specific media or cascade paths.
- Face detection takes a caller-supplied cascade instead of a hardcoded path.
- OpenCV 4 and 5 headers/libraries are supported.
- Linked-list pointers are initialized and invalid media is rejected.
- Video duration uses floating-point FPS rather than truncated integer division.
- EOF preserves the start found after blur; the original omitted this assignment.
- Explicit minimum-duration settings reach `FindNotBlurry`; the default is still 4.
- Sampling has a minimum step of one frame for low-FPS input.
- Samples and selected timestamps are exposed for JSON diagnostics.

The original half-second sample cadence and boundary conventions are retained.
This is not frame-by-frame boundary refinement. The platform's former
median-threshold and ranked three-second highlight algorithms have been removed.

## Wrapper responsibilities

FastAPI invokes the C++ engine, validates duration constraints, and stores its
decisions. Flask calls the same REST API for a small demo dashboard, and MCP
exposes that API. Neither selects replacement footage.

## Regression coverage

CTest creates encoded video fixtures and checks that the core skips a short
opening, keeps the first sustained clear shot, stops at closing blur, handles
EOF after leading blur, honors an explicit shorter minimum, and rejects
all-blurry footage. The Docker build and GitHub Actions run these tests.

The local `pool2.MOV` check selected 2.5333–5.5000 seconds with an explicitly
requested two-second minimum. Its clear run does not meet the original
four-second default. No video files are committed to the repository.
