using System.Collections.Generic;
using System.Linq;
using CounterStrikeSharp.API;
using CounterStrikeSharp.API.Core;
using CounterStrikeSharp.API.Modules.Memory;
using CounterStrikeSharp.API.Modules.Memory.DynamicFunctions;
using CounterStrikeSharp.API.Modules.Utils;
using Xunit;

namespace NativeTestsPlugin;

/// <summary>
/// SetParam&lt;string&gt; pushes a managed HGlobal that is freed as soon as the native call returns unless an
/// outer managed Invoke holds the cleanup lock. Engine-originated hook calls never have that outer Invoke.
/// The decisive probe reads the argument back inside the pre hook, right after SetParam, so it does not
/// depend on post-hook or recall semantics. Only engine-originated calls are used: a managed Invoke of the
/// hooked function does not reach its own hooks (observed 2026-11-08, pre/post hook calls 0/0).
/// </summary>
public class DynamicHookStringTests
{
    private const string TerroristMarker = "/tm_";
    private const string CounterTerroristMarker = "/ctm_";

    /// <summary>Collects what the hooks saw so a failure message explains itself without server logs.</summary>
    private sealed class Trail
    {
        private readonly List<string> _lines = new();
        public int PreCalls, PreMatched, PostCalls, PostMatched;

        public void Add(string line)
        {
            if (_lines.Count < 40) _lines.Add(line);
        }

        public override string ToString() =>
            $"pre calls/matched={PreCalls}/{PreMatched} post calls/matched={PostCalls}/{PostMatched}\n" + string.Join("\n", _lines);
    }

    private static string ModelName(CCSPlayerPawn pawn) =>
        pawn.CBodyComponent!.SceneNode!.GetSkeletonInstance().ModelState.ModelName;

    private static IEnumerable<CCSPlayerPawn> LivePawns() =>
        Utilities.GetPlayers()
            .Select(p => p.PlayerPawn.Value)
            .Where(p => p is { IsValid: true } && p.LifeState == (byte)LifeState_t.LIFE_ALIVE && p.CBodyComponent?.SceneNode != null)!;

    private static string? FindLiveModel(string marker) =>
        LivePawns().Select(ModelName).FirstOrDefault(m => !string.IsNullOrEmpty(m) && m.Contains(marker, StringComparison.Ordinal));

    /// <summary>Same directory scheme as the terrorist model, so both <c>characters/models</c> and <c>agents/models</c> work.</summary>
    private static string CounterTerroristModelFor(string terroristModel)
    {
        var cut = terroristModel.IndexOf(TerroristMarker, StringComparison.Ordinal);
        return cut < 0 ? terroristModel : terroristModel[..cut] + "/ctm_sas/ctm_sas.vmdl";
    }

    private static Func<DynamicHook, HookResult> Guarded(Trail trail, string phase, Func<DynamicHook, HookResult> body) => hook =>
    {
        try
        {
            return body(hook);
        }
        catch (Exception e)
        {
            trail.Add($"{phase} threw {e.GetType().Name}: {e.Message}");
            return HookResult.Continue;
        }
    };

    private sealed class EngineSpawnRun
    {
        public Trail Trail = new();
        public string CounterTerroristModel = "";
        public Dictionary<IntPtr, string> ExpectedByEntity = new();
        public List<string> BadReadBacks = new(), PostMismatches = new();
    }

