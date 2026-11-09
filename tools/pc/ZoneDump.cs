using GBX.NET; using GBX.NET.Engines.Game; using GBX.NET.LZO;
Gbx.LZO = new Lzo(); Gbx.ZLib = new GBX.NET.ZLib.ZLib();
foreach (var f in Directory.EnumerateFiles(args[0], "*.TMZone*.Gbx", SearchOption.AllDirectories).Order())
{
    try
    {
        var g = Gbx.Parse(f);
        if (g.Node is CGameCtnZone z)
        {
            string extra = "";
            if (z is CGameCtnZoneFrontier zf) extra = $" frontier: block {zf.BlockInfoFrontier?.Name} parent {zf.ParentZoneId} child {zf.ChildZoneId}";
            if (z is CGameCtnZoneFlat zl) extra = $" flat: block {zl.BlockInfoFlat?.Name} road {zl.BlockInfoRoad?.Name} clip {zl.BlockInfoClip?.Name} pylon {zl.BlockInfoPylon?.Name} groundOnly {zl.GroundOnly}";
            Console.WriteLine($"{Path.GetFileName(f)}: zone {z.ZoneId} surf {z.SurfaceId} height {z.Height} depth {z.Depth} water {z.HasWater}{extra}");
        }
        else Console.WriteLine($"{Path.GetFileName(f)}: {g.Node?.GetType().Name}");
    }
    catch (Exception e) { Console.WriteLine($"{Path.GetFileName(f)}: ERR {e.GetType().Name} {e.Message.Split('\n')[0]}"); }
}
