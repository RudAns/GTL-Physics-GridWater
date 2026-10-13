"""Build every asset the demo needs, headless:

  UnrealEditor-Cmd.exe GridWater.uproject -run=pythonscript -script="<abs path>/Tools/build_assets.py"

Creates under /Game/GridWater: textures, KayKit props, M_Water, M_Terrain, M_Struct, M_Ghost, M_Sky, M_Prop
(+ MI_Prop, MI_PropGreen) and the empty map /Game/Maps/Valley. The HLSL lives here so the material logic
is reviewable as text. Water and terrain read the simulation from float data textures written each substep:
  TA0/TA1  previous/current  (R surface or floor height in blocks, G depth, B contamination, A cluster saturation)
  TB       (RG velocity in cells/s, B soil moisture 0..16, A soil contamination 0..1)
"""
import os
import unreal

ROOT = "/Game/GridWater"
HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")

PROPS = ["tree_pine_orange_large", "tree_pine_orange_medium", "tree_pine_orange_small", "tree_dead_large", "tree_dead_medium",
         "tree_dead_small", "pumpkin_orange", "pumpkin_orange_small", "fence", "fence_pillar", "post_lantern", "lantern_standing",
         "arch", "path_A", "path_B", "bench"]


def log(*a):
    unreal.log("[build_assets] " + " ".join(str(x) for x in a))


def fresh(name, cls, factory, path=ROOT):
    full = f"{path}/{name}"
    asset = unreal.load_asset(full) if EAL.does_asset_exist(full) else None
    if asset is None:
        asset = AT.create_asset(name, path, cls, factory)
    if isinstance(asset, unreal.Material):
        MEL.delete_all_material_expressions(asset)
    return asset


def import_task(filename, dest, name, options=None):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", filename)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    if options:
        t.set_editor_property("options", options)
    return t


def import_textures():
    src = [("T_Lin", os.path.join(HERE, "SourceArt", "T_Lin.png"), False),
           ("T_HB", os.path.join(PROJ, "SourceAssets", "KayKit", "Halloween", "halloweenbits_texture.png"), True),
           ("T_HBGreen", os.path.join(PROJ, "SourceAssets", "KayKit", "Halloween", "halloweenbits_texture_green.png"), True)]
    AT.import_asset_tasks([import_task(f, ROOT, n) for n, f, _ in src])
    for n, _, srgb in src:
        tex = unreal.load_asset(f"{ROOT}/{n}")
        tex.set_editor_property("srgb", srgb)
        if not srgb:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST if srgb else unreal.TextureFilter.TF_BILINEAR)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        EAL.save_loaded_asset(tex)


def import_props():
    tasks = []
    for n in PROPS:
        ui = unreal.FbxImportUI()
        ui.set_editor_property("automated_import_should_detect_type", False)
        ui.set_editor_property("import_materials", False)
        ui.set_editor_property("import_textures", False)
        ui.set_editor_property("import_mesh", True)
        ui.set_editor_property("import_as_skeletal", False)
        ui.set_editor_property("import_animations", False)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        sm = ui.get_editor_property("static_mesh_import_data")
        sm.set_editor_property("combine_meshes", True)
        sm.set_editor_property("auto_generate_collision", False)
        sm.set_editor_property("import_uniform_scale", 1.0)
        tasks.append(import_task(os.path.join(PROJ, "SourceAssets", "KayKit", "Halloween", n + ".fbx"), f"{ROOT}/Props", n, ui))
    AT.import_asset_tasks(tasks)
    for n in PROPS:
        m = unreal.load_asset(f"{ROOT}/Props/{n}")
        log("prop", n, m.get_bounds().box_extent if m else "FAILED")


class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.y = 0

    def node(self, cls, x=-1400, **props):
        e = MEL.create_material_expression(self.mat, cls, x, self.y)
        self.y += 90
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def custom(self, code, inputs, out_type, desc, x=-300):
        c = self.node(unreal.MaterialExpressionCustom, x)
        c.set_editor_property("code", code)
        c.set_editor_property("description", desc)
        c.set_editor_property("output_type", out_type)
        ins = []
        for name, _ in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", name)
            ins.append(ci)
        c.set_editor_property("inputs", ins)
        for name, (src, out) in inputs:
            MEL.connect_material_expressions(src, out, c, name)
        return c

    def scalar(self, name, v):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=v)

    def vector(self, name, v):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(*v))

    def tex(self, name, t):
        return self.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name=name, texture=t)

    def uv(self, i):
        return self.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=i)


