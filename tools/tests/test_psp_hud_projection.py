"""Host regression test for MM's flat HUD projection; requires a C compiler.

Run with: python3 tools/tests/test_psp_hud_projection.py
"""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


def read_function(path, name):
    source = (ROOT / path).read_text()
    match = re.search(r"^void " + name + r"\([^;]*?\)\s*\{", source, re.M)
    if match is None:
        raise AssertionError(f"Missing function: {name}")
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


class HudProjectionTest(unittest.TestCase):
    def check_projection(self, psp):
        # Compile the actual HUD setup and orthographic matrix calculation.
        # Only the display-list submission and unrelated View fields are mocked.
        code = """
#include <assert.h>
#include <math.h>
typedef float f32;
typedef struct { float zNear, zFar; int fullscreen; } View;
typedef struct { View view; } InterfaceContext;
static float projection[4][4];
static void guMtxIdentF(float m[4][4]) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) m[i][j] = i == j;
}
#define SET_FULLSCREEN_VIEWPORT(v) ((v)->fullscreen = 1)
"""
        code += read_function("src/libultra/gu/ortho.c", "guOrthoF")
        code += """
static void View_ApplyOrthoToOverlay(View* view) {
    guOrthoF(projection, -160, 160, -120, 120,
             view->zNear, view->zFar, 1);
}
"""
        code += read_function("src/code/z_parameter.c", "Interface_SetOrthoView")
        code += """
int main(void) {
    const float farPlanes[] = {60, 12800};
    for (int i = 0; i < 2; i++) {
        InterfaceContext ctx = {{10, farPlanes[i], 0}};
        Interface_SetOrthoView(&ctx);
        assert(ctx.view.fullscreen);
        assert(ctx.view.zNear == 10 && ctx.view.zFar == farPlanes[i]);
        assert(fabsf(projection[0][0] - 1.0f / 160) < 0.000001f);
        assert(fabsf(projection[1][1] - 1.0f / 120) < 0.000001f);
        /* Every clock quad is at z=0: its clip z is the translation term.
         * Include the RSP matrix's 16.16 quantization in the check. */
        float z = (int)(projection[3][2] * 65536) / 65536.0f;
        float w = projection[3][3];
#if PLATFORM_PSP
        assert(z == 0 && w == 1);
        assert(z >= -w && z <= w);
#else
        /* Preserve the N64 projection; reproduce the PSP rejection cause. */
        assert(z < -w);
#endif
    }
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "hud.c"
            binary = Path(temp) / "hud"
            source.write_text(code)
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Werror", f"-DPLATFORM_PSP={psp}",
                 str(source), "-o", str(binary)], check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_psp_hud_vertices_survive_depth_clipping(self):
        self.check_projection(1)

    def test_n64_projection_is_preserved(self):
        self.check_projection(0)


if __name__ == "__main__":
    unittest.main()
