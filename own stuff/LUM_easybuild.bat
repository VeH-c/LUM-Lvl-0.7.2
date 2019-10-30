call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat"
cd "C:\Users\Nolay\Desktop\TeeParty\Teeworlds collection\LUM_Source 0.7.2"

:[START]
taskkill /IM teeworlds_srv.exe
.\bam\bam conf=release server
pause
goto [START]