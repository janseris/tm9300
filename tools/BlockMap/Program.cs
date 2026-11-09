using GBX.NET.Components;
using GBX.NET;
using GBX.NET.Engines.Game;
using GBX.NET.Engines.Plug;
using GBX.NET.Engines.Scene;
using GBX.NET.Engines.Hms;
using GBX.NET.LZO;
using System.Text.Json;

Gbx.LZO = new Lzo();
var root = args[0];
var outRows = new List<object>();
foreach (var f in Directory.EnumerateFiles(root, "*.TMED*.Gbx", SearchOption.AllDirectories).Order())
{
    Gbx gbx; try { gbx = Gbx.Parse(f); } catch (Exception e) { Console.Error.WriteLine($"{f}: {e.GetType().Name} {e.Message.Split('\n')[0]}"); continue; }
    if (gbx.Node is not CGameCtnBlockInfo bi) { Console.Error.WriteLine($"{f}: {gbx.Node?.GetType().Name}"); continue; }
    var kind = bi.GetType().Name.Replace("CGameCtnBlockInfo", "");
    object Variants(NodeRefList<CSceneMobil>[]? rows)
    {
        var vs = new List<object>();
        if (rows is null) return vs;
        for (int vi = 0; vi < rows.Length; vi++)
        {
            var mobs = new List<object>();
            var row = rows[vi]; if (row is null) { vs.Add(mobs); continue; }
            for (int ui = 0; ui < row.Count; ui++)
            {
                CSceneMobil? m = null; string err = "";
                try { m = row[ui]; } catch (Exception e) { err = e.GetType().Name; }
                int tris = -1, trisLast = -1, coll = -1, lods = 0; string mname = m?.Name ?? ""; int links = 0;
                try
                {
                    var solid = m?.Item?.Solid; string sfile = "";
                    while (solid is not null && solid.Model is not null) { sfile = (solid.References.Model.Descriptor as GbxRefTableFile)?.FilePath ?? sfile; solid = solid.Model; }
                    if (solid is not null && solid.Tree is not null) { lods = TreeWalk.MaxLod(solid.Tree); tris = TreeWalk.Tris(solid, 0); trisLast = TreeWalk.Tris(solid, lods - 1); coll = TreeWalk.CollisionTris(solid); }
                    mname = sfile;
                    links = m?.ObjectLink?.Count ?? 0;
                } catch (Exception e) { err += " " + e.GetType().Name + ":" + e.Message.Split('\n')[0]; }
                mobs.Add(new { sub = ui, solid = mname, tris, trisLast, lods, coll, links, err });
            }
            vs.Add(mobs);
        }
        return vs;
    }
    var unitsG = bi.GroundBlockUnitInfos?.Select(u => new { off = u.RelativeOffset.ToString(), dir = u.Dir.ToString(), surf = u.Surface }).ToList();
    var unitsA = bi.AirBlockUnitInfos?.Select(u => new { off = u.RelativeOffset.ToString(), dir = u.Dir.ToString(), surf = u.Surface }).ToList();
    var rec = new { name = bi.Name, kind, file = Path.GetRelativePath(root, f), isPillar = bi.IsPillar, wp = bi.WayPointType.ToString(),
        groundUnits = unitsG, airUnits = unitsA, groundMobils = Variants(bi.GroundMobils), airMobils = Variants(bi.AirMobils) };
    outRows.Add(rec);
}
Console.WriteLine(JsonSerializer.Serialize(outRows, new JsonSerializerOptions { WriteIndented = false }));
