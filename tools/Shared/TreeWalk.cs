using GBX.NET;
using GBX.NET.Engines.Plug;
public static class TreeWalk
{
    public static Iso4 Mul(Iso4 a, Iso4 b) => new Iso4(
        a.XX*b.XX + a.XY*b.YX + a.XZ*b.ZX, a.XX*b.XY + a.XY*b.YY + a.XZ*b.ZY, a.XX*b.XZ + a.XY*b.YZ + a.XZ*b.ZZ,
        a.YX*b.XX + a.YY*b.YX + a.YZ*b.ZX, a.YX*b.XY + a.YY*b.YY + a.YZ*b.ZY, a.YX*b.XZ + a.YY*b.YZ + a.YZ*b.ZZ,
        a.ZX*b.XX + a.ZY*b.YX + a.ZZ*b.ZX, a.ZX*b.XY + a.ZY*b.YY + a.ZZ*b.ZY, a.ZX*b.XZ + a.ZY*b.YZ + a.ZZ*b.ZZ,
        a.XX*b.TX + a.XY*b.TY + a.XZ*b.TZ + a.TX, a.YX*b.TX + a.YY*b.TY + a.YZ*b.TZ + a.TY, a.ZX*b.TX + a.ZY*b.TY + a.ZZ*b.TZ + a.TZ);

    /// Walks the tree; at a VisualMip picks level `lod` (0 = most detailed = smallest FarZ), clamped to the last level.
    public static IEnumerable<(CPlugTree Tree, Iso4 Loc, float FarZ)> Walk(CPlugTree tree, int lod, Iso4? parent = null, float farZ = float.PositiveInfinity)
    {
        var loc = Mul(parent ?? Iso4.Identity, tree.Location ?? Iso4.Identity);
        if (parent is null) loc = tree.Location ?? Iso4.Identity;
        yield return (tree, loc, farZ);
        if (tree is CPlugTreeVisualMip mip && mip.Levels.Count > 0)
        {
            var levels = mip.Levels.OrderBy(l => l.FarZ).ToList();
            var lv = levels[Math.Min(lod, levels.Count - 1)];
            if (lv.Tree is not null)
                foreach (var d in Walk(lv.Tree, lod, loc, lv.FarZ)) yield return d;
        }
        if (tree.Children is not null)
            foreach (var c in tree.Children) if (c is not null)
                foreach (var d in Walk(c, lod, loc, farZ)) yield return d;
    }
    public static int MaxLod(CPlugTree tree)
    {
        int m = 1;
        if (tree is CPlugTreeVisualMip mip) { m = Math.Max(m, mip.Levels.Count); foreach (var l in mip.Levels) if (l.Tree is not null) m = Math.Max(m, MaxLod(l.Tree)); }
        if (tree.Children is not null) foreach (var c in tree.Children) if (c is not null) m = Math.Max(m, MaxLod(c));
        return m;
    }
    public static CPlugSolid Deref(CPlugSolid s) { while (s.Model is not null) s = s.Model; return s; }
    public static int Tris(CPlugSolid s, int lod)
    {
        s = Deref(s); if (s.Tree is null) return 0; int t = 0;
        foreach (var (tr, _, _) in Walk(s.Tree, lod)) if (tr.Visual is CPlugVisualIndexedTriangles v) t += (v.IndexBuffer?.Indices.Length ?? 0) / 3;
        return t;
    }
    public static int CollisionTris(CPlugSolid s)
    {
        s = Deref(s); if (s.Tree is null) return 0; int t = 0;
        foreach (var (tr, _, _) in Walk(s.Tree, 0)) if (tr.Surface is CPlugSurface ps && ps.Geom?.Surf is CPlugSurface.Mesh m) t += m.CookedTriangles?.Length ?? m.Triangles?.Length ?? 0;
        return t;
    }
}
