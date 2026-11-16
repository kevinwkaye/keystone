# Keystone: set up the footswitch and expression pedal.
#
# Plug the footswitch into the laptop by USB (power switch on U) with the expression pedal
# in its pedal jack, then run this. It listens while you press A, B, C and D and rock the
# pedal, and sets Keystone up to match whatever the footswitch sends: controllers or notes,
# any channel, the pedal either way round. Nothing on the footswitch itself is changed.
#
# Keystone is closed while it listens (Windows lets only one program read a MIDI port),
# then started again. A record goes in Documents\Keystone\footswitch setup.txt.

param([switch]$Check)   # -Check: build the MIDI reader, list devices and stop (used by the build)
$ErrorActionPreference = 'Stop'
$docs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Keystone'
New-Item -ItemType Directory -Force -Path $docs | Out-Null
$log = Join-Path $docs 'footswitch setup.txt'
$record = @("Keystone footswitch setup, $(Get-Date)")
function Say($text, $colour = 'Gray') { Write-Host $text -ForegroundColor $colour; $script:record += $text }
function Done($ok) {
    $script:record | Set-Content -Path $log -Encoding UTF8
    Write-Host ''
    if ($ok) { Write-Host 'All set. Press Enter to close.' -ForegroundColor Green } else { Write-Host 'Press Enter to close.' }
    [void](Read-Host)
    exit
}

# ---- reading MIDI through Windows' own MIDI library
Add-Type -TypeDefinition @'
using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
public static class KsMidi {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct MIDIINCAPS { public ushort wMid; public ushort wPid; public uint vDriverVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szPname; public uint dwSupport; }
    public delegate void MidiInProc(IntPtr h, uint msg, IntPtr inst, IntPtr p1, IntPtr p2);
    [DllImport("winmm.dll")] static extern uint midiInGetNumDevs();
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern uint midiInGetDevCaps(UIntPtr id, out MIDIINCAPS caps, uint size);
    [DllImport("winmm.dll")] static extern uint midiInOpen(out IntPtr h, uint id, MidiInProc cb, IntPtr inst, uint flags);
    [DllImport("winmm.dll")] static extern uint midiInStart(IntPtr h);
    [DllImport("winmm.dll")] static extern uint midiInStop(IntPtr h);
    [DllImport("winmm.dll")] static extern uint midiInClose(IntPtr h);
    public static ConcurrentQueue<int> Messages = new ConcurrentQueue<int>();
    static MidiInProc keep = Callback;
    static IntPtr handle = IntPtr.Zero;
    static void Callback(IntPtr h, uint msg, IntPtr inst, IntPtr p1, IntPtr p2) {
        if (msg == 0x3C3) Messages.Enqueue((int)(p1.ToInt64() & 0xFFFFFF));   // MIM_DATA
    }
    public static string[] Names() {
        uint n = midiInGetNumDevs();
        string[] r = new string[n];
        for (uint i = 0; i < n; i++) {
            MIDIINCAPS c;
            r[i] = midiInGetDevCaps(new UIntPtr(i), out c, (uint)Marshal.SizeOf(typeof(MIDIINCAPS))) == 0 ? c.szPname : "";
        }
        return r;
    }
    public static uint Open(uint id) {
        uint e = midiInOpen(out handle, id, keep, IntPtr.Zero, 0x30000);   // CALLBACK_FUNCTION
        if (e == 0) e = midiInStart(handle);
        return e;
    }
    public static void Close() { if (handle != IntPtr.Zero) { midiInStop(handle); midiInClose(handle); handle = IntPtr.Zero; } }
    public static void Clear() { int x; while (Messages.TryDequeue(out x)) { } }
}
'@

if ($Check) { Write-Host "MIDI reader built; $([KsMidi]::Names().Count) MIDI inputs here."; exit 0 }

function Next-Messages([double]$seconds) {
    $out = @()
    $end = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $end) {
        $m = 0
        while ([KsMidi]::Messages.TryDequeue([ref]$m)) {
            $status = $m -band 0xFF; $type = $status -band 0xF0
            if ($type -in 0x80, 0x90, 0xB0, 0xC0) {
                $out += [pscustomobject]@{ Type = $type; Channel = ($status -band 0x0F) + 1; D1 = ($m -shr 8) -band 0x7F; D2 = ($m -shr 16) -band 0x7F }
            }
        }
        Start-Sleep -Milliseconds 20
    }
    return ,$out
}

Clear-Host
Write-Host 'Keystone footswitch setup' -ForegroundColor Cyan
Write-Host ''

# ---- close Keystone so the port is free
$exe = Join-Path $env:ProgramFiles 'Keystone\Keystone.exe'
$wasRunning = $null -ne (Get-Process Keystone -ErrorAction SilentlyContinue)
if ($wasRunning) { Say 'Closing Keystone while we listen...'; Stop-Process -Name Keystone -Force; Start-Sleep -Seconds 2 }