F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1

# shared HLSL helpers (each custom node gets its own copy)
COMMON = r"""
struct GWF
{
    float2 G;
    float4 S(Texture2D T, SamplerState Sm, int2 q) { q = clamp(q, int2(0, 0), int2(G) - 1); return T.SampleLevel(Sm, (float2(q) + 0.5) / G, 0); }
    bool Inside(int2 q) { return q.x >= 0 && q.y >= 0 && q.x < (int)G.x && q.y < (int)G.y; }
    // Corner height: average the wet cells around the corner whose surface is close to ours, so slopes look smooth
    // while waterfalls (big steps) stay sharp and are closed by the side faces.
    float Corner(Texture2D T, SamplerState Sm, int2 c, int2 s)
    {
        float4 me = S(T, Sm, c);
        if (me.g <= 0.01) return me.r - 0.08;
        float sum = me.r, n = 1;
        [unroll] for (int k = 0; k < 3; k++)
        {
            int2 o = k == 0 ? int2(s.x, 0) : (k == 1 ? int2(0, s.y) : s);
            int2 q = c + o;
            if (!Inside(q)) continue;
            float4 v = S(T, Sm, q);
            if (v.g > 0.01 && abs(v.r - me.r) < 0.45) { sum += v.r; n += 1; }
        }
        return sum / n;
    }
    float3 R4(float3 a, float3 b, float3 c, float3 d, float t)
    {
        t = saturate(t) * 3;
        if (t < 1) return lerp(a, b, t);
        if (t < 2) return lerp(b, c, t - 1);
        return lerp(c, d, t - 2);
    }
    // keep in sync with the ramps in GWHud.cpp
    float3 Ramp(int ov, float t)
    {
        if (ov == 1) return R4(float3(0.02, 0.05, 0.15), float3(0.05, 0.35, 0.65), float3(0.30, 0.85, 0.95), float3(0.95, 1.0, 1.0), t);
        if (ov == 2) return R4(float3(0.15, 0.20, 0.60), float3(0.10, 0.70, 0.60), float3(0.95, 0.85, 0.20), float3(0.90, 0.25, 0.10), t);
        if (ov == 3) return R4(float3(0.10, 0.45, 0.80), float3(0.95, 0.85, 0.25), float3(0.85, 0.35, 0.10), float3(0.45, 0.10, 0.05), t);
        if (ov == 4) return R4(float3(0.27, 0.00, 0.33), float3(0.23, 0.32, 0.55), float3(0.13, 0.57, 0.55), float3(0.99, 0.90, 0.14), t);
        if (ov == 5) return R4(float3(0.45, 0.42, 0.38), float3(0.75, 0.85, 0.45), float3(0.30, 0.65, 0.25), float3(0.05, 0.35, 0.15), t);
        return R4(float3(0.45, 0.42, 0.38), float3(0.60, 0.45, 0.60), float3(0.55, 0.20, 0.55), float3(0.35, 0.05, 0.35), t);
    }
};
GWF f; f.G = Grid.xy;
"""

WATER_WPO = COMMON + r"""
if (UV1.x > 2.5) return float3(0, 0, 0);
int2 c = int2(floor(UV0 * Grid.xy));
int2 s = int2(round(UV2));
int2 d = int2(round(UV3));
float4 m0 = f.S(TA0, TA0Sampler, c), m1 = f.S(TA1, TA1Sampler, c);
float t0 = f.Corner(TA0, TA0Sampler, c, s), t1 = f.Corner(TA1, TA1Sampler, c, s);
float z0 = t0, z1 = t1;
if (UV1.x > 1.5)
{
    // a side face hangs from our surface down to the neighbour's surface (or ground); a dry cell has no faces at all
    int2 n = c + d;
    int2 s2 = s - 2 * d;          // the same world corner, seen from the neighbour
    float b0, b1;
    if (!f.Inside(n)) { b0 = m0.r - m0.g - 3.0; b1 = m1.r - m1.g - 3.0; }   // spilling off the edge of the map
    else { b0 = f.Corner(TA0, TA0Sampler, n, s2); b1 = f.Corner(TA1, TA1Sampler, n, s2); }
    z0 = m0.g > 0.01 ? min(b0, t0) : t0;
    z1 = m1.g > 0.01 ? min(b1, t1) : t1;
}
return float3(0, 0, lerp(z0, z1, Alpha) * Grid.w - P.z);
"""

