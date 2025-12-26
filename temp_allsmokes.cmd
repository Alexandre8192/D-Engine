@echo off
rem MSVC: /std:c++latest enables std::print path; fallback exists but we keep the newest mode here.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++latest /Zc:preprocessor /Zi /MDd /I"Source" ^
	tests\AllSmokes\AllSmokes_main.cpp ^
	tests\Audio_smoke.cpp ^
	tests\BasicForwardRenderer_smoke.cpp ^
	tests\RendererSystem_smoke.cpp ^
	tests\RendererSystem_BasicForwardRenderer_smoke.cpp ^
	tests\Time_smoke.cpp ^
	tests\FileSystem_smoke.cpp ^
	tests\Window_smoke.cpp ^
	tests\Input_smoke.cpp ^
	tests\Jobs_smoke.cpp ^
	/link /OUT:AllSmokes.exe x64\Debug\D-Engine.lib /SUBSYSTEM:CONSOLE
