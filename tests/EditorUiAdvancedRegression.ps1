param(
    [ValidateSet('GizmoMarquee', 'ShaderMaterial')]
    [string]$Scenario = 'GizmoMarquee'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TinyEditorAdvancedQaNative {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr handle, int command);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out RECT rect);
    [DllImport("user32.dll")] public static extern IntPtr SetProcessDpiAwarenessContext(IntPtr value);
}
'@

[TinyEditorAdvancedQaNative]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

function Invoke-MouseButton([int]$X, [int]$Y, [uint32]$Down, [uint32]$Up) {
    [TinyEditorAdvancedQaNative]::SetCursorPos(($script:WindowLeft + $X), ($script:WindowTop + $Y)) | Out-Null
    Start-Sleep -Milliseconds 100
    [TinyEditorAdvancedQaNative]::mouse_event($Down, 0, 0, 0, [UIntPtr]::Zero)
    [TinyEditorAdvancedQaNative]::mouse_event($Up, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
}

function Click-At([int]$X, [int]$Y) {
    Invoke-MouseButton $X $Y 0x0002 0x0004
}

function Drag-At([int]$FromX, [int]$FromY, [int]$ToX, [int]$ToY, [switch]$Ctrl) {
    if ($Ctrl) { [TinyEditorAdvancedQaNative]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero) }
    [TinyEditorAdvancedQaNative]::SetCursorPos(($script:WindowLeft + $FromX), ($script:WindowTop + $FromY)) | Out-Null
    [TinyEditorAdvancedQaNative]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 180
    for ($step = 1; $step -le 20; ++$step) {
        $x = [int]($FromX + ($ToX - $FromX) * $step / 20)
        $y = [int]($FromY + ($ToY - $FromY) * $step / 20)
        [TinyEditorAdvancedQaNative]::SetCursorPos(($script:WindowLeft + $x), ($script:WindowTop + $y)) | Out-Null
        Start-Sleep -Milliseconds 20
    }
    [TinyEditorAdvancedQaNative]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    if ($Ctrl) { [TinyEditorAdvancedQaNative]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 500
}

function Press-CtrlKey([byte]$Key) {
    [TinyEditorAdvancedQaNative]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [TinyEditorAdvancedQaNative]::keybd_event($Key, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    [TinyEditorAdvancedQaNative]::keybd_event($Key, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [TinyEditorAdvancedQaNative]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 400
}

function Paste-Text([string]$Text) {
    [System.Windows.Forms.Clipboard]::SetText($Text)
    Press-CtrlKey 0x41
    for ($index = 0; $index -lt 256; ++$index) {
        [TinyEditorAdvancedQaNative]::keybd_event(0x08, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 5
        [TinyEditorAdvancedQaNative]::keybd_event(0x08, 0, 2, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 5
    }
    Press-CtrlKey 0x56
    Start-Sleep -Milliseconds 800
}

function Save-WindowScreenshot([IntPtr]$Handle, [string]$Path) {
    $rect = New-Object TinyEditorAdvancedQaNative+RECT
    [TinyEditorAdvancedQaNative]::GetWindowRect($Handle, [ref]$rect) | Out-Null
    $bitmap = New-Object System.Drawing.Bitmap(($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
}

function Start-Editor([string]$Editor, [string]$Project, [string]$LogPrefix) {
    $process = Start-Process -FilePath $Editor -ArgumentList @($Project) -PassThru `
        -RedirectStandardOutput "$LogPrefix.stdout.log" -RedirectStandardError "$LogPrefix.stderr.log"
    for ($attempt = 0; $attempt -lt 40 -and $process.MainWindowHandle -eq 0; ++$attempt) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
    Start-Sleep -Seconds 3
    $process.Refresh()
    [TinyEditorAdvancedQaNative]::ShowWindow($process.MainWindowHandle, 3) | Out-Null
    [TinyEditorAdvancedQaNative]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    Start-Sleep -Seconds 2
    $rect = New-Object TinyEditorAdvancedQaNative+RECT
    [TinyEditorAdvancedQaNative]::GetWindowRect($process.MainWindowHandle, [ref]$rect) | Out-Null
    $script:WindowLeft = $rect.Left
    $script:WindowTop = $rect.Top
    return $process
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$editor = Join-Path $root 'build\Release\tinyengine_editor.exe'
$qa = Join-Path $root 'Saved\EditorAdvancedQA'
[IO.Directory]::CreateDirectory($qa) | Out-Null
$result = [ordered]@{ scenario = $Scenario; timestamp = (Get-Date).ToString('s') }

if ($Scenario -eq 'GizmoMarquee') {
    $project = Join-Path $root 'Saved\EditorQA\EditorQA.tinyproject'
    $scene = Join-Path $root 'Saved\EditorQA\assets\Demo.scene'
    $backup = [IO.File]::ReadAllBytes($scene)
    $process = Start-Editor $editor $project (Join-Path $qa 'gizmo-marquee')
    try {
        # Physical-pixel coordinates for the 125% DPI test runner.
        Click-At 88 177
        Save-WindowScreenshot $process.MainWindowHandle (Join-Path $qa 'qa-40a-gizmo-before.png')
        $process.Refresh()
        $titleBeforeDrag = $process.MainWindowTitle
        Drag-At 1320 651 1450 651
        Save-WindowScreenshot $process.MainWindowHandle (Join-Path $qa 'qa-40-gizmo-drag.png')
        $process.Refresh()
        $titleAfterDrag = $process.MainWindowTitle
        Press-CtrlKey 0x53
        $sceneText = [IO.File]::ReadAllText($scene)
        $match = [regex]::Match($sceneText, 'entity 1 0 "Player"[\s\S]*?transform ([\-0-9\.]+) ')
        $result.gizmo_player_x = if ($match.Success) { [double]$match.Groups[1].Value } else { $null }
        $result.gizmo_moved = !$titleBeforeDrag.EndsWith('*') -and $titleAfterDrag.EndsWith('*')

        Drag-At 1000 420 1500 850
        Save-WindowScreenshot $process.MainWindowHandle (Join-Path $qa 'qa-41-marquee-without-ctrl.png')
        Drag-At 1000 420 1500 850 -Ctrl
        Save-WindowScreenshot $process.MainWindowHandle (Join-Path $qa 'qa-42-marquee-with-ctrl.png')
    }
    finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        [IO.File]::WriteAllBytes($scene, $backup)
    }
}
else {
    $assetDirectory = Join-Path $qa 'assets'
    [IO.Directory]::CreateDirectory($assetDirectory) | Out-Null
    $project = Join-Path $qa 'AdvancedQA.tinyproject'
    [IO.File]::WriteAllText($project, "project.name=TinyEngineAdvancedQA`nproject.assets=assets`nproject.startupScene=assets/Empty.scene`nproject.cooked=Saved/Cooked`ninput.bindings=assets/default.input`nphysics.fixedStep=0.016666667`n")
    [IO.File]::WriteAllText((Join-Path $assetDirectory 'Empty.scene'), "TINYSCENE 7`n")
    [IO.File]::WriteAllText((Join-Path $assetDirectory 'Graph.matgraph'), "TINY_MATGRAPH 1`nnode 1 0 `"Base`" 30 60 1 0 0 1 `"`"`nnode 2 4 `"Output`" 540 100 1 1 1 1 `"`"`nlink 1 2 0`n")
    [IO.File]::WriteAllText((Join-Path $assetDirectory 'Test.frag'), "#version 330 core`nout vec4 c; void main(){ c=vec4(1.0); }`n")

    $process = Start-Editor $editor $project (Join-Path $qa 'material')
    try {
        Click-At 380 1238
        Start-Sleep -Seconds 1
        Save-WindowScreenshot $process.MainWindowHandle (Join-Path $qa 'qa-43-material-graph.png')
    }
    finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }

    $process = Start-Editor $editor $project (Join-Path $qa 'shader')
    try {
        Click-At 380 1268
        Start-Sleep -Seconds 1
        Save-WindowScreenshot $process.MainWindowHandle (Join-Path $qa 'qa-44-shader-editor.png')
        Click-At 500 300
        Paste-Text "#version 330 core`nout vec4 c; void main(){ this is invalid; }`n"
        Save-WindowScreenshot $process.MainWindowHandle (Join-Path $qa 'qa-45-shader-error.png')
        Click-At 500 300
        Paste-Text "#version 330 core`nout vec4 c; void main(){ c=vec4(0.2,0.4,1.0,1.0); }`n"
        Save-WindowScreenshot $process.MainWindowHandle (Join-Path $qa 'qa-46-shader-recovered.png')
        $shaderText = [IO.File]::ReadAllText((Join-Path $assetDirectory 'Test.frag'))
        $result.shader_source_recovered = $shaderText.Contains('vec4(0.2,0.4,1.0,1.0)') -and !$shaderText.Contains('invalid')
    }
    finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    }
}

$result | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $qa ("$($Scenario.ToLowerInvariant()).json"))
$result | ConvertTo-Json
