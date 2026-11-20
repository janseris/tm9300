// KitExport: one environment's block kit out of a pak dump, for the Nokia 9300 packer.
//   KitExport <dumpdir e.g. dump/Rally> <out.json>
// Output: { materials: [{name, texture}], solids: { name: { bbox, lods: [ { verts:[x,y,z...], faces:[a,b,c,mat...] } ],
//           sprites: [x,y,z,size...], coll: { verts:[...], tris:[a,b,c,surf...], surfs:[names] } } },
//           blocks: [ { name, kind, ground: [[solid,...],...], air: [[...]] } ] }
using GBX.NET; using GBX.NET.Components; using GBX.NET.Engines.Game; using GBX.NET.Engines.Plug; using GBX.NET.Engines.Scene; using GBX.NET.LZO;
using System.Text.Json;
Gbx.LZO = new Lzo();
var root = args[0]; var outPath = args[1];
var materials = new List<object>(); var matIndex = new Dictionary<string, int>();
int Mat(string matFile)
{
    var name = Path.GetFileName(matFile).Replace(".Material.Gbx", "");
    if (matIndex.TryGetValue(name, out var i)) return i;
    string tex = "";
    try
    {
        var full = Directory.EnumerateFiles(root, Path.GetFileName(matFile), SearchOption.AllDirectories).FirstOrDefault();
        if (full is not null)
        {
            var g = Gbx.Parse(full);
            foreach (var rf in g.RefTable?.Files ?? [])
            {
                if (!rf.FilePath.EndsWith(".Texture.gbx", StringComparison.OrdinalIgnoreCase) && !rf.FilePath.EndsWith(".Texture.Gbx")) continue;
                var tp = g.RefTable!.GetFilePath(rf);
                if (!File.Exists(tp)) { var alt = Directory.EnumerateFiles(Path.GetDirectoryName(tp)!).FirstOrDefault(c => string.Equals(Path.GetFileName(c), Path.GetFileName(tp), StringComparison.OrdinalIgnoreCase)); if (alt is null) continue; tp = alt; }
                var tg = Gbx.Parse(tp);
                if (tg.Node is CPlugBitmap bm && bm.References.Image.Descriptor is GbxRefTableFile img && img.FilePath.Length > 0) { tex = Path.GetFileName(img.FilePath); break; }
            }
        }
    } catch (Exception e) { Console.Error.WriteLine($"material {name}: {e.GetType().Name}"); }
    i = materials.Count; matIndex[name] = i; materials.Add(new { name, texture = tex }); return i;
}
var solids = new Dictionary<string, object>();
var solidPaths = new Dictionary<string, string>();
object ExportLod(CPlugSolid s, int lod, out int ntris)
{
    var verts = new List<float>(); var faces = new List<int>(); var norms = new List<float>(); ntris = 0;
    foreach (var (t, loc, _) in TreeWalk.Walk(s.Tree!, lod))
    {
        if (t.Visual is not CPlugVisualIndexedTriangles v || v.IndexBuffer is null) continue;
        int mat = t.References.Shader.Descriptor is GbxRefTableFile rf ? Mat(rf.FilePath) : -1;
        int b = verts.Count / 3;
        foreach (var vx in v.Vertices)
        {
            var p = vx.Position;
            verts.Add(p.X * loc.XX + p.Y * loc.XY + p.Z * loc.XZ + loc.TX);
            verts.Add(p.X * loc.YX + p.Y * loc.YY + p.Z * loc.YZ + loc.TY);
            verts.Add(p.X * loc.ZX + p.Y * loc.ZY + p.Z * loc.ZZ + loc.TZ);
            var n = vx.Normal ?? new Vec3(0, 1, 0);
            norms.Add(n.X * loc.XX + n.Y * loc.XY + n.Z * loc.XZ); norms.Add(n.X * loc.YX + n.Y * loc.YY + n.Z * loc.YZ); norms.Add(n.X * loc.ZX + n.Y * loc.ZY + n.Z * loc.ZZ);
        }
        var idx = v.IndexBuffer.Indices;
        for (int k = 0; k + 2 < idx.Length; k += 3) { faces.Add(b + idx[k]); faces.Add(b + idx[k + 1]); faces.Add(b + idx[k + 2]); faces.Add(mat); ntris++; }
    }
    return new { verts, norms, faces };
}
object? ExportSolid(CPlugSolid s0)
{
    var s = TreeWalk.Deref(s0); if (s.Tree is null) return null;
    int lods = TreeWalk.MaxLod(s.Tree);
    var lodList = new List<object>(); var counts = new List<int>();
    for (int l = 0; l < lods; l++) { lodList.Add(ExportLod(s, l, out var n)); counts.Add(n); }
    var sprites = new List<float>();
    var cv = new List<float>(); var ct = new List<int>(); var surfs = new List<string>();
    float[] bb = { 1e9f, 1e9f, 1e9f, -1e9f, -1e9f, -1e9f };
    foreach (var (t, loc, _) in TreeWalk.Walk(s.Tree, 0))
    {
        if (t.Visual is CPlugVisualSprite sp)
            foreach (var vx in sp.Vertices)
            {
                var p = vx.Position;
                sprites.Add(p.X * loc.XX + p.Y * loc.XY + p.Z * loc.XZ + loc.TX); sprites.Add(p.X * loc.YX + p.Y * loc.YY + p.Z * loc.YZ + loc.TY); sprites.Add(p.X * loc.ZX + p.Y * loc.ZY + p.Z * loc.ZZ + loc.TZ);
                sprites.Add(vx.Normal?.X ?? 1);
            }
        if (t.Surface is CPlugSurface ps && ps.Geom?.Surf is CPlugSurface.Mesh m)
        {
            int b = cv.Count / 3;
            foreach (var p in m.Vertices) { cv.Add(p.X * loc.XX + p.Y * loc.XY + p.Z * loc.XZ + loc.TX); cv.Add(p.X * loc.YX + p.Y * loc.YY + p.Z * loc.YZ + loc.TY); cv.Add(p.X * loc.ZX + p.Y * loc.ZY + p.Z * loc.ZZ + loc.TZ); }
            int sb = surfs.Count;
            foreach (var sm in ps.Materials) { string id = sm.SurfaceId?.ToString() ?? ""; if (id.Length == 0) { try { id = sm.Material?.SurfaceId.ToString() ?? ""; } catch { } } if (id.Length == 0) id = "?"; surfs.Add(id); }
            if (m.CookedTriangles is not null) foreach (var tr in m.CookedTriangles) { ct.Add(b + tr.Indices.X); ct.Add(b + tr.Indices.Y); ct.Add(b + tr.Indices.Z); ct.Add(sb + tr.SurfaceIndex); }
            else if (m.Triangles is not null) foreach (var tr in m.Triangles) { ct.Add(b + tr.Indices.X); ct.Add(b + tr.Indices.Y); ct.Add(b + tr.Indices.Z); ct.Add(sb + tr.SurfaceIndex); }
        }
    }
    var l0 = (dynamic)lodList[0];
    var vl = (List<float>)l0.verts;
    for (int i = 0; i < vl.Count; i += 3) for (int k = 0; k < 3; k++) { bb[k] = Math.Min(bb[k], vl[i + k]); bb[3 + k] = Math.Max(bb[3 + k], vl[i + k]); }
    if (vl.Count == 0) for (int i = 0; i < cv.Count; i += 3) for (int k = 0; k < 3; k++) { bb[k] = Math.Min(bb[k], cv[i + k]); bb[3 + k] = Math.Max(bb[3 + k], cv[i + k]); }
    return new { bbox = bb, tris = counts, lods = lodList, sprites, coll = new { verts = cv, tris = ct, surfs } };
}
// the solid's key: its file name, with its folder in front when the folder is a block's own (Alpine keeps a
// "GreatWallGTCurve3/Air.Solid.Gbx" per block: the plain names collide)
string SolidName(CPlugSolid s)
{
    string f = ""; while (s.Model is not null) { f = (s.References.Model.Descriptor as GbxRefTableFile)?.FilePath ?? f; s = s.Model; }
    var name = Path.GetFileName(f).Replace(".Solid.Gbx", "");
    var folder = Path.GetFileName(Path.GetDirectoryName(f.Replace('\\', '/')) ?? "");
    if (name.Length == 0 || folder.Length == 0 || folder == "Solid" || folder == "Media") return name;
    // 0.18: Bay keeps Road/Turbo/Ground, Bridge/Turbo/Ground and Platform/Turbo/Ground: the first one keeps the short
    // key, the others get their grandparent folder in front (they all were the first one's shape)
    var norm = f.Replace('\\', '/'); var at = norm.LastIndexOf("/Solid/", StringComparison.OrdinalIgnoreCase);
    var full = (at >= 0 ? norm.Substring(at + 7) : norm).ToLowerInvariant();
    var key = folder + "/" + name;
    if (!solidPaths.TryGetValue(key, out var was)) { solidPaths[key] = full; return key; }
    if (was == full) return key;
    var gp = Path.GetFileName(Path.GetDirectoryName(Path.GetDirectoryName(norm) ?? "") ?? "");
    return gp + "/" + key;
}
var blocks = new List<object>();
foreach (var f in Directory.EnumerateFiles(root, "*.TMED*.Gbx", SearchOption.AllDirectories).Order())
{
    Gbx gbx; try { gbx = Gbx.Parse(f); } catch (Exception e) { Console.Error.WriteLine($"{f}: {e.GetType().Name}"); continue; }
    if (gbx.Node is not CGameCtnBlockInfo bi) continue;
    List<List<string>> Rows(NodeRefList<CSceneMobil>[]? rows)
    {
        var res = new List<List<string>>(); if (rows is null) return res;
        foreach (var row in rows)
        {
            var names = new List<string>();
            if (row is not null) foreach (var m in row)
            {
                try
                {
                    var sol = m?.Item?.Solid; if (sol is null) { names.Add(""); continue; }
                    var name = SolidName(sol);
                    if (name.Length == 0) { names.Add(""); continue; }
                    if (!solids.ContainsKey(name)) { var ex = ExportSolid(sol); if (ex is not null) solids[name] = ex; }
                    names.Add(solids.ContainsKey(name) ? name : "");
                } catch (Exception e) { Console.Error.WriteLine($"{bi.Name}: {e.GetType().Name} {e.Message.Split('\n')[0]}"); names.Add(""); }
            }
            res.Add(names);
        }
        return res;
    }
    // the newer layout (chunk 0x01E): CGameCtnBlockInfoMobil rows, each with its OldMobil (the big Alpine pieces)
    List<List<string>> Rows2(NodeRefList<CGameCtnBlockInfoMobil>[]? rows)
    {
        var res = new List<List<string>>(); if (rows is null) return res;
        foreach (var row in rows)
        {
            var names = new List<string>();
            if (row is not null) foreach (var bm in row)
            {
                try
                {
                    var sol = bm?.OldMobil?.Item?.Solid ?? bm?.SolidFid; if (sol is null) { names.Add(""); continue; }
                    var name = SolidName(sol);
                    if (name.Length == 0) { names.Add(""); continue; }
                    if (!solids.ContainsKey(name)) { var ex = ExportSolid(sol); if (ex is not null) solids[name] = ex; }
                    names.Add(solids.ContainsKey(name) ? name : "");
                } catch (Exception e) { Console.Error.WriteLine($"{bi.Name}: {e.GetType().Name} {e.Message.Split('\n')[0]}"); names.Add(""); }
            }
            res.Add(names);
        }
        return res;
    }
    var units = (bi.GroundBlockUnitInfos ?? bi.AirBlockUnitInfos)?.Select(u => new[] { u.RelativeOffset.X, u.RelativeOffset.Y, u.RelativeOffset.Z }).ToList();
    var ground = Rows(bi.GroundMobils); var air = Rows(bi.AirMobils);
    if (ground.Count == 0 && air.Count == 0) { ground = Rows2(bi.GroundBlockInfoMobils); air = Rows2(bi.AirBlockInfoMobils); }
    blocks.Add(new { name = bi.Name, kind = bi.GetType().Name.Replace("CGameCtnBlockInfo", ""), wp = bi.WayPointType.ToString(), units, replacement = bi.IsReplacement, pillar = bi.IsPillar,
        ground, air });
    Console.Error.Write($"\r{blocks.Count} blocks, {solids.Count} solids   ");
}
// the car, and the base plates (the terrain under and around the map, one per map size: Media/Solid/Base/30x30 ...)
// (0.15: every solid of Vehicles/Media/Solid: Desert's car is American.Solid.Gbx)
foreach (var f in Directory.EnumerateFiles(root, "*.Solid.Gbx", SearchOption.AllDirectories))
{
    if (!f.Replace('\\', '/').Contains("/Vehicles/Media/Solid/")) continue;
    var g = Gbx.Parse(f); if (g.Node is CPlugSolid cs) { var ex = ExportSolid(cs); if (ex is not null) solids[Path.GetFileName(f).Replace(".Solid.Gbx", "")] = ex; }
}
foreach (var f in Directory.EnumerateFiles(root, "*.Solid.Gbx", SearchOption.AllDirectories))
{
    if (!f.Replace('\\', '/').Contains("/Solid/Base/")) continue;
    try { var g = Gbx.Parse(f); if (g.Node is CPlugSolid cs) { var ex = ExportSolid(cs); if (ex is not null) solids["Base/" + Path.GetFileName(f).Replace(".Solid.Gbx", "")] = ex; } }
    catch (Exception e) { Console.Error.WriteLine($"{f}: {e.GetType().Name}"); }
}
// the zones: a zone block's mesh is placed at its cell minus the zone's height (checked against the replays)
var zones = new List<object>();
foreach (var f in Directory.EnumerateFiles(root, "*.TMZone*.Gbx", SearchOption.AllDirectories).Order())
{
    try
    {
        var g = Gbx.Parse(f);
        if (g.Node is CGameCtnZone z)
        {
            string block = z is CGameCtnZoneFrontier zf ? zf.BlockInfoFrontier?.Name ?? "" : z is CGameCtnZoneFlat zl ? zl.BlockInfoFlat?.Name ?? "" : "";
            zones.Add(new { zone = z.ZoneId, block, height = z.Height, depth = z.Depth, water = z.HasWater, kind = z is CGameCtnZoneFrontier ? "frontier" : "flat" });
        }
    }
    catch (Exception e) { Console.Error.WriteLine($"{f}: {e.GetType().Name}"); }
}
Console.Error.WriteLine();
File.WriteAllText(outPath, JsonSerializer.Serialize(new { materials, solids, blocks, zones }));
Console.Error.WriteLine($"wrote {outPath}: {materials.Count} materials, {solids.Count} solids, {blocks.Count} blocks");
