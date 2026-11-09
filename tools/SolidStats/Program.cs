using GBX.NET;
using GBX.NET.Engines.Plug;
using GBX.NET.LZO;
Gbx.LZO = new Lzo();
var root = args[0];
var objDir = args.Length > 1 ? args[1] : null;
Console.WriteLine("file\tlods\ttris_lod0\ttris_lod1\ttris_lod2\ttris_last\tcoll_tris\tsprites\tfarz\tmaterials");
foreach (var f in Directory.EnumerateFiles(root, "*.Solid.Gbx", SearchOption.AllDirectories).Order())
{
    var rel = Path.GetRelativePath(root, f);
    try
    {
        var gbx = Gbx.Parse(f);
        if (gbx.Node is not CPlugSolid solid) { Console.WriteLine($"{rel}\tNOT_SOLID {gbx.Node?.GetType().Name}"); continue; }
        solid = TreeWalk.Deref(solid);
        if (solid.Tree is null) { Console.WriteLine($"{rel}\t0\t0\t0\t0\t0\t0\t0\t\t"); continue; }
        int lods = TreeWalk.MaxLod(solid.Tree);
        var mats = new SortedSet<string>(); int sprites = 0; var farz = new SortedSet<float>();
        foreach (var (t, _, fz) in TreeWalk.Walk(solid.Tree, 0))
        {
            if (t.Visual is CPlugVisualSprite) sprites++;
            if (t.References.Shader.Descriptor is GBX.NET.Components.GbxRefTableFile rf) mats.Add(Path.GetFileName(rf.FilePath).Replace(".Material.Gbx", ""));
            if (!float.IsInfinity(fz)) farz.Add(fz);
        }
        Console.WriteLine($"{rel}\t{lods}\t{TreeWalk.Tris(solid, 0)}\t{TreeWalk.Tris(solid, 1)}\t{TreeWalk.Tris(solid, 2)}\t{TreeWalk.Tris(solid, lods - 1)}\t{TreeWalk.CollisionTris(solid)}\t{sprites}\t{string.Join("|", farz)}\t{string.Join("|", mats)}");
        if (objDir is not null)
        {
            var o = Path.Combine(objDir, Path.ChangeExtension(rel, null));
            Directory.CreateDirectory(Path.GetDirectoryName(o)!);
            solid.ExportToObj(o + ".obj", o + ".mtl");
        }
    }
    catch (Exception e) { Console.WriteLine($"{rel}\tERROR {e.GetType().Name}: {e.Message.Split('\n')[0]}"); }
}
