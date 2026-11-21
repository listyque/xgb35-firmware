param(
    [string]$OutExe = "D:\grav\g18\dist_win64\gorky18_port.exe",
    [switch]$WithFFmpeg
)
$ErrorActionPreference = "Stop"
Set-Location "D:\grav\g18\port_zig"
$zig = "D:\grav\g18\.toolchain\zig-x86_64-windows-0.17.1\zig.exe"
$gcc = "D:\grav\g18\.toolchain\w64devkit\bin\gcc.exe"
$sdl = "D:\grav\g18\.toolchain\SDL2-2.32.10\x86_64-w64-mingw32"
$ff  = "D:\grav\g18\.toolchain\ffmpeg-n8.1-latest-win64-lgpl-shared-8.1"

New-Item -ItemType Directory -Force -Path "obj" | Out-Null
if (!(Test-Path "obj\clay.o"))         { & $gcc -O2 -c "third_party\clay\clay.c" -I "third_party\clay" -o "obj\clay.o" }
if (!(Test-Path "obj\stb_truetype.o")) { & $gcc -O2 -c "third_party\stb\stb_truetype.c" -I "third_party\stb" -o "obj\stb_truetype.o" }
if (!(Test-Path "obj\stb_image.o"))    { & $gcc -O1 -c "third_party\stb\stb_image.c" -I "third_party\stb" -o "obj\stb_image.o" }
if (!(Test-Path "obj\dr_libs.o"))      { & $gcc -O2 -c "third_party\dr_libs\dr_libs.c" -I "third_party\dr_libs" -o "obj\dr_libs.o" }
& $gcc -O2 -c "src\viewer\sdl2_compat.c" -I "$sdl\include\SDL2" -I "$sdl\include" -o "obj\sdl2_compat.o"

$opt2 = (Get-ChildItem ".zig-cache\o\*\options.zig" | Select-String "version" | Select-Object -First 1).Path
$zigArgs = @(
    "build-obj", "-fno-strip", "-OReleaseSafe",
    "--dep", "opengorky", "--dep", "sdl", "--dep", "build_options",
    "--dep", "icon_png", "--dep", "panel_plate_png", "--dep", "kit", "--dep", "ui",
    "--dep", "font_regular", "--dep", "font_bold", "--dep", "font_heading",
    "--dep", "launcher_settings", "--dep", "launcher_info", "--dep", "launcher_back",
    "--dep", "launcher_check", "--dep", "launcher_warning", "--dep", "launcher_play",
    "--dep", "launcher_background", "--dep", "version"
)
if ($WithFFmpeg) { $zigArgs += @("--dep", "ffmpeg") }
$zigArgs += @(
    "-Mroot=src\viewer\main.zig",
    "-OReleaseSafe", "--dep", "foundation_traits", "--dep", "foundation_types", "--dep", "foundation_hosts", "-Mopengorky=src\root.zig",
    "-OReleaseSafe", "-Msdl=src\viewer\sdl_gen.zig"
)
if ($WithFFmpeg) {
    $opt1 = (Get-ChildItem ".zig-cache\o\*\options.zig" | Select-String "ffmpeg" | Select-Object -First 1).Path
    $zigArgs += @("-OReleaseSafe", "-Mffmpeg=src\viewer\ffmpeg_gen.zig", "-Mbuild_options=$opt1")
} else {
    $zigArgs += @("-Mbuild_options=src\viewer\build_opts_noffmpeg.zig")
}
$zigArgs += @(
    "-Micon_png=assets\icon-256.png", "-Mpanel_plate_png=assets\panel-plate.png",
    "-OReleaseSafe", "--dep", "sdl", "--dep", "ui", "--dep", "opengorky", "-Mkit=src\editor\kit.zig",
    "-OReleaseSafe", "-Mui=src\editor\ui_gen.zig",
    "-Mfont_regular=assets\fonts\PTSans-Regular.ttf", "-Mfont_bold=assets\fonts\PTSans-Bold.ttf", "-Mfont_heading=assets\fonts\Oswald-Bold.ttf",
    "-Mlauncher_settings=assets\launcher\icons\settings.png", "-Mlauncher_info=assets\launcher\icons\info.png",
    "-Mlauncher_back=assets\launcher\icons\back.png", "-Mlauncher_check=assets\launcher\icons\check.png",
    "-Mlauncher_warning=assets\launcher\icons\warning.png", "-Mlauncher_play=assets\launcher\icons\play.png",
    "-Mlauncher_background=assets\launcher\textures\background.jpg",
    "-Mversion=$opt2",
    "-Mfoundation_traits=foundation\traits.ogs", "-Mfoundation_types=foundation\types.ogs", "-Mfoundation_hosts=foundation\hosts.ogs",
    "-lc", "-femit-bin=obj\opengorky.obj"
)

& $zig @zigArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$linkLibs = @(
    "obj\opengorky.obj", "obj\clay.o", "obj\stb_truetype.o", "obj\stb_image.o", "obj\dr_libs.o", "obj\sdl2_compat.o",
    "$sdl\lib\libSDL2.dll.a"
)
if ($WithFFmpeg) {
    $linkLibs += @(
        "$ff\lib\libavformat.dll.a", "$ff\lib\libavcodec.dll.a", "$ff\lib\libswscale.dll.a",
        "$ff\lib\libswresample.dll.a", "$ff\lib\libavutil.dll.a", "-lbcrypt"
    )
}
$linkLibs += @("-lntdll", "-lws2_32", "-lkernel32", "-luser32", "-ladvapi32", "-lshell32", "-lole32", "-lm")

& $gcc -mwindows @linkLibs -o $OutExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Built: $OutExe ($((Get-Item $OutExe).Length) bytes)"