WATER_SAMPLE = r"""
float2 uv = Pw.xy / (Grid.z * Grid.xy);
float4 a = lerp(TA0.SampleLevel(TA0Sampler, uv, 0), TA1.SampleLevel(TA1Sampler, uv, 0), Alpha);
float4 b = TB.SampleLevel(TBSampler, uv, 0);
float d = max(a.g, 0); float c = saturate(a.b); float sp = length(b.rg);
int ov = (int)round(Overlay);
"""

WATER_COLOR = COMMON + WATER_SAMPLE + r"""
float3 clean = lerp(float3(0.07, 0.30, 0.33), float3(0.010, 0.060, 0.12), saturate(d / 1.6));
float3 bad = lerp(float3(0.50, 0.13, 0.035), float3(0.24, 0.045, 0.02), saturate(d / 1.6));
float3 col = lerp(clean, bad, smoothstep(0.0, 0.8, c));
float3 N = normalize(cross(ddx(Pw), ddy(Pw)));
float side = 1 - abs(N.z);
float foam = saturate((sp - 1.2) * 0.5) * saturate(d * 4.0) + side * 0.75;
float grain = frac(sin(dot(floor(Pw.xy / 9 + Time * float2(1.1, 0.6)), float2(12.9898, 78.233))) * 43758.5453);
float3 foamCol = lerp(float3(0.78, 0.84, 0.84), float3(0.80, 0.55, 0.42), smoothstep(0.0, 0.8, c));
col = lerp(col, foamCol, saturate(foam * (0.55 + 0.45 * grain)) * 0.5);
col *= lerp(1.15, 1.0, saturate(d * 3.0));      // very shallow water reads lighter
if (ov >= 1 && ov <= 4) col *= 0.12;
if (ov >= 5) col = lerp(col, float3(0.30, 0.34, 0.38), 0.55);
return col;
"""

WATER_EMISSIVE = COMMON + WATER_SAMPLE + r"""
if (ov < 1 || ov > 4) return float3(0, 0, 0);
float t = ov == 1 ? d / 3.0 : ov == 2 ? sp / 2.5 : ov == 3 ? c : (a.a < 0.5 ? 0 : (a.a - 1) / 7.0);
return f.Ramp(ov, t) * 0.85;
"""

WATER_NORMAL = WATER_SAMPLE + r"""
float3 N = normalize(cross(ddx(Pw), ddy(Pw)));
if (dot(N, Cv) < 0) N = -N;
float2 v = b.rg;
float t = Time * 0.35;
float ph0 = frac(t), ph1 = frac(t + 0.5);
float w0 = 1 - abs(2 * ph0 - 1);
float2 p = Pw.xy / 70.0;
float2 q0 = p - v * ph0 * 1.4, q1 = p - v * ph1 * 1.4 + 0.37;
float2 g0 = float2(sin(dot(q0, float2(2.1, 1.3)) * 3.0 + 1.7), cos(dot(q0, float2(-1.2, 2.4)) * 2.6));
float2 g1 = float2(sin(dot(q1, float2(2.1, 1.3)) * 3.0 + 1.7), cos(dot(q1, float2(-1.2, 2.4)) * 2.6));
float2 g = lerp(g1, g0, w0) * saturate(length(v) * 2 + 0.2);
g += 0.35 * float2(sin(p.x * 5.3 + Time * 1.3 + p.y * 2.0), cos(p.y * 4.7 + Time * 1.1 - p.x * 1.5));
float k = 0.09 * abs(N.z);
return normalize(N + float3(g * k, 0));
"""

