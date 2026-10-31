// FXAA (Video tab "Anti-aliasing"; renderer metal.m fxaa_encode, vulkan/shaders.c "fxaa.frag"): the FXAA 3.11
// "quality" algorithm (Timothy Lottes, NVIDIA), written out here for both shading languages. Each backend puts a
// prelude in front that defines F2..F4, SAT, TAP(p) (a bilinear sample of the game image at mip 0) and TAPO(p, x, y)
// (the same, x / y whole texels away), and FXAA_ARGS (the texture parameter where the language needs one).
//
// Per pixel: skip it when the local luma contrast is low; else find whether the edge through it runs horizontally or
// vertically and which neighbour is across it, walk along the edge both ways until the luma leaves the edge's, and
// sample shifted towards the neighbour by how close the pixel is to the nearer end (a long edge's middle barely moves,
// its ends blend half). Lone bright / dark pixels get a separate subpixel blend.
const char fxaa_src[] =
    "float fxaa_luma(F3 c) { return dot(c, F3(0.299, 0.587, 0.114)); }\n"
    "F4 fxaa(FXAA_ARGS F2 uv, F2 rcp) {\n"
    "  F4 m = TAP(uv);\n"
    "  float lM = fxaa_luma(m.rgb);\n"
    "  float lN = fxaa_luma(TAPO(uv, 0, -1).rgb), lS = fxaa_luma(TAPO(uv, 0, 1).rgb);\n"
    "  float lW = fxaa_luma(TAPO(uv, -1, 0).rgb), lE = fxaa_luma(TAPO(uv, 1, 0).rgb);\n"
    "  float lMax = max(max(max(lN, lS), max(lW, lE)), lM), lMin = min(min(min(lN, lS), min(lW, lE)), lM);\n"
    "  float range = lMax - lMin;\n"
    "  if (range < max(0.0312, lMax * 0.125)) return m;\n"  // (FXAA's edge_threshold_min / edge_threshold)
    "  float lNW = fxaa_luma(TAPO(uv, -1, -1).rgb), lNE = fxaa_luma(TAPO(uv, 1, -1).rgb);\n"
    "  float lSW = fxaa_luma(TAPO(uv, -1, 1).rgb), lSE = fxaa_luma(TAPO(uv, 1, 1).rgb);\n"
    // a horizontal edge changes luma from row to row: second differences down the columns vs. along the rows
    "  float eH = 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE) + abs(lNW + lSW - 2.0 * lW);\n"
    "  float eV = 2.0 * abs(lW + lE - 2.0 * lM) + abs(lNW + lNE - 2.0 * lN) + abs(lSW + lSE - 2.0 * lS);\n"
    "  bool horz = eH >= eV;\n"
    // the neighbour across the edge: the side with the bigger step (N / W are the negative direction)
    "  float l1 = horz ? lN : lW, l2 = horz ? lS : lE;\n"
    "  float g1 = abs(l1 - lM), g2 = abs(l2 - lM);\n"
    "  float px = horz ? rcp.y : rcp.x, lEdge, gMin;\n"
    "  if (g1 >= g2) { px = -px; lEdge = 0.5 * (l1 + lM); gMin = 0.25 * g1; }\n"
    "  else { lEdge = 0.5 * (l2 + lM); gMin = 0.25 * g2; }\n"
    "  F2 e = uv, dir = horz ? F2(rcp.x, 0.0) : F2(0.0, rcp.y);\n"
    "  if (horz) e.y += 0.5 * px; else e.x += 0.5 * px;\n"  // on the edge, halfway to that neighbour
    "  F2 p1 = e - dir, p2 = e + dir;\n"
    "  float d1 = fxaa_luma(TAP(p1).rgb) - lEdge, d2 = fxaa_luma(TAP(p2).rgb) - lEdge;\n"
    "  bool end1 = abs(d1) >= gMin, end2 = abs(d2) >= gMin;\n"
    "  for (int i = 1; i < 12 && !(end1 && end2); i++) {\n"
    "    float q = i < 5 ? 1.0 : i == 5 ? 1.5 : i < 10 ? 2.0 : i == 10 ? 4.0 : 8.0;\n"  // (the quality preset's steps)
    "    if (!end1) { p1 -= dir * q; d1 = fxaa_luma(TAP(p1).rgb) - lEdge; end1 = abs(d1) >= gMin; }\n"
    "    if (!end2) { p2 += dir * q; d2 = fxaa_luma(TAP(p2).rgb) - lEdge; end2 = abs(d2) >= gMin; }\n"
    "  }\n"
    "  float s1 = horz ? uv.x - p1.x : uv.y - p1.y, s2 = horz ? p2.x - uv.x : p2.y - uv.y;\n"
    "  bool near1 = s1 < s2;\n"
    // blend only on the edge's side where the luma at the nearer end goes the other way from the centre
    "  bool ok = ((near1 ? d1 : d2) < 0.0) != (lM < lEdge);\n"
    "  float off = ok ? 0.5 - min(s1, s2) / (s1 + s2) : 0.0;\n"
    "  float sub = SAT(abs((2.0 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) / 12.0 - lM) / range);\n"
    "  sub = (3.0 - 2.0 * sub) * sub * sub;\n"
    "  off = max(off, sub * sub * 0.75);\n"
    "  F2 f = uv;\n"
    "  if (horz) f.y += off * px; else f.x += off * px;\n"
    "  return F4(TAP(f).rgb, m.a);\n"
    "}\n";
