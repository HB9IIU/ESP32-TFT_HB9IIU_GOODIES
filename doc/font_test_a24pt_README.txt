FONT TEST (a24pt7b)

This repo normally builds src/mainNEW.cpp which provides setup()/loop().
The file src/font_test_a24pt.cpp is wrapped in #ifdef FONT_TEST_A24PT,
so it only compiles when you enable that flag.

How to run the font test:

1) In platformio.ini, add to the desired env build_flags:
   -D FONT_TEST_A24PT

2) Temporarily exclude the normal main file to avoid duplicate setup()/loop():
   Option A (quick): rename src/mainNEW.cpp -> src/mainNEW.cpp__
   Option B (clean): add a src_filter for that env, e.g.
     src_filter =
       +<font_test_a24pt.cpp>
       -<mainNEW.cpp>
       -<*>

3) Build/upload as usual.

Expected:
- Black screen with white text rendered using a24pt7b.

Notes:
- Requires -D LOAD_GFXFF enabled (already in your platformio.ini common flags).