# ---- find the footswitch
$names = [KsMidi]::Names()
if ($names.Count -eq 0) { Say 'No MIDI devices found. Plug the footswitch in by USB, set its power switch to U, and run this again.' 'Yellow'; Done $false }
$pick = -1
for ($i = 0; $i -lt $names.Count; $i++) { if ($names[$i] -match 'chocolate|footctrl|foot ctrl|foot ?switch|m-?vave|cuvave|pedal') { $pick = $i; break } }
if ($pick -lt 0) {
    Say 'MIDI devices on this laptop:'
    for ($i = 0; $i -lt $names.Count; $i++) { Say "  $($i + 1). $($names[$i])" }
    $answer = Read-Host 'Which number is the footswitch'
    $pick = [int]$answer - 1
    if ($pick -lt 0 -or $pick -ge $names.Count) { Say 'That number is not on the list.' 'Yellow'; Done $false }
}
Say "Footswitch: $($names[$pick])" 'Green'
$err = [KsMidi]::Open([uint32]$pick)
if ($err -ne 0) { Say "Windows would not open it (error $err). Close anything else using MIDI and try again." 'Yellow'; Done $false }

# ---- the four switches
$switches = @()
$warnings = @()
foreach ($name in 'A', 'B', 'C', 'D') {
    while ($true) {
        [KsMidi]::Clear()
        Write-Host ''
        Write-Host "Press footswitch $name once, then let go." -ForegroundColor Cyan
        $got = Next-Messages 6
        if ($got.Count -eq 0) { Write-Host "Nothing heard from $name. Try again." -ForegroundColor Yellow; continue }
        # a switch sends full on and off values; a pedal nudged by a foot sends values in between
        $clean = @($got | Where-Object { $_.Type -ne 0xB0 -or $_.D2 -eq 0 -or $_.D2 -ge 100 })
        $first = if ($clean.Count) { $clean[0] } else { $got[0] }
        if ($first.Type -eq 0xC0) {
            Say "Footswitch $name sends program changes, which can't be held. Keystone needs it to send a controller." 'Yellow'
            $warnings += 'program'
            $switches += [pscustomobject]@{ Name = $name; Kind = 'pc'; Channel = $first.Channel; Number = $first.D1; Toggle = $false }
            break
        }
        if ($first.Type -eq 0x90 -or $first.Type -eq 0x80) {
            $num = 128 + $first.D1
            $offs = @($got | Where-Object { ($_.Type -eq 0x80 -or ($_.Type -eq 0x90 -and $_.D2 -eq 0)) -and $_.D1 -eq $first.D1 })
            $toggle = $offs.Count -eq 0
            Say ("Footswitch {0}: note {1} on channel {2}{3}" -f $name, $first.D1, $first.Channel, $(if ($toggle) { ', latching' } else { '' }))
            $switches += [pscustomobject]@{ Name = $name; Kind = 'note'; Channel = $first.Channel; Number = $num; Toggle = $toggle }
        } else {
            $same = @($got | Where-Object { $_.Type -eq 0xB0 -and $_.D1 -eq $first.D1 })
            $toggle = -not (($same | Where-Object { $_.D2 -ge 64 }) -and ($same | Where-Object { $_.D2 -lt 64 }))
            Say ("Footswitch {0}: controller {1} on channel {2}{3}" -f $name, $first.D1, $first.Channel, $(if ($toggle) { ', latching' } else { '' }))
            $switches += [pscustomobject]@{ Name = $name; Kind = 'cc'; Channel = $first.Channel; Number = $first.D1; Toggle = $toggle }
        }
        if ($toggle) { $warnings += 'toggle' }
        break
    }
}
$used = @($switches | ForEach-Object { "$($_.Kind)$($_.Number)" })
if (($used | Select-Object -Unique).Count -lt 4) { Say 'Two footswitches send the same message, so Keystone could not tell them apart.' 'Yellow'; $warnings += 'same' }

