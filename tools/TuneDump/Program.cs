using System.Reflection; using GBX.NET; using GBX.NET.Engines.MwFoundations; using GBX.NET.Engines.Plug; using GBX.NET.Engines.Function; using GBX.NET.LZO;
Gbx.LZO = new Lzo(); Gbx.ZLib = new GBX.NET.ZLib.ZLib();
// every property of each tuning in a VehicleTunings file, curves as their keys
static string Show(object? v)
{
    if (v is null) return "null";
    if (v is PlugCurve pc)
    {
        if (pc.Curve is CFuncKeysReal ck) return "curve[" + string.Join(" ", (ck.Xs ?? Array.Empty<float>()).Zip(ck.Ys ?? Array.Empty<float>(), (x, y) => $"{x:0.###}:{y:0.###}")) + "]";
        if (pc.Values is not null) return "curve[" + string.Join(" ", pc.Values.Select(q => $"{q.X:0.###}:{q.Y:0.###}")) + "]";
        return "curve(empty)";
    }
    if (v is CFuncKeysReal k) return "curve[" + string.Join(" ", (k.Xs ?? Array.Empty<float>()).Zip(k.Ys ?? Array.Empty<float>(), (x, y) => $"{x:0.###}:{y:0.###}")) + "]";
    if (v is CMwNod n) { var t = n.GetType(); var sb = new List<string>(); foreach (var p in t.GetProperties(BindingFlags.Public | BindingFlags.Instance)) { if (p.GetIndexParameters().Length > 0 || p.Name is "Chunks" or "References" or "Id") continue; object? x; try { x = p.GetValue(n); } catch { continue; } if (x is float or int or bool or uint or double) sb.Add($"{p.Name}={x}"); else if (x is float[] fa) sb.Add($"{p.Name}=[{string.Join(" ", fa.Select(f => f.ToString("0.###")))}]"); } return t.Name + "{" + string.Join(", ", sb) + "}"; }
    if (v is float[] a) return "[" + string.Join(" ", a.Select(f => f.ToString("0.####"))) + "]";
    if (v is System.Collections.IEnumerable e && v is not string) return "[" + string.Join("; ", e.Cast<object?>().Select(Show)) + "]";
    return v.ToString() ?? "";
}
foreach (var f in args)
{
    var g = Gbx.Parse(f);
    Console.WriteLine($"=== {Path.GetFileName(f)}: {g.Node?.GetType().Name}");
    if (g.Node is not CPlugVehiclePhyTunings ts || ts.Tuning is null) continue;
    Console.WriteLine($"TuningIndex {ts.TuningIndex}");
    int i = 0;
    foreach (var t in ts.Tuning)
    {
        Console.WriteLine($"--- tuning {i++}: {t?.GetType().Name}");
        if (t is null) continue;
        foreach (var p in t.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
        {
            if (p.GetIndexParameters().Length > 0 || p.Name is "Chunks" or "References") continue;
            object? v; try { v = p.GetValue(t); } catch { continue; }
            Console.WriteLine($"  {p.Name} = {Show(v)}");
        }
    }
}
