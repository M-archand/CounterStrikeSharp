using System.Runtime.InteropServices;
using CounterStrikeSharp.API.Core;

namespace CounterStrikeSharp.API.Tests;

public class ScriptContextTests
{
    // Guard field after the context catches writes past result[8].
    [StructLayout(LayoutKind.Sequential)]
    private struct GuardedContext
    {
        public fxScriptContext Context;
        public ulong Guard;
    }

    private const ulong GuardValue = 0xDEADBEEFCAFEBABE;

    [Theory]
    [InlineData("hello")]
    [InlineData("héllo wörld ✓")]
    [InlineData("")]
    public unsafe void SetResult_String_WritesPointerIntoResultSlot(string value)
    {
        var guarded = new GuardedContext { Guard = GuardValue };
        var scriptContext = new ScriptContext();

        scriptContext.SetResult(value, &guarded.Context);

        Assert.Equal(GuardValue, guarded.Guard);

        var nativeUtf8 = *(IntPtr*)guarded.Context.result;
        Assert.NotEqual(IntPtr.Zero, nativeUtf8);
        Assert.Equal(value, Marshal.PtrToStringUTF8(nativeUtf8));
    }

    [Fact]
    public unsafe void SetResult_String_LongerValueAfterShorterOneIsIntact()
    {
        var guarded = new GuardedContext { Guard = GuardValue };
        var scriptContext = new ScriptContext();
        var longValue = new string('x', 4096);

        scriptContext.SetResult("short", &guarded.Context);
        scriptContext.SetResult(longValue, &guarded.Context);

        Assert.Equal(GuardValue, guarded.Guard);
        Assert.Equal(longValue, Marshal.PtrToStringUTF8(*(IntPtr*)guarded.Context.result));
    }

    [Fact]
    public unsafe void SetResult_Null_WritesNullPointer()
    {
        var guarded = new GuardedContext { Guard = GuardValue };
        var scriptContext = new ScriptContext();

        scriptContext.SetResult("not null", &guarded.Context);
        scriptContext.SetResult(null!, &guarded.Context);

        Assert.Equal(GuardValue, guarded.Guard);
        Assert.Equal(IntPtr.Zero, *(IntPtr*)guarded.Context.result);
    }
}
