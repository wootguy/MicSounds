cd "C:\Games\Steam\steamapps\common\Sven Co-op\svencoop\addons\metamod\dlls"

if exist MicSounds_old.dll (
    del MicSounds_old.dll
)
if exist MicSounds.dll (
    rename MicSounds.dll MicSounds_old.dll 
)

exit /b 0