TERRAIN_COLOR = COMMON + r"""
float2 uv = Pw.xy / (Grid.z * Grid.xy);
float4 b = TB.SampleLevel(TBSampler, uv, 0);
float m = b.b, sc = b.a;
float top = VC.r, hs = VC.g, ao = VC.b;
float2 cellf = Pw.xy / Grid.z;
float2 nq = Pw.xy / 60.0; float2 ni = floor(nq); float2 nf = frac(nq); nf = nf * nf * (3 - 2 * nf);
float h00 = frac(sin(dot(ni, float2(12.9898, 78.233))) * 43758.5453), h10 = frac(sin(dot(ni + float2(1, 0), float2(12.9898, 78.233))) * 43758.5453);
float h01 = frac(sin(dot(ni + float2(0, 1), float2(12.9898, 78.233))) * 43758.5453), h11 = frac(sin(dot(ni + float2(1, 1), float2(12.9898, 78.233))) * 43758.5453);
float n1 = lerp(lerp(h00, h10, nf.x), lerp(h01, h11, nf.x), nf.y);
float n2 = 0.5 + 0.5 * sin(Pw.x * 0.11 + sin(Pw.y * 0.07) * 2.0) * cos(Pw.y * 0.09 + Pw.x * 0.02);
float3 dry = lerp(float3(0.52, 0.41, 0.27), float3(0.62, 0.51, 0.35), n1) * (0.93 + 0.12 * hs);
float2 cc = frac(Pw.xy / 31.0 + n1 * 0.37);
float crack = smoothstep(0.035, 0.0, min(min(cc.x, cc.y), min(1 - cc.x, 1 - cc.y)));
dry *= 1 - 0.16 * crack;
float3 grass = lerp(float3(0.13, 0.31, 0.07), float3(0.24, 0.43, 0.11), n1) * (0.90 + 0.18 * n2);
float3 bad = lerp(float3(0.36, 0.25, 0.27), float3(0.44, 0.31, 0.33), n1);
float3 topc = lerp(dry, grass, smoothstep(0.2, 3.0, m));
topc = lerp(topc, bad, smoothstep(0.15, 0.85, sc));
float zb = Pw.z / Grid.w;
float band = frac(zb);
float layer = frac(floor(zb) * 0.37 + 0.11);
float3 dirt = lerp(float3(0.34, 0.23, 0.15), float3(0.43, 0.31, 0.20), layer);
float3 rock = lerp(float3(0.32, 0.30, 0.28), float3(0.40, 0.38, 0.35), n2);
float3 side = lerp(dirt, rock, step(0.62, layer)) * (0.86 + 0.14 * n2);
side *= 1 - 0.22 * smoothstep(0.05, 0.0, band);
side = lerp(side, topc * 0.85, smoothstep(0.92, 0.985, ao) * step(0.5, 1 - top));
float3 col = top > 0.5 ? topc : side;
col *= lerp(0.62, 1.0, saturate(ao));
float2 gl = abs(frac(cellf) - 0.5);
col *= 1 - 0.32 * smoothstep(0.465, 0.5, max(gl.x, gl.y)) * top * GridLines;
int ov = (int)round(Overlay);
if (top > 0.5 && ov == 5) col = m <= 0.01 ? float3(0.30, 0.28, 0.26) : f.Ramp(5, m / 16.0);
if (top > 0.5 && ov == 6) col = f.Ramp(6, sc);
if (ov >= 1 && ov <= 4) col *= 0.5;
return col;
"""

STRUCT_COLOR = r"""
float3 c = Color.rgb;
float n = frac(sin(dot(floor(P.xyz / 6.0), float3(12.9898, 78.233, 37.719))) * 43758.5453);
if (Pattern > 0.5 && Pattern < 1.5)
{
    float g = sin(P.z * 0.9 + sin(P.x * 0.05 + P.y * 0.05) * 3.0) * 0.5 + 0.5;
    c *= 0.85 + 0.28 * g * (0.7 + 0.3 * n);
}
else if (Pattern > 1.5 && Pattern < 2.5)
{
    float2 q = float2(P.x + P.y, P.z) / float2(28.0, 18.0);
    float2 qq = q + float2(floor(q.y) * 0.5, 0);
    float2 fr = frac(qq);
    float mortar = smoothstep(0.07, 0.0, min(min(fr.x, 1 - fr.x), min(fr.y, 1 - fr.y)));
    float id = frac(sin(dot(floor(qq), float2(17.1, 63.7))) * 9431.3);
    c *= (0.80 + 0.32 * id) * (1 - 0.45 * mortar);
}
else c *= 0.92 + 0.12 * n;
return c;
"""