# ---- the expression pedal
$expCC = -1; $expCh = 1; $heel = 0; $toe = 127
Write-Host ''
Write-Host 'Now rock the expression pedal slowly, heel to toe, two or three times.' -ForegroundColor Cyan
[KsMidi]::Clear()
$moves = Next-Messages 8
$switchCCs = @($switches | Where-Object { $_.Kind -eq 'cc' } | ForEach-Object { $_.Number })
$ccs = @($moves | Where-Object { $_.Type -eq 0xB0 -and $switchCCs -notcontains $_.D1 })
if ($ccs.Count -lt 5) {
    Say 'Nothing heard from the expression pedal. Check it is in the footswitch''s pedal jack (with the 3.5 mm adapter) and that the jack is set to Expression Pedal. The footswitches are set up anyway.' 'Yellow'
    $warnings += 'exp'
} else {
    $best = $ccs | Group-Object D1 | Sort-Object Count -Descending | Select-Object -First 1
    $expCC = [int]$best.Name
    $expCh = ($best.Group | Select-Object -First 1).Channel
    Write-Host ''
    Write-Host 'Put the pedal fully back on its heel, keep it there and press Enter.' -ForegroundColor Cyan
    [KsMidi]::Clear()
    [void](Read-Host)
    Write-Host 'Wiggle it a little at the heel end, then hold it fully back.' -ForegroundColor DarkGray
    $h = Next-Messages 3
    $hv = @($h | Where-Object { $_.Type -eq 0xB0 -and $_.D1 -eq $expCC } | ForEach-Object { $_.D2 })
    Write-Host 'Now push it fully down on the toe, keep it there and press Enter.' -ForegroundColor Cyan
    [KsMidi]::Clear()
    [void](Read-Host)
    Write-Host 'Wiggle it a little at the toe end, then hold it fully down.' -ForegroundColor DarkGray
    $t = Next-Messages 3
    $tv = @($t | Where-Object { $_.Type -eq 0xB0 -and $_.D1 -eq $expCC } | ForEach-Object { $_.D2 })
    $all = @($ccs | Where-Object { $_.D1 -eq $expCC } | ForEach-Object { $_.D2 })
    $lo = ($all | Measure-Object -Minimum).Minimum; $hi = ($all | Measure-Object -Maximum).Maximum
    # the heel end is whichever extreme the heel readings sit nearest
    $heelSide = if ($hv.Count) { [int](($hv | Measure-Object -Average).Average) } elseif ($tv.Count) { 127 - [int](($tv | Measure-Object -Average).Average) } else { $lo }
    if ([math]::Abs($heelSide - $hi) -lt [math]::Abs($heelSide - $lo)) { $heel = $hi; $toe = $lo } else { $heel = $lo; $toe = $hi }
    Say ("Expression: controller {0} on channel {1}, heel {2}, toe {3}{4}" -f $expCC, $expCh, $heel, $toe, $(if ($heel -gt $toe) { ' (reads backwards, fixed)' } else { '' }))
    if ([math]::Abs($toe - $heel) -lt 60) { Say 'The pedal only moved part of its range. If the sweep feels short, run this again and push it all the way.' 'Yellow' }
}
[KsMidi]::Close()

# ---- write Keystone's pedal settings (shared with the Flip Fella plugins, other settings kept)
$dir = Join-Path $env:APPDATA 'Kevin Kaye'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$file = Join-Path $dir 'Flip Fella Pedals.xml'
$xml = New-Object System.Xml.XmlDocument
if (Test-Path $file) { try { $xml.Load($file) } catch { $xml = New-Object System.Xml.XmlDocument } }
if (-not $xml.DocumentElement -or $xml.DocumentElement.Name -ne 'Pedals') {
    $xml = New-Object System.Xml.XmlDocument
    [void]$xml.AppendChild($xml.CreateXmlDeclaration('1.0', 'UTF-8', $null))
    [void]$xml.AppendChild($xml.CreateElement('Pedals'))
}
$root = $xml.DocumentElement
$toggleBits = 0
for ($i = 0; $i -lt 4; $i++) {
    $s = $switches[$i]
    if ($s.Kind -eq 'pc') { continue }   # left as it was
    $root.SetAttribute("fsChannel$i", [string]$s.Channel)
    $root.SetAttribute("fsCC$i", [string]$s.Number)
    if ($s.Toggle) { $toggleBits = $toggleBits -bor (1 -shl $i) }
}
$root.SetAttribute('fsToggle', [string]$toggleBits)
if ($expCC -ge 0) {
    $root.SetAttribute('expChannel', [string]$expCh)
    $root.SetAttribute('expCC', [string]$expCC)
    $root.SetAttribute('heel', [string]$heel)
    $root.SetAttribute('toe', [string]$toe)
}
$root.SetAttribute('footDevice', '')   # find it automatically
$xml.Save($file)
Say "Saved to $file" 'Green'

# ---- what can't be fixed from here
if ($warnings -contains 'program' -or $warnings -contains 'toggle') {
    Write-Host ''
    Say 'One thing to change on the footswitch itself, in the CubeSuite app:' 'Yellow'
    Say '  Custom mode, footswitches A to D on controllers 20, 21, 22 and 23, set to momentary,' 'Yellow'
    Say '  pedal jack as Expression Pedal on controller 11. Then run this again.' 'Yellow'
    if ($warnings -contains 'toggle' -and $warnings -notcontains 'program') { Say '  Until then the latching switches tap fine, but their hold jobs (songs, tuner, slap, Keep) won''t work.' 'Yellow' }
}

# ---- start Keystone again
if (Test-Path $exe) {
    Start-Process -FilePath $exe -ArgumentList '--background'
    Say 'Keystone started. Try the switches: tap A for Edge and Drive, hold B for the next song.' 'Green'
} else {
    Say "Keystone isn't installed at $exe, so it wasn't started." 'Yellow'
}
Done ($warnings.Count -eq 0)
