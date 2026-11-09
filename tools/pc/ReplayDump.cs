using GBX.NET; using GBX.NET.Engines.Game; using GBX.NET.LZO; using System.Text.Json;
Gbx.LZO = new Lzo(); Gbx.ZLib = new GBX.NET.ZLib.ZLib();
var gbx = Gbx.Parse(args[0]);
Console.Error.WriteLine($"node {gbx.Node?.GetType().Name}");
if (gbx.Node is CGameCtnReplayRecord rr)
{
    foreach (var g in rr.GetGhosts())
    {
        Console.Error.WriteLine($"ghost {g.GhostNickname} time {g.RaceTime} model {g.PlayerModel} samples {g.SampleData?.Samples.Count} period {g.SampleData?.SamplePeriod}");
        var sd = g.SampleData; if (sd is null) continue;
        var list = sd.Samples.Select(s => new { t = s.Time.TotalMilliseconds, p = new[] { s.Position.X, s.Position.Y, s.Position.Z }, q = new[] { s.Rotation.X, s.Rotation.Y, s.Rotation.Z, s.Rotation.W }, v = new[] { s.Velocity.X, s.Velocity.Y, s.Velocity.Z } }).ToList();
        Console.WriteLine(JsonSerializer.Serialize(new { nick = g.GhostNickname, time = g.RaceTime?.TotalMilliseconds, samples = list }));
        break;
    }
}