PROP_COLOR = r"""
float3 c = Atlas.Sample(AtlasSampler, UV).rgb;
float lum = dot(c, float3(0.3, 0.59, 0.11));
c = lerp(c, lum * float3(0.86, 0.66, 0.42), saturate(Wither));
c = lerp(c, lum * float3(0.58, 0.44, 0.60), saturate(Contam) * 0.85);
return c;
"""

SKY_COLOR = r"""
float3 dir = normalize(P - O);
float t = saturate(dir.z * 1.6 + 0.08);
float3 c = lerp(Horizon.rgb, Top.rgb, t);
c = lerp(c, Horizon.rgb * 0.55, saturate(-dir.z * 2.5));
return c;
"""


def set_flags(mat, **flags):
    for k, v in flags.items():
        try:
            mat.set_editor_property(k, v)
        except Exception as e:  # noqa
            log("flag", k, e)


def data_inputs(g, lin):
    return {"TA0": g.tex("TA0", lin), "TA1": g.tex("TA1", lin), "TB": g.tex("TB", lin), "Alpha": g.scalar("Alpha", 1.0),
            "Overlay": g.scalar("Overlay", 0.0), "Grid": g.vector("Grid", (96, 72, 100, 100))}


def build_water(lin):
    mat = fresh("M_Water", unreal.Material, unreal.MaterialFactoryNew())
    set_flags(mat, two_sided=True, tangent_space_normal=False)
    g = Graph(mat)
    d = data_inputs(g, lin)
    p_vs = g.node(unreal.MaterialExpressionWorldPosition, world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    p_ps = g.node(unreal.MaterialExpressionWorldPosition)
    cv = g.node(unreal.MaterialExpressionCameraVectorWS)
    time = g.node(unreal.MaterialExpressionTime)
    uvs = [g.uv(i) for i in range(4)]
    wpo = g.custom(WATER_WPO, [("P", (p_vs, "")), ("UV0", (uvs[0], "")), ("UV1", (uvs[1], "")), ("UV2", (uvs[2], "")), ("UV3", (uvs[3], "")),
                               ("TA0", (d["TA0"], "")), ("TA1", (d["TA1"], "")), ("Alpha", (d["Alpha"], "")), ("Grid", (d["Grid"], "RGBA"))], F3, "WaterWPO")
    common = [("Pw", (p_ps, "")), ("TA0", (d["TA0"], "")), ("TA1", (d["TA1"], "")), ("TB", (d["TB"], "")), ("Alpha", (d["Alpha"], "")),
              ("Overlay", (d["Overlay"], "")), ("Grid", (d["Grid"], "RGBA")), ("Time", (time, ""))]
    col = g.custom(WATER_COLOR, common, F3, "WaterColor")
    emi = g.custom(WATER_EMISSIVE, common, F3, "WaterOverlay")
    nrm = g.custom(WATER_NORMAL, common + [("Cv", (cv, ""))], F3, "WaterNormal")
    rough = g.node(unreal.MaterialExpressionConstant, r=0.06)
    spec = g.node(unreal.MaterialExpressionConstant, r=0.6)
    MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(emi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def build_terrain(lin):
    mat = fresh("M_Terrain", unreal.Material, unreal.MaterialFactoryNew())
    set_flags(mat, two_sided=True)
    g = Graph(mat)
    d = data_inputs(g, lin)
    p_ps = g.node(unreal.MaterialExpressionWorldPosition)
    vc = g.node(unreal.MaterialExpressionVertexColor)
    gl = g.scalar("GridLines", 0.0)
    col = g.custom(TERRAIN_COLOR, [("Pw", (p_ps, "")), ("VC", (vc, "")), ("TA1", (d["TA1"], "")), ("TB", (d["TB"], "")), ("Overlay", (d["Overlay"], "")),
                                   ("Grid", (d["Grid"], "RGBA")), ("GridLines", (gl, ""))], F3, "TerrainColor")
    rough = g.node(unreal.MaterialExpressionConstant, r=0.92)
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def build_struct():
    mat = fresh("M_Struct", unreal.Material, unreal.MaterialFactoryNew())
    g = Graph(mat)
    color = g.vector("Color", (0.5, 0.5, 0.5, 1))
    pattern = g.scalar("Pattern", 0.0)
    rough_p = g.scalar("Rough", 0.8)
    p = g.node(unreal.MaterialExpressionWorldPosition)
    col = g.custom(STRUCT_COLOR, [("Color", (color, "")), ("Pattern", (pattern, "")), ("P", (p, ""))], F3, "StructColor")
    emi = g.custom("return Pattern > 2.5 ? Color.rgb * 6.0 : float3(0, 0, 0);", [("Color", (color, "")), ("Pattern", (pattern, ""))], F3, "Lamp")
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(emi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(rough_p, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def build_ghost():
    mat = fresh("M_Ghost", unreal.Material, unreal.MaterialFactoryNew())
    set_flags(mat, blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT, shading_model=unreal.MaterialShadingModel.MSM_UNLIT, two_sided=True)
    g = Graph(mat)
    color = g.vector("Color", (0.4, 1, 0.5, 0.45))
    rgb = g.custom("return Color.rgb * 1.6;", [("Color", (color, "RGBA"))], F3, "GhostRGB")
    alpha = g.custom("return Color.a;", [("Color", (color, "RGBA"))], F1, "GhostA")
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def build_sky():
    mat = fresh("M_Sky", unreal.Material, unreal.MaterialFactoryNew())
    set_flags(mat, shading_model=unreal.MaterialShadingModel.MSM_UNLIT, two_sided=True)
    g = Graph(mat)
    top = g.vector("Top", (0.22, 0.45, 0.80, 1))
    hz = g.vector("Horizon", (0.70, 0.80, 0.90, 1))
    p = g.node(unreal.MaterialExpressionWorldPosition)
    o = g.node(unreal.MaterialExpressionObjectPositionWS)
    col = g.custom(SKY_COLOR, [("P", (p, "")), ("O", (o, "")), ("Top", (top, "")), ("Horizon", (hz, ""))], F3, "Sky")
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def build_prop(atlas, green):
    mat = fresh("M_Prop", unreal.Material, unreal.MaterialFactoryNew())
    set_flags(mat, used_with_instanced_static_meshes=True)
    g = Graph(mat)
    tex = g.tex("Atlas", atlas)
    uv = g.uv(0)
    wither = g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=0)
    contam = g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=1)
    col = g.custom(PROP_COLOR, [("Atlas", (tex, "")), ("UV", (uv, "")), ("Wither", (wither, "")), ("Contam", (contam, ""))], F3, "PropColor")
    rough = g.node(unreal.MaterialExpressionConstant, r=0.85)
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    for name, t in (("MI_Prop", atlas), ("MI_PropGreen", green)):
        mi = fresh(name, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi.set_editor_property("parent", mat)
        MEL.set_material_instance_texture_parameter_value(mi, "Atlas", t)
        EAL.save_loaded_asset(mi)


def build_map():
    path = "/Game/Maps/Valley"
    if EAL.does_asset_exist(path):
        log("map exists")
        return
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    log("map saved", unreal.EditorLoadingAndSavingUtils.save_map(world, path))


def main():
    import_textures()
    lin = unreal.load_asset(f"{ROOT}/T_Lin")
    if not EAL.does_asset_exist(f"{ROOT}/Props/arch"):
        import_props()
    build_water(lin)
    build_terrain(lin)
    build_struct()
    build_ghost()
    build_sky()
    build_prop(unreal.load_asset(f"{ROOT}/T_HB"), unreal.load_asset(f"{ROOT}/T_HBGreen"))
    build_map()
    EAL.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    log("done")


main()