    /// <summary>Hooks SetModel, then lets the engine spawn fresh bots. Every terrorist model is swapped to a counter-terrorist one.</summary>
    private static async Task<EngineSpawnRun> RunEngineSpawn()
    {
        var run = new EngineSpawnRun { CounterTerroristModel = FindLiveModel(CounterTerroristMarker) ?? "" };
        var trail = run.Trail;
        var pending = new Dictionary<IntPtr, Stack<string>>();

        var pre = Guarded(trail, "pre", hook =>
        {
            trail.PreCalls++;
            var self = hook.GetParam<IntPtr>(0);
            var model = hook.GetParam<string>(1);
            if (model is null || !model.Contains(TerroristMarker, StringComparison.Ordinal))
            {
                trail.Add($"pre skip this=0x{self:X} model='{model}'");
                return HookResult.Continue;
            }

            trail.PreMatched++;
            var replacement = run.CounterTerroristModel.Length > 0 ? run.CounterTerroristModel : CounterTerroristModelFor(model);
            hook.SetParam(1, replacement);
            var readBack = hook.GetParam<string>(1);
            trail.Add($"pre this=0x{self:X} '{model}' -> '{replacement}' readback='{readBack}'");
            if (readBack != replacement) run.BadReadBacks.Add($"0x{self:X}: read back '{readBack}'");
            if (!pending.TryGetValue(self, out var stack)) pending[self] = stack = new Stack<string>();
            stack.Push(replacement);
            run.ExpectedByEntity[self] = replacement;
            return HookResult.Changed;
        });
        var post = Guarded(trail, "post", hook =>
        {
            trail.PostCalls++;
            var self = hook.GetParam<IntPtr>(0);
            var model = hook.GetParam<string>(1);
            if (!pending.TryGetValue(self, out var stack) || stack.Count == 0) return HookResult.Continue;
            trail.PostMatched++;
            var want = stack.Pop();
            trail.Add($"post this=0x{self:X} callee got '{model}'");
            if (want != model) run.PostMismatches.Add($"0x{self:X}: expected '{want}', callee received '{model}'");
            return HookResult.Continue;
        });

        VirtualFunctions.SetModelFunc.Hook(pre, HookMode.Pre);
        VirtualFunctions.SetModelFunc.Hook(post, HookMode.Post);
        try
        {
            await CreateTestPlayerAsync();
            await WaitOneFrame();
            await WaitOneFrame();
        }
        finally
        {
            VirtualFunctions.SetModelFunc.Unhook(pre, HookMode.Pre);
            VirtualFunctions.SetModelFunc.Unhook(post, HookMode.Post);
        }

        Assert.True(trail.PreCalls > 0, $"Inconclusive: bot spawn never reached CBaseModelEntity::SetModel.\n{trail}");
        Assert.True(trail.PreMatched > 0, $"Inconclusive: SetModel ran during spawn but never with a terrorist model.\n{trail}");
        return run;
    }

    [Fact]
    public async Task EngineSpawn_CalleeArgumentSurvivesCleanup()
    {
        var run = await RunEngineSpawn();
        Assert.True(run.BadReadBacks.Count == 0,
            $"Argument corrupted right after SetParam<string>:\n{string.Join("\n", run.BadReadBacks)}\n{run.Trail}");
    }

    [Fact]
    public async Task EngineSpawn_PostHookSeesReplacement()
    {
        var run = await RunEngineSpawn();
        Assert.True(run.Trail.PostMatched > 0, $"Post hook never fired for a replaced call.\n{run.Trail}");
        Assert.True(run.PostMismatches.Count == 0, $"{string.Join("\n", run.PostMismatches)}\n{run.Trail}");
    }

    [Fact]
    public async Task EngineSpawn_ModelApplied()
    {
        var run = await RunEngineSpawn();
        var checkedPawns = 0;
        var wrong = new List<string>();
        foreach (var pawn in LivePawns())
        {
            if (!run.ExpectedByEntity.TryGetValue(pawn.Handle, out var expected)) continue;
            checkedPawns++;
            var actual = ModelName(pawn);
            if (actual != expected) wrong.Add($"0x{pawn.Handle:X}: expected '{expected}', engine shows '{actual}'");
        }

        Assert.True(checkedPawns > 0, $"Inconclusive: no live pawn whose model was replaced.\n{run.Trail}");
        Assert.True(wrong.Count == 0, $"{string.Join("\n", wrong)}\n{run.Trail}");
    }
}
